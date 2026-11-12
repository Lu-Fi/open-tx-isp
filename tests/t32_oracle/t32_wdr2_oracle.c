// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, WDR phase 2: our readable port (driver/t32/tx_isp_t32_wdr.h)
 * against the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc): tisp_wdr_getVar, get_lum_s, quicksort, paixu.
 *
 * Both sides get the same random arena (parameter blocks of all 4 channels,
 * tables, outputs) made of canary framed regions; the whole arena and the
 * WDR globals are compared after the call (outputs, inputs unchanged, the
 * frames, the other channels' blocks as decoys), plus the return value (low
 * word, for the functions that return one).   Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_wdr.h"

#define WEAK __attribute__((weak))
#define FR	64u
#define CAN	0xc5
#define ARN	(1u << 15)

u64 tisp_wdr_getVar(u32, u32, u32, u32);
u64 tisp_wdr_get_lum_s(u32, u32, u32, u32, u32);
u64 tisp_wdr_quicksort(u32, u32, u32, u32);
u64 tisp_wdr_paixu(u32, u32, u32, u32);

#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_VAR, F_LUM, F_QS, F_PX, F_N };
static const char *const fname[F_N] = { "getVar", "get_lum_s", "quicksort", "paixu" };
static const int has_ret[F_N] = { 1, 1, 0, 0 };

static u32 rs = 0x2b9e11u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rb(u32 n) { return rnd() % n; }

static u32 A[6], OFF[8], TOT;
static u8 IMG[ARN], OA[ARN] __attribute__((aligned(16)));
static u8 *VA;
static u32 cur;

static void lay_begin(void) { cur = 0; memset(IMG, CAN, sizeof(IMG)); }
static u32 lay(u32 n)
{
	u32 i, o;

	cur = (cur + FR + 7u) & ~7u;
	o = cur;
	for (i = 0; i < n; i++)
		IMG[o + i] = (u8)(rnd() >> 9);
	cur += n;
	TOT = cur + FR;
	return o;
}
static void put16(u32 o, u32 i, u32 v) { u16 x = (u16)v; memcpy(IMG + o + 2 * i, &x, 2); }
static void put32(u32 o, u32 i, u32 v) { memcpy(IMG + o + 4 * i, &v, 4); }

static u32 val16(u32 mode)
{
	switch (mode) {
	case 0: return rb(256);
	case 1: return 190 + rb(20);
	case 2: return rb(8);
	default: return rb(65536);
	}
}

static void gen(int f)
{
	u32 i, m = rb(4), n;

	lay_begin();
	memset(A, 0, sizeof(A));
	switch (f) {
	case F_VAR:
		OFF[0] = lay(257 * 2);
		for (i = 0; i < 257; i++)
			put16(OFF[0], i, rb(8) == 0 ? val16(rb(4)) : val16(m));
		if (rb(6) == 0) for (i = 0; i < 257; i++) put16(OFF[0], i, i < 128 ? 50 + i : 300);
		break;
	case F_LUM:
		OFF[0] = lay(160 * 4);
		OFF[1] = lay(3 * 2);
		OFF[2] = lay(4 * 2);
		OFF[3] = lay(3);
		OFF[4] = lay(160 * 2);
		for (i = 0; i < 160; i++)
			put32(OFF[0], i, m == 0 ? rnd() : m == 1 ? rb(50) : m == 2 ? (rb(10) ? 0 : rb(3000)) : rb(700));
		for (i = 0; i < 3; i++)
			put16(OFF[1], i, rb(5) == 0 ? rb(65536) : rb(4) == 0 ? 0 : rb(m == 1 ? 9000 : 30000));
		if (rb(4) == 0) put16(OFF[1], 1, 0);
		if (rb(4) == 0) put16(OFF[1], 2, 65535);
		for (i = 0; i < 3; i++)
			IMG[OFF[3] + i] = rb(4) == 0 ? (u8)rnd() : (u8)rb(10);
		break;
	case F_QS:
		n = rb(5) == 0 ? rb(300) : rb(40);
		OFF[0] = lay((n + 4) * 2);
		for (i = 0; i < n + 4; i++)
			put16(OFF[0], i, m == 0 ? rnd() : m == 1 ? rb(4) : m == 2 ? i * 3 : 60000 - i);
		A[1] = rb(3);
		A[2] = rb(8) == 0 ? A[1] - 1 - rb(2) : (rb(6) == 0 ? A[1] : n + 1);
		break;
	case F_PX:
		OFF[0] = lay(225 * 2);
		for (i = 0; i < 225; i++)
			put16(OFF[0], i, m == 0 ? rnd() : m == 1 ? rb(4) : m == 2 ? i : 300 - i);
		A[1] = rb(4) == 0 ? rnd() : rb(6) == 0 ? rb(256) : rb(20);
		A[2] = rb(3) == 0 ? rnd() : rb(2);
		break;
	}
}

