// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: YDNS per-frame paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_ydns_{par_refresh,refresh,dn_params_refresh,wake_up} from
 * driver/t32/tx_isp_t32_ydns.h and compares every register write and the
 * changed context fields.
 *
 * Fixed-seed random cases cover random gain, threshold, tuning bytes and
 * channels.  Boundary cases exercise the refresh hysteresis at
 * threshold-1/threshold/threshold+1 and the initial -1 gain path.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ydns.h"

#define SECB		T32_YDNS_SEC_BYTES
#define SEC_OFF		T32_YDNS_SEC_OFF
#define MAXW		64

/* env.c global used by the reference functions */
extern u32 t32_data_e23c0[4];

/* unmodified vendor functions from ref/ (Matt Davis' recovered C) */
extern u64 tisp_ydns_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ydns_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ydns_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ydns_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

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
	struct t32_ydns_ctx *our_ctx;
	u8 initial_param[22], after_ref_param[22], after_our_param[22];
	u32 ref_gain = 0, our_gain = 0;
	u32 ref_sec_offset = 0, our_sec_offset = 0;
	u32 ref_threshold, our_threshold;
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
	for (i = 0; i < 22; i++)
		param_ref[i] = (u8)rnd();
	memcpy(initial_param, param_ref, 22);

	*(u32 *)(ctx_ref + 0)  = T32O_PTR(base_ref + SEC_OFF);
	*(u32 *)(ctx_ref + 4)  = T32O_PTR(param_ref);
	*(u32 *)(ctx_ref + 8)  = initial_gain;
	*(u32 *)(ctx_ref + 12) = threshold;

	t32_data_e23c0[ch] = T32O_PTR(ctx_ref);
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);

	switch (which) {
	case CASE_PAR:
		t32o_call((t32o_fn)(void (*)(void))tisp_ydns_par_refresh,
			  ch, gain, threshold, 0, 0);
		break;
	case CASE_REFRESH:
		t32o_call((t32o_fn)(void (*)(void))tisp_ydns_refresh,
			  ch, gain, threshold, 0, 0);
		break;
	case CASE_DN:
		t32o_call((t32o_fn)(void (*)(void))tisp_ydns_dn_params_refresh,
			  ch, 0, 0, 0, 0);
		break;
	case CASE_WAKE:
		t32o_call((t32o_fn)(void (*)(void))tisp_ydns_wake_up,
			  ch, 0, 0, 0, 0);
		break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	memcpy(after_ref_param, param_ref, 22);
	ref_gain = *(u32 *)(ctx_ref + 8);
	ref_sec_offset = *(u32 *)(ctx_ref + 0) - T32O_PTR(base_ref);
	ref_threshold = *(u32 *)(ctx_ref + 12);

	/* Our side: native struct context. */
	t32o_reset();
	base_our = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_our)
		return 2;
	memcpy(base_our + SEC_OFF, section, SECB);
	param_our = t32o_alloc(64);
	our_ctx = t32o_alloc(sizeof(*our_ctx));
	if (!param_our || !our_ctx)
		return 2;
	our_ctx->sec = base_our + SEC_OFF;
	our_ctx->param = param_our;
	our_ctx->gain = initial_gain;
	our_ctx->threshold = threshold;
	memcpy(param_our, initial_param, 22);

	switch (which) {
	case CASE_PAR:
		our_n = t32_ydns_par_refresh(our_ctx, (u8)ch, gain, ow);
		break;
	case CASE_REFRESH:
		our_n = t32_ydns_refresh(our_ctx, (u8)ch, gain, ow);
		break;
	case CASE_DN:
		our_n = t32_ydns_dn_params_refresh(our_ctx, (u8)ch, base_our, ow);
		break;
	case CASE_WAKE:
		our_n = t32_ydns_wake_up(our_ctx, (u8)ch, ow);
		break;
	}
	memcpy(after_our_param, param_our, 22);
	our_gain = our_ctx->gain;
	our_sec_offset = (u32)((uintptr_t)our_ctx->sec - (uintptr_t)base_our);
	our_threshold = our_ctx->threshold;

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	if (ref_gain != our_gain)
		goto bad;
	if (ref_sec_offset != our_sec_offset)
		goto bad;
	if (ref_threshold != our_threshold)
		goto bad;
	if (memcmp(after_ref_param, after_our_param, 22))
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
		printf("  ctx gain ref %08x ours %08x, sec off ref %08x ours %08x, thr ref %08x ours %08x\n",
		       ref_gain, our_gain, ref_sec_offset, our_sec_offset, ref_threshold, our_threshold);
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
	run_case("par-old-neg2", CASE_PAR, 0, 0xfffffffeu, 0xfffffffdu, 10, zero);

	run_case("refresh-bound-n", CASE_REFRESH, 0, 0, 255, 256, zero);
	run_case("refresh-bound-e", CASE_REFRESH, 0, 0, 256, 256, zero);
	run_case("refresh-bound-p", CASE_REFRESH, 0, 0, 257, 256, zero);
	run_case("refresh-init-neg", CASE_REFRESH, 0, 0xffffffffu, 12345, 256, zero);

	run_case("dn-zero", CASE_DN, 0, 0, 0, 0, zero);
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
		u32 initial_gain = rnd();
		u32 gain = rnd();
		u32 threshold = rnd();

		gen_random_section(sec);
		run_case("par-random", CASE_PAR, ch, initial_gain, gain, threshold, sec);

		gen_random_section(sec);
		run_case("refresh-random", CASE_REFRESH, ch, initial_gain, gain, threshold, sec);

		gen_random_section(sec);
		run_case("dn-random", CASE_DN, ch, initial_gain, gain, threshold, sec);

		gen_random_section(sec);
		run_case("wake-random", CASE_WAKE, ch, initial_gain, 0, threshold, sec);
	}
	run_boundaries();

	printf("t32_ydnsf_oracle: random %u compared, %u mismatches\n", compared, failed);
	if (failed)
		return 1;
	return 0;
}

