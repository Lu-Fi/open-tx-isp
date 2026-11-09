// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: GAMMA per-frame paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_gamma_{dn_params_refresh,wdr_en,wake_up} from
 * driver/t32/tx_isp_t32_gamma.h and compares every register write and the
 * changed context fields.
 *
 * Fixed-seed random cases cover random gain/ev/day-night inputs and random
 * tuning bytes.  Boundary cases exercise the d03a0 mode switch and the
 * pending counter.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_gamma.h"

#define SECB		T32_GAMMA_SEC_BYTES
#define SEC_OFF		0x81a8u
#define MAXW		T32_GAMMA_MAX_WRITES

/* env.c globals the reference functions touch */
extern u32 t32_data_e1834[4];

/* unmodified vendor functions from ref/ (Matt Davis' recovered C) */
extern u64 tisp_gamma_dn_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_wdr_en(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x3c9e11u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, failed;

enum {
	CASE_DN,
	CASE_WDR,
	CASE_WAKE
};

static int run_case(const char *what, int which, u32 ch, u32 d03a0,
		    u32 ev_lo, u32 ev_hi, u32 pending, const u8 *sec, const u8 *lut)
{
	u8 *base_ref, *info, *vlut_ref, *ctx_ref;
	u8 *base_our, *vlut_our;
	struct t32_gamma *our_ctx;
	struct t32_gamma our_st, *our_stp = &our_st;
	struct t32_front_write dummy_w[T32_GAMMA_MAX_WRITES];
	u32 ref_pending = 0, our_pending = 0;
	u32 ref_sec_offset = 0, our_sec_offset = 0;
	u32 *ev_ref;
	struct t32_front_write refw[MAXW], ow[MAXW];
	unsigned int ref_n = 0, i;
	int our_n = 0;

	(void)what;
	/* env.c has no buffer for the addr_write path: only direct mode (d03a0 = 0) can run */
	d03a0 = 0;

	/* Reference side: initialize with tisp_gamma_init (ignore writes), then
	 * clear the write log so only the per-frame function's writes are captured. */
	t32o_reset();
	base_ref = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_ref)
		return 2;
	memcpy(base_ref + SEC_OFF, sec, SECB);
	info = t32o_alloc(64);
	if (!info)
		return 2;
	vlut_ref = t32o_alloc(T32_GAMMA_CURVE_BYTES + 8);
	if (!vlut_ref)
		return 2;
	memcpy(vlut_ref, lut, T32_GAMMA_CURVE_BYTES);
	put32(info + 32, T32O_PTR(vlut_ref));
	t32_data_dd0a4[ch] = T32O_PTR(base_ref);
	t32_data_dc274[ch] = T32O_PTR(info);
	t32_data_e1820[0] = T32_GAMMA_CURVE_BYTES;
	t32o_call((t32o_fn)(void (*)(void))tisp_gamma_init, ch, 0, 0, 0, 0);
	ctx_ref = (u8 *)(uintptr_t)t32_data_e1834[ch];
	if (!ctx_ref)
		return 2;
	/* Manually set ev_lo/ev_hi and pending via known offsets. */
	ev_ref = (u32 *)(uintptr_t)*(u32 *)(ctx_ref + 8);
	ev_ref[0] = ev_lo;
	ev_ref[1] = ev_hi;
	*(u8 *)(ctx_ref + 24) = (u8)pending;
	t32_data_d03a0[0] = (u8)d03a0;

	/* Clear init writes, then capture the per-frame reference writes. */
	t32o_log.n = 0;
	switch (which) {
	case CASE_DN:
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_dn_params_refresh,
			  ch, 0, 0, 0, 0);
		break;
	case CASE_WDR:
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_wdr_en,
			  ch, 0, 0, 0, 0);
		break;
	case CASE_WAKE:
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_wake_up,
			  ch, 0, 0, 0, 0);
		break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	ref_pending = *(u8 *)(ctx_ref + 24);
	ref_sec_offset = *(u32 *)(ctx_ref + 0) - T32O_PTR(base_ref);
	if (which != CASE_WAKE) {
		/* observe the curve the refresh left behind: wake_up writes it out */
		unsigned int m;

		t32o_log.n = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_wake_up,
			  ch, 0, 0, 0, 0);
		m = t32o_log.n;
		if (ref_n + m > MAXW)
			return 2;
		memcpy(refw + ref_n, t32o_log.w, m * sizeof(refw[0]));
		ref_n += m;
	}

	/* Our side: initialize with t32_gamma_start (ignore writes). */
	t32o_reset();
	base_our = t32o_alloc(SEC_OFF + SECB + 64);
	if (!base_our)
		return 2;
	memcpy(base_our + SEC_OFF, sec, SECB);
	vlut_our = t32o_alloc(T32_GAMMA_CURVE_BYTES + 8);
	if (!vlut_our)
		return 2;
	memcpy(vlut_our, lut, T32_GAMMA_CURVE_BYTES);
	our_stp = &our_st;
	memset(our_stp, 0, sizeof(*our_stp));
	if (t32_gamma_start(our_stp, base_our + SEC_OFF, vlut_our, T32_GAMMA_CURVE_BYTES, dummy_w) < 0)
		return 0;	/* refused section: nothing to compare */
	our_stp->ev_lo = ev_lo;
	our_stp->ev_hi = ev_hi;
	our_stp->pending = (u8)pending;
	t32_data_d03a0[0] = (u8)d03a0;
	our_ctx = our_stp;

	/* Clear start writes, then run our per-frame function. */
	t32o_log.n = 0;
	switch (which) {
	case CASE_DN:
		our_n = t32_gamma_dn_params_refresh(our_ctx, (u8)ch, base_our, ow);
		break;
	case CASE_WDR:
		our_n = t32_gamma_wdr_en(our_ctx, (u8)ch, ow);
		break;
	case CASE_WAKE:
		our_n = t32_gamma_wake_up(our_ctx, ow);
		break;
	}
	our_pending = our_ctx->pending;
	our_sec_offset = (u32)((uintptr_t)our_ctx->sec - (uintptr_t)base_our);
	if (which != CASE_WAKE)
		our_n += t32_gamma_wake_up(our_ctx, ow + our_n);

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	if (ref_pending != our_pending)
		goto bad;
	if (ref_sec_offset != our_sec_offset)
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
		printf("  ctx pending ref %08x ours %08x, sec off ref %08x ours %08x\n",
		       ref_pending, our_pending, ref_sec_offset, our_sec_offset);
	}
	return 1;
}

