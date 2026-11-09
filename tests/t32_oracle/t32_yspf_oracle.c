// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: YSP per-frame paths.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_ysp_par_refresh,
 * tisp_ysp_refresh, tisp_ysp_dn_params_refresh and tisp_ysp_wake_up, and our
 * readable t32_ysp_* from driver/t32/tx_isp_t32_ysp.h, from the same initial
 * context state, and compares every register write (address, value, order),
 * the interpolated value block and the context fields the vendor changes.
 *
 * The vendor context (t32_data_e2424[ch]) is 20 bytes: +0 section pointer,
 * +4 value block, +8 accepted gain, +12/+16 unused by these functions.
 * Section 21 sits at tuning base + 0xd630.  tisp_ysp_dn_params_refresh also
 * copies the section to t32_data_e2420[0]; that scratch is supplied to the
 * vendor but not compared (env.c's memcpy does not reproduce the copy).
 *
 * Fixed-seed random cases cover random gain, threshold, channel and tuning
 * bytes; boundary cases exercise the first-refresh (-1) path, the
 * threshold-1/threshold/threshold+1 hysteresis, the 256 step of
 * tisp_ysp_refresh and the interpolation knots.
 *
 * Exit 0 = all compared cases equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ysp.h"

#define SECB		T32_YSP_SEC_BYTES
#define SEC_OFF		0xd630u
#define CTXB		20u
#define MAXW		T32_YSP_MAX_WRITES

/* env.c provides these per-channel and scratch objects; declare them */
extern u32 t32_data_e2424[4];
extern u32 t32_data_e2420[4];

/* unmodified vendor functions from ref/ (Matt Davis' recovered C) */
extern u64 tisp_ysp_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ysp_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ysp_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ysp_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x6f31a5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

enum {
	CASE_PAR,
	CASE_REFRESH,
	CASE_DN,
	CASE_WAKE
};

static void gen_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7);
}