static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 P0 = T32O_PTR(b + OFF[0]), P1 = T32O_PTR(b + OFF[1]), P2 = T32O_PTR(b + OFF[2]), P3 = T32O_PTR(b + OFF[3]);

	t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
	switch (f) {
	case F_VAR: return t32o_call(FN(tisp_wdr_getVar), P0, 0, 0, 0, 0);
	case F_LUM: return t32o_call(FN(tisp_wdr_get_lum_s), P0, P1, P2, P3, T32O_PTR(b + OFF[4]));
	case F_QS: return t32o_call(FN(tisp_wdr_quicksort), P0, A[1], A[2], 0, 0);
	case F_PX: return t32o_call(FN(tisp_wdr_paixu), P0, A[1], A[2], 0, 0);
	}
	return 0;
}

static int cur_f;
static u64 run_ours_(int f)
{
	u8 *b = OA;

	switch (f) {
	case F_VAR: return t32_wdr_getvar((const u16 *)(b + OFF[0]));
	case F_LUM:
		return (u32)t32_wdr_get_lum_s((const u32 *)(b + OFF[0]), (const u16 *)(b + OFF[1]), (u16 *)(b + OFF[2]),
					      b + OFF[3], (const u16 *)(b + OFF[4]));
	case F_QS: t32_wdr_quicksort((u16 *)(b + OFF[0]), (int)A[1], (int)A[2]); return 0;
	case F_PX: t32_wdr_paixu((u16 *)(b + OFF[0]), A[1], A[2]); return 0;
	}
	return 0;
}

static u64 ours_ret;
static void run_ours_g(void *p) { (void)p; ours_ret = run_ours_(cur_f); }
/* our sort / loops run under the runaway guard: a mutant that no longer ends dies here by mismatch */
static u64 run_ours(int f)
{
	cur_f = f;
	if (t32o_guard(run_ours_g, 0)) {
		fprintf(stderr, "t32_wdr2_oracle: %s does not return (runaway loop)\n", fname[f]);
		printf("t32_wdr2_oracle: MISMATCH, runaway loop in %s\n", fname[f]);
		exit(1);
	}
	return ours_ret;
}

int main(void)
{
	static u32 hit[F_N];
	u32 n, cases = 0, bad = 0;
	int f;

	for (n = 0; n < 40000; n++) {
		u64 rv, ro;

		f = (int)(n % F_N);
		gen(f);
		t32o_reset();
		VA = t32o_alloc(ARN);
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		rv = run_vendor(f);
		ro = run_ours(f);
		cases++;
		hit[f]++;
		if ((has_ret[f] && (u32)rv != (u32)ro) || memcmp(VA, OA, TOT) || t32o_log.n) {
			if (bad++ < 5)
				fprintf(stderr, "t32_wdr2_oracle: %s case %u differs (ret %08x / %08x)\n", fname[f], n,
					(u32)rv, (u32)ro);
		}
	}
	for (f = 0; f < F_N; f++)
		if (!hit[f])
			bad++;
	printf("t32_wdr2_oracle: %u cases, %u mismatches\n", cases, bad);
	return bad ? 1 : 0;
}