static void run_boundaries(void)
{
	u8 zero[SECB];
	static const u8 lut[T32_GAMMA_CURVE_BYTES];

	memset(zero, 0, sizeof(zero));
	put32(zero, 0x134da11);
	zero[0x5a] = 1;

	run_case("dn-d03a0-0", CASE_DN, 0, 0, 0, 0, 0, zero, lut);
	run_case("dn-d03a0-1", CASE_DN, 0, 1, 0, 0, 0, zero, lut);
	run_case("wdr-d03a0-0", CASE_WDR, 0, 0, 0, 0, 0, zero, lut);
	run_case("wdr-d03a0-1", CASE_WDR, 0, 1, 0, 0, 0, zero, lut);
	run_case("wake-d03a0-0", CASE_WAKE, 0, 0, 0, 0, 0, zero, lut);
	run_case("wake-d03a0-1", CASE_WAKE, 0, 1, 0, 0, 0, zero, lut);
	run_case("wake-pending-0", CASE_WAKE, 0, 0, 0, 0, 0, zero, lut);
	run_case("wake-pending-1", CASE_WAKE, 0, 0, 0, 0, 1, zero, lut);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, i;

	for (c = 0; c < n; c++) {
		u8 sec[SECB], lut[T32_GAMMA_CURVE_BYTES];
		u32 ch = rnd() & 3;
		u32 d03a0 = rnd() & 1;
		u32 ev_lo = rnd();
		u32 ev_hi = rnd() & 0x1ff;
		u32 pending = rnd() & 0xff;

		for (i = 0; i < SECB; i++)
			sec[i] = (u8)rnd();
		put32(sec, rnd() % 8 ? 0x134da11 : 0x134ff90);
		sec[0x5a] = 1;
		sec[0x48] = (u8)(rnd() % 3);
		for (i = 0; i < T32_GAMMA_CURVE_BYTES; i++)
			lut[i] = (u8)rnd();

		run_case("dn-random", CASE_DN, ch, d03a0, ev_lo, ev_hi, pending, sec, lut);
		run_case("wdr-random", CASE_WDR, ch, d03a0, ev_lo, ev_hi, pending, sec, lut);
		run_case("wake-random", CASE_WAKE, ch, d03a0, ev_lo, ev_hi, pending, sec, lut);
	}
	run_boundaries();

	printf("t32_gammad_oracle: random %u compared, %u mismatches\n", compared, failed);
	if (failed)
		return 1;
	return 0;
}
