// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, WDR phase 1: our readable port (driver/t32/tx_isp_t32_wdr.h)
 * against the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc): tisp_wdr_absfun, interplate, exptime_update, minFun, maxFun,
 * log2, long_tgain_update, short_tgain_update, short_ev_update, ev_interp,
 * long_ev_update, api_get_output_mode, degweight_ev.
 *
 * Both sides get the same random arena (parameter blocks of all 4 channels,
 * tables, outputs) made of canary framed regions; the whole arena and the
 * WDR globals are compared after the call (outputs, inputs unchanged, the
 * frames, the other channels' blocks as decoys), plus the return value (low
 * word, for the functions that return one).  Refused on purpose (counted):
 * interplate with x1 == x0 (vendor division by zero).  Exit 0 = all equal.
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
#define PSZ	1700u	/* parameter block bytes used by phase 1 (+1608 knots .. +1628) */

WEAK u32 t32_data_e3b40[4];
WEAK u32 t32_data_e3aa0[1], t32_data_e3aa4[1];
WEAK u32 t32_data_e3ab0[4], t32_data_e3ac0[4];
WEAK u8 t32_data_e3ad0[4];

u64 tisp_wdr_absfun(u32, u32, u32, u32);
u64 tisp_wdr_interplate(u32, u32, u32, u32, u32);
u64 tisp_wdr_exptime_update(u32, u32, u32, u32);
u64 tisp_wdr_minFun(u32, u32, u32, u32);
int32_t tisp_wdr_maxFun(int32_t, int32_t);
u64 tisp_wdr_log2(u32, u32, u32, u32);
int32_t tisp_wdr_long_tgain_update(void);
u64 tisp_wdr_short_tgain_update(u32, u32, u32, u32);
u64 tisp_wdr_short_ev_update(u32, u32, u32, u32);
u64 tisp_wdr_ev_interp(u32, u32, u32, u32);
u64 tisp_wdr_long_ev_update(u32, u32, u32, u32);
u64 tisp_wdr_api_get_output_mode(u32, u32, u32, u32);
u64 tisp_wdr_degweight_ev(u32, u32, u32, u32, u32);

#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_ABS, F_INTERP, F_EXPT, F_MIN, F_MAX, F_LOG2, F_LTG, F_STG, F_SEV, F_EVI, F_LEV, F_GOM, F_DEG, F_N };
static const char *const fname[F_N] = {
	"absfun", "interplate", "exptime_update", "minFun", "maxFun", "log2", "long_tgain_update",
	"short_tgain_update", "short_ev_update", "ev_interp", "long_ev_update", "api_get_output_mode", "degweight_ev"
};
static const int has_ret[F_N] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1 };

static u32 rs = 0x7d1e05u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rb(u32 n) { return rnd() % n; }

static u32 A[6], OFF[8], PAR[4], TOT, CH;
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
static u32 get16(u32 o, u32 i) { u16 x; memcpy(&x, IMG + o + 2 * i, 2); return x; }
static u32 get32(u32 o, u32 i) { u32 x; memcpy(&x, IMG + o + 4 * i, 4); return x; }

static int wide(void)
{
	switch (rb(6)) {
	case 0: return (int)rb(40) - 20;
	case 1: return (int)rb(2000) - 1000;
	case 2: return (int)rb(300000) - 150000;
	case 3: return (int)rnd();
	case 4: return (rnd() & 1) ? (int)0x7fffffff - (int)rb(3) : (int)0x80000000 + (int)rb(3);
	default: return (int)rb(100000) - 30000;
	}
}

/* sorted (mostly) knot axis of n entries: 16 or 32 bit */
static void axis(u32 o, u32 n, int b32)
{
	u32 v = rb(3) == 0 ? 0 : rb(b32 ? 100000 : 3000), i, mode = rb(6);

	for (i = 0; i < n; i++) {
		if (b32) put32(o, i, v); else put16(o, i, v);
		if (mode == 0) v += rb(3);
		else if (mode == 1) v = b32 ? rnd() : rb(65536);
		else v += rb(b32 ? 20000 : 6000) + 1;
		if (!b32 && v > 65535) v = 65535;
	}
}

