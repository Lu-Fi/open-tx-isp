// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: CDNS per-frame paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_cdns_{par_refresh,refresh,dn_params_refresh,wake_up} from
 * driver/t32/tx_isp_t32_cdns.h and compares every register write and the
 * changed context fields.
 *
 * Fixed-seed random cases cover random gain, threshold, tuning bytes,
 * channels and initial gain, plus boundary cases for the refresh hysteresis
 * at threshold-1/threshold/threshold+1 and the initial -1 gain path.
 * The refresh path is compared as tisp_cdns_par_refresh(ch, gain, 256)
 * (see below: the wrapper ref does not compile as C23).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_cdns.h"

#define SECB		T32_CDNS_SEC_BYTES
#define SEC_OFF		0xcfa0u
#define MAXW		96

/* env.c globals the reference functions touch */
extern u32 t32_data_e23d0[4];

/*
 * Unmodified vendor functions from ref/ (Matt Davis' recovered C).
 * tisp_cdns_refresh (fn_...760b8) is `return tisp_cdns_par_refresh(ch, gain,
 * 256);` but its ref declares `extern int tisp_cdns_par_refresh()` (empty
 * prototype), which C23 rejects at compile time in a read-only file; the
 * refresh case therefore calls par_refresh with 256, the same vendor call.
 */
extern u64 tisp_cdns_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_cdns_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_cdns_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x7a19c3u;
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

static void gen_random_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7);
}

static int run_case(const char *what, int which, u32 ch, u32 initial_gain,
		    u32 gain, u32 threshold, const u8 *section)
{
	u8 *base_ref, *param_ref, *ctx_ref;
	u8 *base_our, *param_our;
	struct t32_cdns_ctx our_ctx;
	u8 initial_param[39], after_ref_param[39], after_our_param[39];
	u32 ref_gain = 0, our_gain = 0, ref_rsvd = 0, our_rsvd = 0;
	u32 ref_sec_offset = 0, our_sec_offset = 0;
	struct t32_front_write refw[MAXW], ow[MAXW];
	unsigned int ref_n = 0, i;
	int our_n = 0;

	(void)what;

	/* Reference side: vendor context is a 16-byte struct with u32 fields. */
	t32o_reset();
	base_ref = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_ref)
		return 2;
	memcpy(base_ref + SEC_OFF, section, SECB);
	param_ref = t32o_alloc(64);
	ctx_ref = t32o_alloc(16);
	if (!param_ref || !ctx_ref)
		return 2;
	for (i = 0; i < 39; i++)
		param_ref[i] = (u8)rnd();
	memcpy(initial_param, param_ref, 39);

	*(u32 *)(ctx_ref + 0)  = T32O_PTR(base_ref + SEC_OFF);
	*(u32 *)(ctx_ref + 4)  = T32O_PTR(param_ref);
	*(u32 *)(ctx_ref + 8)  = initial_gain;
	*(u32 *)(ctx_ref + 12) = 0;

	t32_data_e23d0[ch] = T32O_PTR(ctx_ref);
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);

	switch (which) {
	case CASE_PAR:
		t32o_call((t32o_fn)(void (*)(void))tisp_cdns_par_refresh,
			  ch, gain, threshold, 0, 0);
		break;
	case CASE_REFRESH:
		/* tisp_cdns_refresh: tisp_cdns_par_refresh(ch, gain, 256) */
		t32o_call((t32o_fn)(void (*)(void))tisp_cdns_par_refresh,
			  ch, gain, 256, 0, 0);
		break;
	case CASE_DN:
		t32o_call((t32o_fn)(void (*)(void))tisp_cdns_dn_params_refresh,
			  ch, 0, 0, 0, 0);
		break;
	case CASE_WAKE:
		t32o_call((t32o_fn)(void (*)(void))tisp_cdns_wake_up,
			  ch, 0, 0, 0, 0);
		break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	memcpy(after_ref_param, param_ref, 39);
	ref_gain = *(u32 *)(ctx_ref + 8);
	ref_rsvd = *(u32 *)(ctx_ref + 12);
	ref_sec_offset = *(u32 *)(ctx_ref + 0) - T32O_PTR(base_ref);

	/* Our side: native struct context. */
	t32o_reset();
	base_our = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_our)
		return 2;
	memcpy(base_our + SEC_OFF, section, SECB);
	param_our = t32o_alloc(64);
	if (!param_our)
		return 2;
	our_ctx.sec = base_our + SEC_OFF;
	our_ctx.param = param_our;
	our_ctx.gain = initial_gain;
	our_ctx.rsvd = 0;
	memcpy(param_our, initial_param, 39);

	switch (which) {
	case CASE_PAR:
		our_n = t32_cdns_par_refresh(&our_ctx, (u8)ch, gain, threshold, ow);
		break;
	case CASE_REFRESH:
		our_n = t32_cdns_refresh(&our_ctx, (u8)ch, gain, ow);
		break;
	case CASE_DN:
		our_n = t32_cdns_dn_params_refresh(&our_ctx, (u8)ch, base_our, ow);
		break;
	case CASE_WAKE:
		our_n = t32_cdns_wake_up(&our_ctx, (u8)ch, ow);
		break;
	}
	memcpy(after_our_param, param_our, 39);
	our_gain = our_ctx.gain;
	our_rsvd = our_ctx.rsvd;
	our_sec_offset = (u32)((uintptr_t)our_ctx.sec - (uintptr_t)base_our);

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	if (ref_gain != our_gain || ref_rsvd != our_rsvd)
		goto bad;
	if (ref_sec_offset != our_sec_offset)
		goto bad;
	if (memcmp(after_ref_param, after_our_param, 39))
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
				       (int)i < our_n ? ow[i].reg : 0, (int)i < our_n ? ow[i].val : 0);
				if (i > 8)
					break;
			}
		printf("  ctx gain ref %08x ours %08x, sec off ref %08x ours %08x\n",
		       ref_gain, our_gain, ref_sec_offset, our_sec_offset);
	}
	return 1;
}

