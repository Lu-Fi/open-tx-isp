// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 14: our readable port t32_tmo_adr_fpga (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor function
 * tisp_tmo_adr_fpga (Matt Davis' recovered C, PR #15 @ 2e2e06bc), end to end.  All callees are the vendor refs on the vendor side and our ports on
 * ours (map_curve1, ispint_adr_16, interp1_short, subsection_up (+ subsection_map, fix_point_*), map_y_filter(_sp), local_info (+ math_exp2),
 * faceae_curve_adjust).  4 channels (context 10240 bytes, s0 800, B 1140, C 10056, canary framed): channel CH is run, the others are decoys; the
 * whole arena (vendor on VA, ours on OA, identical start) and the written tables (d88d4, e3160, e3518, e3244) are compared.  The vendor tables
 * (c0488, c04a4, d8320, d8120, e31b0, d8914, d88a4 / d88d4 / d88f4 (adjacent, d88d4 written by the y filters), d87a0, d8324, c0444, e3500, faceae tables) are
 * generated at random, the same values on both sides.  Not covered: the vendor's printk every 1000th call (dropped in the port, the oracle env only
 * counts it).  Not generated (vendor UB): division by zero (d8320 0, W == V, thr < 0 with a zero deviation, 4095 - d88d4[k] == 0).
 * After the random cases T32O_NREAL (default 1000) more cases run with the REAL vendor tables of tx_isp_t32_tmotab.h (data.c values, openimp
 * #280) on both sides: ours gets them through t32_tmo_fpga_env_tables, d88d4 starts from t32_tmo_fpga_d88d4_init; the rest stays random.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_tmo.h"
#include "tx_isp_t32_tmotab.h"

#define WEAK __attribute__((weak))
#define FR	64u
#define CAN	0xc5
#define ARN	(1u << 18)
#define CTXN	10240u

WEAK u32 t32_data_e35e0[4];
WEAK u32 t32_data_e35b0[4];
WEAK s16 t32_data_c0488[16];
WEAK s16 t32_data_c04a4[16];
WEAK int t32_data_d8320[2];
WEAK int t32_data_d8120[128];
WEAK int t32_data_e31b0[24];
WEAK s16 t32_data_d8914[24];
WEAK s16 t32_data_d87a0[136];
WEAK u16 t32_data_e3518[4];
WEAK u8 t32_data_e3500[4];
WEAK u16 t32_data_e3516[4];
WEAK u8 t32_data_ca3e0[16];
WEAK int t32_data_d8324[256];
WEAK s16 t32_data_c0444[40];
WEAK u32 t32_data_e3244;
WEAK u8 t32_data_e34e8[24];
WEAK s16 t32_data_e3248[336];
u8 t32_dblk[128] __attribute__((aligned(16)));
u32 t32_e60[12];
__asm__(".weak t32_data_d88a4\n.set t32_data_d88a4, t32_dblk\n.weak t32_data_d88d4\n.set t32_data_d88d4, t32_dblk+48\n"
	".weak t32_data_d88f0\n.set t32_data_d88f0, t32_dblk+76\n.weak t32_data_d88f4\n.set t32_data_d88f4, t32_dblk+80\n"
	".weak t32_data_e3160\n.set t32_data_e3160, t32_e60\n.weak t32_data_e3188\n.set t32_data_e3188, t32_e60+40\n");

#ifdef T32O_NB	/* sub-block oracle (t32_tmo14a..e): the first T32O_NB blocks against the vendor function cut by trunc_fpga.sh */
#define NBLK T32O_NB
#else
#define NBLK (getenv("T32O_BLK") ? atoi(getenv("T32O_BLK")) : 0)
#endif
#ifndef T32O_NAME
#define T32O_NAME "t32_tmo14_oracle"
#endif
u64 tisp_tmo_adr_fpga(u32, u32, u32, u32);
u64 tisp_math_exp2(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

static u32 rs = 0x5a3c91u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 OC[4], OS[4], OB[4], OK[4], TOT, cur, CH;
static u8 IMG[ARN];
static u8 *VA;
static u8 OA[ARN] __attribute__((aligned(16)));

/* the generated tables (the vendor symbols get a copy; ours read these) */
static s16 c0488[16], c04a4[16], d8914[24], d87a0[136], d88a4[24], d88d4[16], d88f4[16], c0444[40], e3248[336];
static int d8320, d8120[128], e31b0[24], d8324[256];
static u8 e34e8[24];
static u32 e3244, e60[12];
static u16 e3518, e3516;
static u8 e3500;

static u32 lay(u32 n)
{
	u32 i, o;

	cur = (cur + FR + 15u) & ~15u;
	o = cur;
	for (i = 0; i < n; i++)
		IMG[o + i] = (u8)(rnd() >> 9);
	cur += n;
	TOT = cur + FR;
	return o;
}
static void p16(u32 o, int v) { u16 x = (u16)v; memcpy(IMG + o, &x, 2); }
static void p32(u32 o, u32 v) { memcpy(IMG + o, &v, 4); }
static int pick16(void)
{
	switch (rb(6)) {
	case 0: return (int)rb(1100) - 60;
	case 1: return (int)(s16)rnd();
	case 2: return (int)rb(5000) - 400;
	default: return (int)rb(300);
	}
}

/* n ascending s16 values in lo .. hi (step 0 allowed when !strict) */
static void asc(s16 *d, u32 n, u32 lo, u32 hi, int strict)
{
	u32 v = lo, k, step = (hi - lo) / n + 1;

	for (k = 0; k < n; k++) {
		d[k] = (s16)v;
		v += (strict ? 1 : 0) + rb(step + 1);
		if (v > hi - (strict ? (n - 1 - k) : 0))
			v = hi - (strict ? (n - 1 - k) : 0);
	}
}

/* the real tables (tx_isp_t32_tmotab.h) into the generated ones, which settables() hands to the vendor symbols */
static void realtab(void)
{
	memcpy(c0488, t32_tmo_tab_c0488, sizeof(t32_tmo_tab_c0488));
	memcpy(c04a4, t32_tmo_tab_c04a4, sizeof(t32_tmo_tab_c04a4));
	memcpy(d8914, t32_tmo_tab_d8914, sizeof(t32_tmo_tab_d8914));
	memcpy(d87a0, t32_tmo_tab_d87a0, sizeof(t32_tmo_tab_d87a0));
	memcpy(d88a4, t32_tmo_tab_d88a4, sizeof(t32_tmo_tab_d88a4));
	memcpy(d88f4, t32_tmo_tab_d88f4, sizeof(t32_tmo_tab_d88f4));
	memcpy(d8120, t32_tmo_tab_d8120, sizeof(t32_tmo_tab_d8120));
	memcpy(d8324, t32_tmo_tab_d8324, sizeof(t32_tmo_tab_d8324));
	memcpy(c0444, t32_tmo_tab_c0444, sizeof(t32_tmo_tab_c0444));
	d8320 = T32_TMO_TAB_D8320;
	t32_tmo_fpga_d88d4_init(d88d4);
}

static void gen(void)
{
	u32 i, k, c;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (c = 0; c < 4; c++) {
		OC[c] = lay(CTXN);
		OS[c] = lay(800);
		OB[c] = lay(1140);
		OK[c] = lay(10056);
	}
	CH = rb(4);
	/* vendor tables */
	asc(c0488, 13, 0, 4095, 1);
	asc(c04a4, 9, 0, 4095, 1);
	asc(d8914, 17, 0, 4095, rb(4) != 0);
	asc(d87a0, 129, rb(3) ? 0 : rb(100), 4095, 1);
	for (k = 0; k < 24; k++)
		d88a4[k] = (s16)(rb(6) ? rb(4096) : rnd());
	asc(d88d4, 15, rb(3) ? 0 : rb(200), 4094, 0);
	d88d4[15] = (s16)rnd();
	for (k = 0; k < 14; k++)
		d88f4[k] = (s16)(rb(5) ? 1 + rb(130) : rb(2) ? 0 : rnd());
	d88f4[14] = d88f4[15] = (s16)rnd();
	d8320 = rb(4) ? (int)(1 + rb(1u << 14)) : rb(2) ? -(int)(1 + rb(1000)) : (int)(rnd() | 1u);
	for (k = 0; k < 128; k++)
		d8120[k] = rb(5) ? (int)rb(200) : (int)rnd();
	for (k = 0; k < 24; k++)
		e31b0[k] = rb(5) ? (int)rb(20001) : (int)rnd();
	for (k = 0; k < 256; k++) {
		int v;

		switch (rb(6)) {
		case 0: v = (int)rb(300) - 150; break;
		case 1: v = (int)rb(1 << 20) - (1 << 19); break;
		case 2: v = (int)rnd(); break;
		case 3: v = 0; break;
		case 4: v = -(int)rb(300); break;
		default: v = (int)rb(800); break;
		}
		d8324[k] = v;
	}
	{
		u32 v = rb(50), step = 1 + rb(rb(2) ? 200 : 40);

		for (k = 0; k < 33; k++) {
			c0444[k] = (s16)(rb(60) == 0 ? rnd() : v);
			v += rb(step + 1);
		}
	}
	e3518 = rb(2);
	e3500 = rb(2);
	e3516 = rb(3) == 0 ? (u16)(1000 * rb(5)) : (u16)rnd();
	for (k = 0; k < 24; k++)
		e34e8[k] = rb(3) ? (u8)(rb(4) == 0) : (u8)(rb(2) ? 1 : rb(256));
	for (k = 0; k < 336; k++)
		e3248[k] = (s16)(rb(4) ? rb(65536) : 0x8000 + rb(3) - 1);
	switch (rb(5)) {
	case 0: e3244 = 100000; break;
	case 1: e3244 = 99999; break;
	case 2: e3244 = 100001; break;
	case 3: e3244 = 0xffffffffu - rb(2); break;
	default: e3244 = rb(2) ? rb(300) : rnd(); break;
	}
	for (k = 0; k < 11; k++)
		e60[k] = rnd();
	/* the channels' contexts / C blocks */
	for (c = 0; c < 4; c++) {
		u32 o = OC[c], K = OK[c], mode, m100, R, V, W;
		s16 cv[129];

		mode = rb(8) < 3 ? 3 : rb(3) ? 2 : rb(4);
		p16(o + 9574, (int)mode);
		p16(o + 8314, rb(8) ? (int)rb(2000) : pick16());
		if (mode == 2 || mode == 3 || rb(5) == 0) {
			p16(o + 8316, pick16());
			p16(o + 8318, pick16());
			p16(o + 8320, pick16());
		} else {
			p16(o + 8316, (int)rb(1300));
			p16(o + 8318, (int)(1 + rb(1500)));
			p16(o + 8320, (int)(1 + rb(1500)));
		}
		for (k = 0; k < 8; k++)
			p16(o + 8820 + 2 * (3 + k), pick16());
		p16(o + 8322, rb(3) ? (int)rb(256) : pick16());
		p16(o + 8324, rb(3) ? (int)rb(256) : pick16());
		p16(o + 9270, rb(2) ? 1 : (int)rb(5));
		m100 = rb(8);
		p16(o + 100, (int)m100);
		p16(o + 9906, rb(3) ? 1 : (int)rb(4));
		p16(o + 9908, rb(3) ? (int)rb(101) : (int)rb(400) - 150);
		p16(o + 9910, (int)rb(400) - 200);
		p16(o + 9844, rb(3) ? 1 : 0);
		for (k = 0; k < 9; k++)
			p16(o + 9824 + 2 * k, rb(3) ? (int)rb(900) : pick16());
		p16(o + 9822, (int)rb(2));
		{
			s16 ax[11];

			asc(ax, 11, 0, 4095, 0);
			for (k = 0; k < 11; k++)
				p16(o + 9784 + 2 * k, ax[k]);
		}
		for (k = 0; k < 129; k++)
			cv[k] = 0;
		asc(cv, 129, rb(3) ? 0 : rb(100), 4095, 1);
		memcpy(IMG + o + 9272, cv, 258);
		p16(o + 9916, (int)rb(7));
		p16(o + 9918, rb(3) ? (int)rb(300) : pick16());
		p16(o + 9920, rb(3) ? (int)rb(300) : pick16());
		p16(o + 9922, rb(3) ? (int)rb(300) : pick16());
		p16(o + 9926, rb(3) ? 1 : (int)rb(3));
		R = rb(3) == 0 ? 9 : rb(12);
		p16(o + 9930, (int)R);
		p16(o + 9928, rb(5) ? (int)rb(700) : rb(2) ? 512 : 513);
		if (R != 9 && rb(8) == 0)
			p16(o + 9928, rb(2) ? 0 : -(int)rb(300));
		p16(o + 9638, rb(3) ? 1 : 0);
		for (k = 0; k < 14; k++)
			p16(o + 9640 + 2 * k, rb(4) ? (int)rb(300) : pick16());
		p16(o + 9934, rb(3) ? 1 : 0);
		V = rb(600);
		W = rb(600);
		if (W == V)
			W++;
		p16(o + 9936, (int)V - 40);
		p16(o + 9938, (int)W - 40);
		p16(o + 9940, rb(5) ? (int)rb(600) - 40 : pick16());
		p16(o + 9864, (int)rb(5));
		p16(o + 9862, rb(4) ? (int)rb(6) : rb(2) ? -(int)rb(100) : (int)rb(300));
		p16(o + 9860, rb(4) ? (int)rb(17) : (int)rb(24) - 4);
		for (k = 0; k < 6; k++) {
			p16(o + 8336 + 2 * k, rb(4) ? (int)rb(600) - 100 : pick16());
			p16(o + 8754 + 2 * k, rb(4) ? (int)rb(600) - 100 : pick16());
		}
		for (k = 0; k < 14; k++)
			p16(o + 8798 + 2 * k, rb(4) ? (int)rb(4200) - 100 : pick16());
		/* C: histogram cum[], weights words, columns */
		{
			u32 v = rb(50);

			for (k = 0; k < 1090 - 224; k++) {
				p32(K + 5696 + 4 * k, v);
				v += rb(1 + (rb(3) ? 4000 : 100));
			}
		}
		for (k = 0; k < 24 * 14; k++)
			p32(K + 3968 + 4 * k, rb(5) ? rb(10001) : rnd());
		for (k = 0; k < 24; k++) {
			p32(K + 9664 + 4 * k, rb(8) ? rb(4200) - 50 : rnd());
			p32(K + 9760 + 4 * k, rb(2) ? rb(5000) : rnd());
		}
		if (getenv("T32O_RR")) p16(o + 9638, atoi(getenv("T32O_RR")));
		if (getenv("T32O_TT")) p16(o + 9934, atoi(getenv("T32O_TT")));
		if (getenv("T32O_PP")) p16(o + 9926, atoi(getenv("T32O_PP")));
		(void)i;
	}
}

static void settables(void)
{
	memcpy(t32_data_c0488, c0488, 26);
	memcpy(t32_data_c04a4, c04a4, 18);
	t32_data_d8320[0] = d8320;
	memcpy(t32_data_d8120, d8120, sizeof(d8120));
	memcpy(t32_data_e31b0, e31b0, sizeof(e31b0));
	memcpy(t32_data_d8914, d8914, 34);
	memcpy(t32_data_d87a0, d87a0, 258);
	memcpy(t32_dblk, d88a4, 48);
	memcpy(t32_dblk + 48, d88d4, 32);
	memcpy(t32_dblk + 80, d88f4, 32);
	memcpy(t32_e60, e60, sizeof(e60));
	t32_data_e3518[0] = e3518;
	t32_data_e3500[0] = e3500;
	t32_data_e3516[0] = e3516;
	memcpy(t32_data_d8324, d8324, sizeof(d8324));
	memcpy(t32_data_c0444, c0444, 66);
	t32_data_e3244 = e3244;
	memcpy(t32_data_e34e8, e34e8, 24);
	memcpy(t32_data_e3248, e3248, sizeof(e3248));
}

/*
 * make the previous curves of block 5 (B + 200) close to what blocks 1 .. 4 produce (a dry run of our port on a copy of the arena and of the written tables):
 * the vendor's "deviation <= threshold" branch needs it, random previous curves never get there.
 */
static void near_prev(struct t32_tmo_fpga_env *env0)
{
	static u8 TA[ARN] __attribute__((aligned(16)));
	struct t32_tmo_fpga_env e = *env0;
	s16 d4[16];
	u32 e6[12], k, c;
	u16 e35 = e3518;
	int w[24], mn, mx;
	s16 *d = (s16 *)(TA + OS[CH] + 128);

	memcpy(TA, IMG, TOT);
	memcpy(d4, d88d4, 32);
	memcpy(e6, e60, sizeof(e6));
	e.d88d4 = d4;
	e.e3160 = e6;
	e.e3518 = &e35;
	t32_tmo_adr_fpga_blk1(TA + OS[CH], TA + OK[CH], &e);
	t32_tmo_adr_fpga_blk2(TA + OS[CH], TA + OC[CH], TA + OK[CH], &e, w, &mn, &mx);
	t32_tmo_adr_fpga_blk3(TA + OS[CH], TA + OC[CH], TA + OB[CH], TA + OK[CH], &e, mn, mx);
	t32_tmo_adr_fpga_blk4(TA + OS[CH], TA + OC[CH], TA + OK[CH], &e, w);
	c = rb(3);
	for (k = 0; k < 336; k++) {
		int v = d[k] + (c == 0 ? 0 : c == 1 ? (int)rb(5) - 2 : (int)rb(60) - 30);

		p16(OB[CH] + 200 + 2 * k, rb(40) == 0 ? (int)rnd() : v);
	}
}

static u32 vexp2(u32 v, u32 a, u32 b) { return (u32)t32o_call(FN(tisp_math_exp2), v, a, b, 0, 0); }

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 10000;
	unsigned int nr = getenv("T32O_NREAL") ? (unsigned)atoi(getenv("T32O_NREAL")) : 1000;
	unsigned int c, compared = 0, real = 0, i;
	/* #282 F3: the whole arena (ARN bytes, not only the laid out part) is initialised and compared after the last phase */

	for (c = 0; c < n + nr; c++) {
		struct t32_tmo_fpga_env env;
		u32 *rec, *q;
		const char *bad = NULL;

		t32o_reset();
		VA = t32o_alloc(ARN);
		gen();
		if (c >= n)
			realtab();
		memcpy(VA, IMG, ARN);
		memcpy(OA, IMG, ARN);
		rec = t32o_alloc(4 * 16);
		q = t32o_alloc(4 * 16);
		for (i = 0; i < 4; i++) {
			rec[i * 4] = T32O_PTR(VA + OC[i]);
			q[i * 4] = T32O_PTR(VA + OS[i]);
			q[i * 4 + 1] = T32O_PTR(VA + OB[i]);
			q[i * 4 + 2] = T32O_PTR(VA + OK[i]);
			t32_data_e35e0[i] = T32O_PTR(&rec[i * 4]);
			t32_data_e35b0[i] = T32O_PTR(&q[i * 4]);
		}
		env.c0488 = c0488; env.c04a4 = c04a4; env.d8120 = d8120; env.e31b0 = e31b0; env.d8320 = d8320;
		env.d8914 = d8914; env.d88d4 = d88d4; env.d88a4 = d88a4; env.d88f4 = d88f4; env.d87a0 = d87a0;
		env.e3160 = e60; env.e3518 = &e3518; env.e3500 = e3500; env.d8324 = d8324; env.c0444 = c0444; env.exp2 = vexp2;
		env.e3244 = &e3244; env.e34e8 = e34e8; env.e3248 = e3248;
		if (c >= n) {	/* the real tables: ours straight from the header */
			t32_tmo_fpga_env_tables(&env, d88d4);
			real++;
		}
		if (rb(2)) {
			near_prev(&env);
			memcpy(VA, IMG, ARN);
			memcpy(OA, IMG, ARN);
		}
		settables();
		t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
		t32o_call(FN(tisp_tmo_adr_fpga), CH, 0, 0, 0, 0);
		if (NBLK) {	/* truncated run (trunc_fpga.sh): the first N blocks only */
			int nb = NBLK, w[24], mn, mx;

			t32_tmo_adr_fpga_blk1(OA + OS[CH], OA + OK[CH], &env);
			if (nb > 1)
				t32_tmo_adr_fpga_blk2(OA + OS[CH], OA + OC[CH], OA + OK[CH], &env, w, &mn, &mx);
			if (nb > 2)
				t32_tmo_adr_fpga_blk3(OA + OS[CH], OA + OC[CH], OA + OB[CH], OA + OK[CH], &env, mn, mx);
			if (nb > 3)
				t32_tmo_adr_fpga_blk4(OA + OS[CH], OA + OC[CH], OA + OK[CH], &env, w);
			if (nb > 4) {
				env.e3500 = 0;
				t32_tmo_adr_fpga_blk5(OA + OS[CH], OA + OC[CH], OA + OB[CH], OA + OK[CH], &env);
			}
		} else
		t32_tmo_adr_fpga(OA + OC[CH], OA + OS[CH], OA + OB[CH], OA + OK[CH], &env);
		compared++;
		if (memcmp(VA, OA, ARN)) {
			for (i = 0; i < ARN && VA[i] == OA[i]; i++)
				;
			bad = "arena";
			if (!getenv("T32O_CONT")) { u32 o = OC[CH]; int j; printf("fm %d sel %d P %d Rr %d T %d A %d D %d E %d mode %d m100 %d e3500 %d\n", *(s16*)(OA+o+9864), *(s16*)(OA+o+9916), *(s16*)(OA+o+9926), *(s16*)(OA+o+9638), *(s16*)(OA+o+9934), *(s16*)(OA+o+9906), *(s16*)(OA+o+9844), *(s16*)(OA+o+9822), *(s16*)(OA+o+9574), *(s16*)(OA+o+100), e3500);
			  for (j = 0; j < 14; j++) { printf(" %d/%d", ((s16*)(VA+OS[CH]+128))[j], ((s16*)(OA+OS[CH]+128))[j]); } printf("\n"); }
			else { u32 o = OC[CH]; for (i = 0; i < ARN && VA[i] == OA[i]; i++) ; { int j; for (j = 0; j < 14; j++) printf(" %d/%d/%d", ((s16*)(VA+OS[CH]+128))[j], ((s16*)(OA+OS[CH]+128))[j], d88d4[1+j]); printf("\n"); } printf("e60eq %d d88d4eq %d\n", !memcmp(t32_e60, e60, sizeof(e60)), !memcmp(t32_dblk + 48, d88d4, 32)); printf("Y ctx%d s0%d B%d C%d\n", (int)i - (int)OC[CH], (int)i - (int)OS[CH], (int)i - (int)OB[CH], (int)i - (int)OK[CH]); printf("X fm %d sel %d P %d Rr %d T %d A %d D %d E %d mode %d m100 %d e3500 %d e3518 %d\n", *(s16*)(OA+o+9864), *(s16*)(OA+o+9916), *(s16*)(OA+o+9926), *(s16*)(OA+o+9638), *(s16*)(OA+o+9934), *(s16*)(OA+o+9906), *(s16*)(OA+o+9844), *(s16*)(OA+o+9822), *(s16*)(OA+o+9574), *(s16*)(OA+o+100), e3500, e3518); }
			printf("MISMATCH case %u ch %u: arena byte %u (blocks: ctx %u s0 %u B %u C %u) vendor %02x ours %02x\n", c, CH, i, OC[CH], OS[CH], OB[CH], OK[CH], VA[i], OA[i]);
		} else if (memcmp(t32_dblk + 48, d88d4, 32)) {
			bad = "d88d4";
		} else if (memcmp(t32_e60, e60, sizeof(e60))) {
			bad = "e3160";
		} else if (t32_data_e3518[0] != e3518) {
			bad = "e3518";
		} else if (t32_data_e3244 != e3244) {
			bad = "e3244";
		}
		if (bad) {
			printf("MISMATCH case %u: %s\n", c, bad);
			if (getenv("T32O_CONT")) continue;
			return 1;
		}
	}
	printf("%s: %u cases compared (%u with the real data.c tables, adr_fpga %s), 0 mismatches\n", T32O_NAME, compared, real, NBLK ? "blocks 1 .. N against the cut vendor function" : "end to end");
	return 0;
}