/* exposure near one of the knots of the 16 bit axis at o (n entries) */
static u64 near16(u32 o, u32 n)
{
	if (rb(8) == 0)
		return (u64)rnd() << 32 | rnd();
	if (rb(6) == 0)
		return rb(70000);
	return (u64)(get16(o, rb(n)) + rb(5) - 2) & 0xffffffffu;
}

static void gen(int f)
{
	u32 i;

	lay_begin();
	memset(A, 0, sizeof(A));
	memset(OFF, 0, sizeof(OFF));
	for (i = 0; i < 4; i++)
		PAR[i] = lay(PSZ);
	CH = rb(2);
	switch (f) {
	case F_ABS: case F_MIN: case F_MAX:
		A[0] = (u32)wide(); A[1] = rb(6) == 0 ? A[0] + rb(3) - 1 : (u32)wide();
		break;
	case F_INTERP:
		A[0] = (u32)wide(); A[1] = rb(5) == 0 ? A[0] + 1 + rb(3) : (u32)wide();
		A[2] = (u32)wide(); A[3] = (u32)wide(); A[4] = (u32)wide();
		if (rb(3) == 0) { A[0] = rb(200); A[1] = A[0] + 1 + rb(100); A[4] = A[0] + rb(120) - 10; A[2] = rb(500); A[3] = rb(500); }
		if (rb(12) == 0) A[1] = A[0];
		if (rb(15) == 0) { A[1] = A[0] - 1; A[2] = 0; A[3] = 1; A[4] = A[0] + 0x80000001u; }
		break;
	case F_EXPT:
		OFF[0] = lay(16);
		if (rb(4) == 0) put32(OFF[0], 2, get32(OFF[0], 1) + rb(3) - 1);
		break;
	case F_LOG2:
		A[0] = rb(3) == 0 ? (u32)wide() : (1u << rb(17)) + rb(3) - 1;
		break;
	case F_STG: case F_SEV: case F_LEV: case F_EVI:
		A[0] = CH;
		A[2] = rnd(); A[3] = rb(3) == 0 ? rnd() : rb(4) == 0 ? rb(1024) : 0;
		axis(PAR[CH] + T32_WDR_P_EVX, 10, 0);
		for (i = 0; i < 4; i++) if (rb(3) == 0) put32(PAR[CH] + T32_WDR_P_EVY_LO, i, rb(4096));
		for (i = 0; i < 6; i++) if (rb(3) == 0) put32(PAR[CH] + T32_WDR_P_EVY_HI, i, rb(4096));
		if (rb(3)) {	/* an exposure (>> 10 for the update functions) near the knots */
			u64 e = near16(PAR[CH] + T32_WDR_P_EVX, 10);
			if (f != F_EVI) e = e << 10 | rb(1024);
			A[2] = (u32)e; A[3] = (u32)(e >> 32);
		}
		if (f == F_EVI) { A[1] = A[3]; A[0] = A[2]; A[2] = CH; }
		break;
	case F_GOM:
		A[0] = CH;
		OFF[0] = lay(4);
		break;
	case F_DEG:
		OFF[0] = lay(5 * 2);	/* cfg */
		OFF[1] = lay(8 * 4);	/* xs */
		OFF[2] = lay(8 * 2);	/* ys */
		OFF[3] = lay(4 * 4);	/* ev */
		axis(OFF[1], 8, 1);
		for (i = 0; i < 8; i++) put16(OFF[2], i, rb(4) == 0 ? get16(OFF[2], i) : rb(64));
		put16(OFF[0], 0, rb(4) == 0 ? get16(OFF[0], 0) : rb(3));
		put16(OFF[0], 1, rb(4) == 0 ? get16(OFF[0], 1) : rb(3));
		put16(OFF[0], 2, rb(2000) + 1000); put16(OFF[0], 3, rb(1500));
		if (rb(5) == 0) put16(OFF[0], 3, get16(OFF[0], 2));
		put16(OFF[0], 4, rb(3) == 0 ? get16(OFF[0], 4) : rb(1000));
		put32(OFF[3], 3, rb(3) == 0 ? get16(OFF[0], 4) + rb(3) - 1 : rb(1000));
		{
			u32 k = rb(8), e = get32(OFF[1], k) + rb(5) - 2;
			put32(OFF[3], 1, rb(5) == 0 ? rnd() : e);
		}
		A[3] = rb(4) == 0 ? rnd() : get16(OFF[0], 2 + rb(2)) + rb(5) - 2;
		if (rb(4) == 0) A[3] = rb(3000);
		break;
	}
}