static void run_boundaries(void)
{
	static const u8 zero[SECB];

	run_case("par-bound-n", CASE_PAR, 0, 0, 9, 10, zero);
	run_case("par-bound-e", CASE_PAR, 0, 0, 10, 10, zero);
	run_case("par-bound-p", CASE_PAR, 0, 0, 11, 10, zero);
	run_case("par-init-neg", CASE_PAR, 0, 0xffffffffu, 12345, 10, zero);
	run_case("par-init-neg2", CASE_PAR, 0, 0xffffffffu, 0xfffffffeu, 10, zero);
	run_case("par-rev-eq", CASE_PAR, 0, 100, 90, 10, zero);
	run_case("par-rev-below", CASE_PAR, 0, 100, 90, 11, zero);

	run_case("refresh-bound-n", CASE_REFRESH, 0, 0, 255, 0, zero);
	run_case("refresh-bound-e", CASE_REFRESH, 0, 0, 256, 0, zero);
	run_case("refresh-bound-p", CASE_REFRESH, 0, 0, 257, 0, zero);
	run_case("refresh-init-neg", CASE_REFRESH, 0, 0xffffffffu, 12345, 0, zero);

	run_case("dn-zero", CASE_DN, 0, 0, 0, 0, zero);
	run_case("dn-gain", CASE_DN, 1, 0x12345678u, 0, 0, zero);
	run_case("wake-zero", CASE_WAKE, 0, 0, 0, 0, zero);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		u8 sec[SECB];
		u32 ch = rnd() & 3;
		u32 initial_gain = (c % 5 == 0) ? 0xffffffffu : rnd();
		u32 gain = rnd();
		u32 threshold = (c & 1) ? (rnd() & 0x1ff) : rnd();

		gen_random_section(sec);
		run_case("par-random", CASE_PAR, ch, initial_gain, gain, threshold, sec);

		gen_random_section(sec);
		run_case("refresh-random", CASE_REFRESH, ch, initial_gain, gain, 0, sec);

		gen_random_section(sec);
		run_case("dn-random", CASE_DN, ch, initial_gain, gain, 0, sec);

		gen_random_section(sec);
		run_case("wake-random", CASE_WAKE, ch, initial_gain, 0, 0, sec);
	}
	run_boundaries();

	printf("t32_cdnsf_oracle: random %u compared, %u mismatches\n", compared, failed);
	if (failed)
		return 1;
	return 0;
}