static int run_case(const char *what, int which, u32 ch, u32 initial_gain,
		    u32 gain, u32 threshold, u8 *section)
{
	u8 *base_ref, *param_ref, *ctx_ref, *dn_ref;
	u8 *base_our, *param_our, *dn_our;
	struct t32_ysp_ctx *our_ctx;
	u8 initial_param[T32_YSP_VALUES];
	u32 ref_gain, our_gain, ref_sec, our_sec, ref_par, our_par;
	struct t32_front_write refw[MAXW], ow[MAXW];
	unsigned int ref_n, i;
	int our_n = 0;

	/* both sides start from the value block of the stored gain */
	t32_ysp_intp(section, initial_param,
		     initial_gain == (u32)-1 ? 0U : initial_gain);

	/* reference side: vendor context and globals */
	t32o_reset();
	base_ref = t32o_alloc(SEC_OFF + SECB + 64);
	param_ref = t32o_alloc(T32_YSP_VALUES + 16);
	ctx_ref = t32o_alloc(CTXB);
	dn_ref = t32o_alloc(SECB + 64);
	if (!base_ref || !param_ref || !ctx_ref || !dn_ref)
		return 2;
	memcpy(base_ref + SEC_OFF, section, SECB);
	memcpy(param_ref, initial_param, T32_YSP_VALUES);
	memset(dn_ref, 0x5a, SECB);
	*(u32 *)(ctx_ref + 0) = T32O_PTR(base_ref + SEC_OFF);
	*(u32 *)(ctx_ref + 4) = T32O_PTR(param_ref);
	*(u32 *)(ctx_ref + 8) = initial_gain;
	*(u32 *)(ctx_ref + 12) = 0;
	*(u32 *)(ctx_ref + 16) = 0;

	t32_data_e2424[ch] = T32O_PTR(ctx_ref);
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);
	t32_data_e2420[0] = T32O_PTR(dn_ref);

	switch (which) {
	case CASE_PAR:
		t32o_call((t32o_fn)(void (*)(void))tisp_ysp_par_refresh,
			  ch, gain, threshold, 0, 0);
		break;
	case CASE_REFRESH:
		t32o_call((t32o_fn)(void (*)(void))tisp_ysp_refresh,
			  ch, gain, 0, 0, 0);
		break;
	case CASE_DN:
		t32o_call((t32o_fn)(void (*)(void))tisp_ysp_dn_params_refresh,
			  ch, 0, 0, 0, 0);
		break;
	case CASE_WAKE:
		t32o_call((t32o_fn)(void (*)(void))tisp_ysp_wake_up,
			  ch, 0, 0, 0, 0);
		break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	ref_gain = *(u32 *)(ctx_ref + 8);
	ref_sec = *(u32 *)(ctx_ref + 0) - T32O_PTR(base_ref);
	ref_par = *(u32 *)(ctx_ref + 4) - T32O_PTR(param_ref);

	/* our side: native context */
	t32o_reset();
	base_our = t32o_alloc(SEC_OFF + SECB + 64);
	param_our = t32o_alloc(T32_YSP_VALUES + 16);
	dn_our = t32o_alloc(SECB + 64);
	our_ctx = t32o_alloc(sizeof(*our_ctx));
	if (!base_our || !param_our || !dn_our || !our_ctx)
		return 2;
	memcpy(base_our + SEC_OFF, section, SECB);
	memcpy(param_our, initial_param, T32_YSP_VALUES);
	memset(dn_our, 0x5a, SECB);
	our_ctx->sec = base_our + SEC_OFF;
	our_ctx->param = param_our;
	our_ctx->gain = initial_gain;
	our_ctx->rsvd = 0;

	switch (which) {
	case CASE_PAR:
		our_n = t32_ysp_par_refresh(our_ctx, (u8)ch, gain, threshold, ow);
		break;
	case CASE_REFRESH:
		our_n = t32_ysp_refresh(our_ctx, (u8)ch, gain, ow);
		break;
	case CASE_DN:
		our_n = t32_ysp_dn_params_refresh(our_ctx, (u8)ch, base_our,
						  dn_our, ow);
		break;
	case CASE_WAKE:
		our_n = t32_ysp_wake_up(our_ctx, (u8)ch, ow);
		break;
	}
	our_gain = our_ctx->gain;
	our_sec = (u32)((uintptr_t)our_ctx->sec - (uintptr_t)base_our);
	our_par = (u32)((uintptr_t)our_ctx->param - (uintptr_t)param_our);

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	if (ref_gain != our_gain || ref_sec != our_sec || ref_par != our_par)
		goto bad;
	if (memcmp(param_ref, param_our, T32_YSP_VALUES))
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s: writes ref %u ours %d\n", what, ref_n, our_n);
		for (i = 0; i < ref_n || (int)i < our_n; i++)
			if (i >= ref_n || (int)i >= our_n ||
			    refw[i].reg != ow[i].reg || refw[i].val != ow[i].val) {
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
				       i < ref_n ? refw[i].reg : 0, i < ref_n ? refw[i].val : 0,
				       (int)i < our_n ? ow[i].reg : 0,
				       (int)i < our_n ? ow[i].val : 0);
				if (i > 8)
					break;
			}
		printf("  ctx gain ref %08x ours %08x, sec off ref %08x ours %08x, param off ref %08x ours %08x\n",
		       ref_gain, our_gain, ref_sec, our_sec, ref_par, our_par);
		for (i = 0; i < T32_YSP_VALUES; i++)
			if (param_ref[i] != param_our[i]) {
				printf("  param+0x%x vendor %02x ours %02x\n", i,
				       param_ref[i], param_our[i]);
				break;
			}
	}
	return 1;
}