static void vendor_globals(u8 *b)
{
	u32 i, *rec;

	for (i = 0; i < 4; i++) {
		rec = t32o_alloc(64);
		memset(rec, 0x77, 64);
		rec[3] = T32O_PTR(b + PAR[i]);
		t32_data_e3b40[i] = T32O_PTR(rec);
	}
}

static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 P0 = T32O_PTR(b + OFF[0]), P1 = T32O_PTR(b + OFF[1]), P2 = T32O_PTR(b + OFF[2]), P3 = T32O_PTR(b + OFF[3]);

	t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
	switch (f) {
	case F_ABS: return t32o_call(FN(tisp_wdr_absfun), A[0], A[1], 0, 0, 0);
	case F_INTERP: return t32o_call(FN(tisp_wdr_interplate), A[0], A[1], A[2], A[3], A[4]);
	case F_EXPT: return t32o_call(FN(tisp_wdr_exptime_update), P0, 0, 0, 0, 0);
	case F_MIN: return t32o_call(FN(tisp_wdr_minFun), A[0], A[1], 0, 0, 0);
	case F_MAX: return (u32)tisp_wdr_maxFun((int32_t)A[0], (int32_t)A[1]);
	case F_LOG2: return t32o_call(FN(tisp_wdr_log2), A[0], 0, 0, 0, 0);
	case F_LTG: return (u32)tisp_wdr_long_tgain_update();
	case F_STG: return t32o_call(FN(tisp_wdr_short_tgain_update), A[0], A[1], A[2], A[3], 0);
	case F_SEV: return t32o_call(FN(tisp_wdr_short_ev_update), A[0], A[1], A[2], A[3], 0);
	case F_EVI: return t32o_call(FN(tisp_wdr_ev_interp), A[0], A[1], A[2], 0, 0);
	case F_LEV: return t32o_call(FN(tisp_wdr_long_ev_update), A[0], A[1], A[2], A[3], 0);
	case F_GOM: return t32o_call(FN(tisp_wdr_api_get_output_mode), A[0], P0, 0, 0, 0);
	case F_DEG: return t32o_call(FN(tisp_wdr_degweight_ev), P0, P1, P2, A[3], P3);
	}
	return 0;
}

static struct t32_wdr_st ST;
static struct t32_wdr_rec REC[4];
static u64 run_ours(int f)
{
	u8 *b = OA;
	u64 e = (u64)A[3] << 32 | A[2];
	u32 i;

	for (i = 0; i < 4; i++) {
		REC[i].par = b + PAR[i];
		ST.rec[i] = &REC[i];
	}
	switch (f) {
	case F_ABS: return t32_wdr_absfun(A[0], A[1]);
	case F_INTERP: return (u32)t32_wdr_interplate((int)A[0], (int)A[1], (int)A[2], (int)A[3], (int)A[4]);
	case F_EXPT: return t32_wdr_exptime_update((u32 *)(b + OFF[0]));
	case F_MIN: return (u32)t32_wdr_minfun((int)A[0], (int)A[1]);
	case F_MAX: return (u32)t32_wdr_maxfun((int)A[0], (int)A[1]);
	case F_LOG2: return t32_wdr_log2((int)A[0]);
	case F_LTG: return (u32)t32_wdr_long_tgain_update();
	case F_STG: return (u32)t32_wdr_short_tgain_update(&ST, e);
	case F_SEV: return (u32)t32_wdr_short_ev_update(&ST, A[0], e);
	case F_EVI: t32_wdr_ev_interp(&ST, (u64)A[1] << 32 | A[0], A[2]); return 0;
	case F_LEV: return (u32)t32_wdr_long_ev_update(&ST, A[0], e);
	case F_GOM: return (u32)t32_wdr_api_get_output_mode(&ST, A[0], (u32 *)(b + OFF[0]));
	case F_DEG:
		return t32_wdr_degweight_ev((const u16 *)(b + OFF[0]), (const u32 *)(b + OFF[1]),
					    (const u16 *)(b + OFF[2]), A[3], (const u32 *)(b + OFF[3]));
	}
	return 0;
}

static void seed_globals(void)
{
	u32 i;

	ST.short_tgain = (u64)rnd() << 32 | rnd();
	for (i = 0; i < 2; i++) {
		ST.short_ev[i] = (u64)rnd() << 32 | rnd();
		ST.long_ev[i] = (u64)rnd() << 32 | rnd();
	}
	for (i = 0; i < 4; i++)
		ST.ev_changed[i] = (u8)rnd();
	t32_data_e3aa0[0] = (u32)ST.short_tgain; t32_data_e3aa4[0] = (u32)(ST.short_tgain >> 32);
	for (i = 0; i < 2; i++) {
		t32_data_e3ab0[2 * i] = (u32)ST.short_ev[i]; t32_data_e3ab0[2 * i + 1] = (u32)(ST.short_ev[i] >> 32);
		t32_data_e3ac0[2 * i] = (u32)ST.long_ev[i]; t32_data_e3ac0[2 * i + 1] = (u32)(ST.long_ev[i] >> 32);
	}
	memcpy(t32_data_e3ad0, ST.ev_changed, 4);
}

static int globals_differ(void)
{
	u32 i;

	if (t32_data_e3aa0[0] != (u32)ST.short_tgain || t32_data_e3aa4[0] != (u32)(ST.short_tgain >> 32))
		return 1;
	for (i = 0; i < 2; i++)
		if (t32_data_e3ab0[2 * i] != (u32)ST.short_ev[i] || t32_data_e3ab0[2 * i + 1] != (u32)(ST.short_ev[i] >> 32) ||
		    t32_data_e3ac0[2 * i] != (u32)ST.long_ev[i] || t32_data_e3ac0[2 * i + 1] != (u32)(ST.long_ev[i] >> 32))
			return 1;
	return memcmp(t32_data_e3ad0, ST.ev_changed, 4) != 0;
}

int main(void)
{
	static u32 hit[F_N];
	u32 n, cases = 0, refused = 0, bad = 0;
	int f;

	for (n = 0; n < 60000; n++) {
		u64 rv, ro;

		f = (int)(n % F_N);
		gen(f);
		if (f == F_INTERP && A[1] == A[0]) {
			refused++;
			continue;
		}
		t32o_reset();
		VA = t32o_alloc(ARN);
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		vendor_globals(VA);
		seed_globals();
		rv = run_vendor(f);
		ro = run_ours(f);
		cases++;
		hit[f]++;
		if ((has_ret[f] && (u32)rv != (u32)ro) || memcmp(VA, OA, TOT) || globals_differ() || t32o_log.n) {
			if (bad++ < 5)
				fprintf(stderr, "t32_wdr1_oracle: %s case %u differs (ret %08x / %08x)\n", fname[f], n,
					(u32)rv, (u32)ro);
		}
	}
	for (f = 0; f < F_N; f++)
		if (!hit[f])
			bad++;
	printf("t32_wdr1_oracle: %u cases, %u refused (interplate x1 == x0), %u mismatches\n", cases, refused, bad);
	return bad ? 1 : 0;
}