static void run_boundaries(void)
{
	static u8 zero[SECB];
	u8 sec[SECB];

	/* the first refresh (stored gain -1) takes the all-reg refresh path */
	run_case("par-first", CASE_PAR, 0, 0xffffffffu, 0x10000, 256, zero);
	run_case("par-first0", CASE_PAR, 1, 0xffffffffu, 0, 0, zero);

	/* hysteresis at threshold-1 / threshold / threshold+1, both directions */
	run_case("par-thr-n", CASE_PAR, 0, 1000, 1009, 10, zero);
	run_case("par-thr-e", CASE_PAR, 0, 1000, 1010, 10, zero);
	run_case("par-thr-p", CASE_PAR, 0, 1000, 1011, 10, zero);
	run_case("par-thr-dn", CASE_PAR, 0, 1000, 990, 10, zero);
	run_case("par-thr-dn1", CASE_PAR, 0, 1000, 989, 10, zero);
	run_case("par-same0", CASE_PAR, 0, 1000, 1000, 0, zero);
	run_case("par-same1", CASE_PAR, 0, 1000, 1000, 1, zero);

	/* tisp_ysp_refresh passes the vendor's 256 */
	run_case("refresh-255", CASE_REFRESH, 0, 1000, 1255, 0, zero);
	run_case("refresh-256", CASE_REFRESH, 0, 1000, 1256, 0, zero);
	run_case("refresh-257", CASE_REFRESH, 0, 1000, 1257, 0, zero);
	run_case("refresh-first", CASE_REFRESH, 2, 0xffffffffu, 0x10000, 0, zero);

	/* interpolation knots and the 16.16 range */
	run_case("par-knot0", CASE_PAR, 0, 0xffffffffu, 0, 0, zero);
	run_case("par-knot3", CASE_PAR, 0, 0xffffffffu, 0x30000, 0, zero);
	run_case("par-knotmax", CASE_PAR, 3, 0xffffffffu, 0xffffffffu, 0, zero);

	run_case("dn-zero", CASE_DN, 0, 0, 0, 0, zero);
	run_case("dn-one", CASE_DN, 3, 0x10000, 0, 0, zero);
	run_case("dn-first", CASE_DN, 1, 0xffffffffu, 0, 0, zero);
	run_case("wake-zero", CASE_WAKE, 0, 0, 0, 0, zero);
	run_case("wake-knot", CASE_WAKE, 2, 0x20000, 0, 0, zero);

	/* random sections through every entry point */
	gen_section(sec);
	run_case("par-sec", CASE_PAR, 1, 0xffffffffu, 0x12345, 7, sec);
	run_case("refresh-sec", CASE_REFRESH, 1, 0x2000, 0x4000, 0, sec);
	run_case("dn-sec", CASE_DN, 1, 0x2000, 0, 0, sec);
	run_case("wake-sec", CASE_WAKE, 1, 0x2000, 0, 0, sec);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		u8 sec[SECB];
		u32 ch = rnd() & 3;
		u32 initial_gain = rnd();
		u32 gain = rnd();
		u32 threshold = rnd();

		if ((c & 7) == 0)
			initial_gain = 0xffffffffu;	/* first refresh */
		if ((c & 7) == 1)
			gain = initial_gain;		/* no move */
		if ((c & 7) == 2) {			/* exactly the threshold */
			threshold = rnd() & 0xffff;
			gain = initial_gain + threshold;
		}

		gen_section(sec);
		run_case("par-random", CASE_PAR, ch, initial_gain, gain, threshold, sec);

		gen_section(sec);
		run_case("refresh-random", CASE_REFRESH, ch, initial_gain, gain, 0, sec);

		gen_section(sec);
		run_case("dn-random", CASE_DN, ch, initial_gain, gain, 0, sec);

		gen_section(sec);
		run_case("wake-random", CASE_WAKE, ch, initial_gain, gain, 0, sec);
	}
	run_boundaries();

	printf("t32_yspf_oracle: random %u compared, %u mismatches\n", compared, failed);
	if (failed)
		return 1;
	return 0;
}
