// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_ae_tune (Matt Davis' recovered C, PR #15 @ 2e2e06bc,
 * run unmodified) against t32_ae_tune of driver/t32/tx_isp_t32_aefu.h.  Both
 * sides start from the same random state: tune T, buffer B, state R, the
 * weights p and the pair w, the channel's list flag (dc284) and a total.  The
 * generator steers the many branches (history filter or copy, first call,
 * window of the stored target, target from the table / R[1296], up / down,
 * the three step formulas and the clamp against D1 +- G, the history average,
 * list or plain allocation, the load result) with values derived from the
 * same vendor maths (a probe of the vendor divisions).  The callees
 * tisp_ae_long_target, tisp_ae_calc_convergence_speed, tisp_ae_ev_alloc_calc
 * and tisp_ae_ev_list_alloc_calc run as vendor code on the reference side
 * and as our ports (t32_ae_long_target, t32_ae_calc_convergence_speed,
 * t32_aev_ev_alloc_calc, t32_aev_list_alloc_calc) on ours; fix_point_* is the
 * same vendor code on both sides (hooks).  The bilinear interpolation, the
 * frame rate stubs are logging mocks shared by both sides.  Compared: the
 * whole arena, e1510, the return value (result, B pointer) and the logs.
 * Not covered: fix_point_*, the bilinear interpolation, a history length nR
 * outside 1..20 or n1 beyond 255 (undefined in the vendor), a channel > 1
 * (dc284 holds two).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_aefu.h"

#define WEAKD __attribute__((weak))
WEAKD u32 t32_data_e1588[4], t32_data_e15a8[4];
WEAKD u8 t32_data_e1510[4];
extern u8 t32_data_dc284[];
/* the vendor's selector jump tables (.rodata + 0x1f20 / 0x1f3c) */
u32 t32_data_bf340[8] = { 0x2bb78, 0x2bcd4, 0x2be10, 0x2bf0c, 0x2c034, 0x2c128, 0x2c254, 0 };
u32 t32_data_bf35c[8] = { 0x2cb60, 0x2cd50, 0x2cf7c, 0x2d154, 0x2d37c, 0x2d558, 0x2d768, 0x2e094 };

u64 tisp_ae_tune(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_div_32(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult2_32(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult3_32(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_div_64(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult2_64(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult3_64(u32 a0, u32 a1, u32 a2, u32 a3);

#define CALLF(f, a, b, c, d, e) t32o_call((t32o_fn)(void (*)(void))(f), a, b, c, d, e)

#define OFF_ST		0x0000u
#define OFF_TUNE	0x1000u
#define OFF_BUF		0x2000u
#define OFF_CTX		0x5400u
#define OFF_PW		0x5500u		/* p (2 words), w (2 words) */
#define ARENA		0x5600u

static u32 rs = 0x3c5e91u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rsh(void) { return rnd() >> (rnd() % 28); }
static void put32(u8 *p, u32 v) { memcpy(p, &v, 4); }
static void put16(u8 *p, u32 v) { u16 x = (u16)v; memcpy(p, &x, 2); }
static void put64(u8 *q, t32_u64 v) { put32(q, (u32)v); put32(q + 4, (u32)(v >> 32)); }

static u32 h_div32(u32 sh, u32 a, u32 b) { return (u32)CALLF(fix_point_div_32, sh, a, b, 0, 0); }
static u32 h_mult2_32(u32 sh, u32 a, u32 b) { return (u32)CALLF(fix_point_mult2_32, sh, a, b, 0, 0); }
static u32 h_mult3_32(u32 sh, u32 a, u32 b, u32 c) { return (u32)CALLF(fix_point_mult3_32, sh, a, b, c, 0); }
static t32_u64 h_div64(u32 sh, t32_u64 a, t32_u64 b)
{
	u64 r;

	t32o_arg5 = (u32)(b >> 32);
	r = CALLF(fix_point_div_64, sh, 0, (u32)a, (u32)(a >> 32), (u32)b);
	t32o_arg5 = 0;
	return r;
}
static t32_u64 h_mult2_64(u32 sh, t32_u64 a, t32_u64 b)
{
	u64 r;

	t32o_arg5 = (u32)(b >> 32);
	r = CALLF(fix_point_mult2_64, sh, 0, (u32)a, (u32)(a >> 32), (u32)b);
	t32o_arg5 = 0;
	return r;
}
static t32_u64 h_mult3_64(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c)
{
	u64 r;

	t32o_arg5 = (u32)(b >> 32);
	t32o_arg6 = (u32)c;
	t32o_arg7 = (u32)(c >> 32);
	r = CALLF(fix_point_mult3_64, sh, 0, (u32)a, (u32)(a >> 32), (u32)b);
	t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
	return r;
}

/* mocks shared by both sides, logging into t32o_evlog */
static u32 g_mret;
static void evpush(u32 tag, const u32 *v, unsigned int n)
{
	if (t32o_ev_n < T32O_EVLOG) {
		unsigned int i;

		t32o_evlog[t32o_ev_n][0] = tag;
		for (i = 0; i < n; i++)
			t32o_evlog[t32o_ev_n][1 + i] = v[i];
	}
	t32o_ev_n++;
}
u64 tisp_set_ae_sensor_fps(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[1] = { a0 };

	(void)a1; (void)a2; (void)a3;
	evpush(1, v, 1);
	return 0;
}
u64 tisp_ae_max_exp_calc_fps(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[2] = { a0, a1 };

	(void)a2; (void)a3;
	evpush(2, v, 2);
	return g_mret;
}
static u64 bil_core(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7)
{
	u32 v[8] = { a0, a1, a2, a3, a4, a5, a6, a7 }, h = 0x9e3779b9u, i;

	evpush(3, v, 8);
	for (i = 0; i < 8; i++) {
		h = (h ^ v[i]) * 0x01000193u + i;
		h ^= h >> 15;
	}
	return (u64)(h * 2654435761u) << 32 | (h % 900);
}
u64 tisp_ae_lib_bilinear_intp(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7)
{
	return bil_core(a0, a1, a2, a3, a4, a5, a6, a7);
}
static t32_u64 h_bil(t32_u64 x, t32_u64 e, t32_u64 en, u32 y0, u32 y1)
{
	return bil_core((u32)x, (u32)(x >> 32), (u32)e, (u32)(e >> 32), (u32)en, (u32)(en >> 32), y0, y1);
}
static void h_set_fps(u32 ch) { CALLF(tisp_set_ae_sensor_fps, ch, 0, 0, 0, 0); }
static u32 h_max_exp(u32 ch, u32 ev) { return (u32)CALLF(tisp_ae_max_exp_calc_fps, ch, ev, 0, 0, 0); }

static const struct t32_ae_ops AEOPS = { h_div32, h_mult2_32, h_mult3_32, NULL, h_bil };
static const struct t32_aev_ops EVOPS = { h_div32, h_mult2_32, h_div64, h_mult2_64, h_mult3_64, h_set_fps, h_max_exp };
static const struct t32_aeu_ops OPS = { &AEOPS, &EVOPS };

/* probe ops: the same maths, remembering whether a mult2_64 result equals pr_one */
static t32_u64 pr_one;
static int pr_hit;
static t32_u64 p_mult2_64(u32 sh, t32_u64 a, t32_u64 b)
{
	t32_u64 r = h_mult2_64(sh, a, b);

	if (r == pr_one)
		pr_hit = 1;
	return r;
}
static const struct t32_aev_ops PEVOPS = { h_div32, h_mult2_32, h_div64, p_mult2_64, h_mult3_64, h_set_fps, h_max_exp };
static const struct t32_aeu_ops POPS = { &AEOPS, &PEVOPS };

struct par { u32 ch, total, dcw, junk; };
static u8 img[ARENA];
static u8 *base_a;
static unsigned int nres[3];

static u32 ld(u32 off) { u32 v; memcpy(&v, img + off, 4); return v; }
static u32 sc(u32 sh) { return (rnd() % ((rnd() % 3 + 1) << (sh & 15) | 1)) & 0xffff; }
static u32 near(u32 v)
{
	switch (rnd() % 6) {
	case 0: return v;
	case 1: return v + 1;
	case 2: return v ? v - 1 : 0;
	case 3: return v + rnd() % 30;
	case 4: return v > 30 ? v - rnd() % 30 : v;
	default: return rnd() % 4 ? rsh() % 4000 : rnd();
	}
}

static void gen(struct par *p)
{
	u32 i, sh, sx, cur, tot, q, k, m, n0, n1, sum1 = 0, sum2 = 0, nR, dens, flag8566, flag8568;
	u32 T = OFF_TUNE, B = OFF_BUF, R = OFF_ST;
	unsigned int sel = rnd() % 4;
	t32_u64 w, base, step;

	for (i = 0; i < ARENA; i++)
		img[i] = sel ? (u8)rnd() : (u8)(rnd() & 7);
	sh = rnd() % 5 ? 4 + rnd() % 10 : rnd() % 17;
	if (rnd() % 5 == 0)
		sh = 1 + rnd() % 3;
	if (rnd() % 30 == 0)
		sh = 31;
	if (rnd() % 60 == 0)
		sh = 32 + rnd() % 40;
	sx = sh & 31;
	p->ch = rnd() & 1;
	p->junk = rnd() % 4 ? 0 : rnd() & 0xffffff00u;
	p->dcw = rnd() % 3 ? 1 : rnd() % 3;
	put16(img + T + 1736, sh);
	/* ev state for the allocations (as in t32_aev3_oracle) */
	put32(img + R + 608, rnd() % 4 ? 1 + rnd() % 60 : rnd() % 3);
	put32(img + R + 612, (1U << sx) + rnd() % ((5U << sx) | 1));
	put32(img + R + 620, rnd() % 6 ? (1U << sx) + rnd() % ((5U << sx) | 1) : rsh());
	put32(img + R + 624, rnd() % 3 ? 1 + rnd() % 400 : rsh());
	put32(img + R + 628, rnd() % 3 ? (1U << sx) + rnd() % ((8U << sx) | 1) : rsh());
	put32(img + R + 636, rnd() % 3 ? (1U << sx) + rnd() % ((8U << sx) | 1) : rsh());
	m = rnd() % 10;
	img[R + 1276] = m & 1;
	img[R + 1277] = (m >> 1) & 1;
	img[R + 1279] = (m >> 2) & 1;
	img[R + 1284] = rnd() % 8 ? 0 : 1;
	put16(img + T + 2920, rnd() % 6 ? 1 : rnd() % 3);
	put16(img + T + 2922, rnd() % 4 ? 1 + rnd() % 2 : rnd() % 5);
	put16(img + T + 2924, rnd() % 4 ? rnd() % 200 : rnd());
	img[B + 12842] = 1 + rnd() % (rnd() & 1 ? 6 : 40);
	put32(img + T + 1620, rnd() % 8);
	put32(img + R + 1184, rnd() % 4 ? rnd() % 8 : rnd());
	put32(img + R + 1264, 1 + rnd() % 60);
	put32(img + R + 1260, near(ld(R + 1264)));
	put32(img + B + 8684, near(100));
	put32(img + B + 8688, near(100));
	put32(img + B + 8692, near(100));
	g_mret = rnd() % 4 ? 100 + rnd() % 3 - 1 : rnd() % 200;
	put16(img + B + 8576, rnd() % 17);
	put32(img + R + 1700, rnd() % 4 ? 10 + rnd() % 300 : rsh());
	for (i = 1; i < 64; i++)
		put32(img + R + 1700 + 4 * i, ld(R + 1700 + 4 * (i - 1)) + rnd() % 30);
	/* tune: step, ratios, thresholds */
	put16(img + T + 2920, rnd() % 5 ? 1 : rnd() % 3);	/* k256 (the allocation modes share it, as in the vendor) */
	put16(img + T + 2852, rnd() % 3 ? sc(sh) : rnd());
	put16(img + T + 2854, rnd() % 3 ? sc(sh) : rnd());
	put16(img + T + 2856, rnd() % 4 ? sc(sh) : rnd());
	put16(img + T + 2858, rnd() % 4 ? sc(sh) : rnd());
	put16(img + T + 2860, rnd() % 4 ? sc(sh) : rnd() % 3 ? 0 : rnd());
	put16(img + T + 2866, rnd() % 50);
	put16(img + T + 2868, rnd() % 50);
	if (rnd() % 10 == 0)
		put16(img + T + 2866, rnd());
	put32(img + T + 1644, rnd());
	put32(img + T + 1648, rnd());
	put32(img + T + 1740, rnd() % 3 ? 1 : rnd() % 3);
	put16(img + T + 2956, rnd() % 4 ? 1 : rnd() % 3);
	put16(img + T + 2958, rnd() % 3 ? 100 + rnd() % 60 : rnd());
	put16(img + T + 2960, rnd() % 3 ? 100 + rnd() % 200 : rnd());
	for (i = 0; i < 15; i++) {
		put16(img + T + 2972 + 2 * i, rnd() % 10 == 0 ? 0 : rnd() % 3 ? 100 + rnd() % 60 : rnd());
		if (rnd() % 12 == 0)
			put16(img + T + 2870 + 2 * i, 0);
		else
			put16(img + T + 2870 + 2 * i, rnd() % 4 ? rnd() % 1000 : rnd());
		if (rnd() % 10 == 0)
			put64(img + T + 1496 + 8 * i, 0);
	}
	/* the history table / long_target scale */
	w = rnd() % 3 ? ((t32_u64)rsh() << (rnd() % 20)) : rnd() % 64;
	if (rnd() % 8 == 0)
		w |= (t32_u64)rsh() << 32;
	put64(img + T + 368, w);
	base = ((w << 10) >> (sh & 63)) * (1 + rnd() % 3) / (1 + rnd() % 3) + 15 + (rnd() % 4 ? 0 : rnd());
	step = base / 15;
	for (i = 0; i < 15; i++)
		if (rnd() % 10)
			put64(img + T + 1496 + 8 * i, base - i * step);
	/* B state */
	put32(img + B + 7616, rnd() % 3 ? rsh() : rnd() % 100);
	put32(img + B + 7620, rsh());
	put32(img + B + 7628, rsh());
	for (i = 0; i < 256; i++)
		put32(img + B + 4 * i, rnd() % 4 ? rnd() % 64 : rsh());
	flag8566 = rnd() % 4 == 0 ? 0 : rnd() % 8 ? 1 : rnd();
	put16(img + B + 8566, flag8566);
	flag8568 = rnd() % 2 ? 0 : rnd() % 3 ? 1 : rnd() % 4;
	put16(img + B + 8568, flag8568);
	cur = rnd() % 3 ? 5 + rnd() % 300 : rnd() & 0xffff;
	put16(img + B + 8570, cur);
	put16(img + B + 8574, rnd() % 3);
	img[R + 1285] = rnd() % 3 ? 0 : rnd() % 2 ? 1 : rnd();
	img[R + 1297] = rnd() % 3 == 0 ? 0 : rnd() % 4 ? 1 : rnd();
	img[R + 1296] = rnd() % 3 == 0 ? rnd() % 14 : rnd() % 3 ? 10 + rnd() % 191 : rnd() % 2 ? 200 + rnd() % 3 : rnd();
	/* total near the stored target (window of the first test), sometimes 0 */
	tot = rnd() % 4 ? cur + (u32)((int)(rnd() % 120) - 60) : rnd() % 4 ? rsh() % 2000 : rnd() % 3 ? 0 : rnd();
	if (rnd() % 4 == 0)
		tot = cur + (rnd() % 2 ? ld(T + 2868) : (u32)-(int)ld(T + 2866)) + (u32)((int)(rnd() % 3) - 1);
	if (rnd() % 3 == 0 && !(tot & 0xffff))
		tot = rnd() % 5 ? tot | 1 : tot;
	if (img[R + 1297] && rnd() % 6 == 0) {	/* total equal to the clamped R[1296] target */
		u32 v = img[R + 1296];

		tot = v < 10 ? 10 : v < 201 ? v : 200;
	}
	p->total = tot;
	/* window of the stored target: c2862 > q > c2864 hit or miss at both edges */
	q = h_div32(sh, (cur & 0xffff) << sx, (tot ? tot : 1) << sx);
	if (!flag8566)
		q = 0;
	put16(img + T + 2862, rnd() % 6 ? q + 1 + rnd() % 3 : rnd() % 2 ? q : rnd() % 2 ? q - 1 : rnd());
	put16(img + T + 2864, rnd() % 6 ? q - 1 - rnd() % 3 : rnd() % 2 ? q : rnd() % 2 ? q + 1 : rnd());
	/* R history */
	nR = 1 + rnd() % 20;
	if (rnd() % 30 == 0)
		nR = 1;
	put32(img + R + 1540, nR);
	dens = rnd() % 9;
	for (i = 0; i < 30; i++)
		put32(img + R + 1380 + 4 * i, rnd() % 8 < dens ? 1 : rnd() % 20 == 0 ? rnd() % 5 : 0);
	for (i = 0; i < nR; i++)
		put32(img + R + 1300 + 4 * i, rnd() % 3 ? rnd() % 100 : rsh());
	/* tail: counts, sums, thresholds, the pair w, the constants */
	n0 = rnd() % 3 ? rnd() % 256 : rnd() % 4 ? 250 + rnd() % 10 : rnd() % 4;
	n1 = rnd() % 4 ? rnd() % 40 : rnd() % 200;
	put32(img + T + 1688, n0);
	put32(img + T + 1696, n1);
	for (k = 0; k < n1; k++)
		sum1 += ld(B + 4 * k);
	for (k = n0 + 1; k < 256; k++)
		sum2 += ld(B + 4 * k);
	put32(img + T + 1700, rnd() % 6 ? sum1 + (u32)((int)(rnd() % 3) - 1) : rnd() % 2 ? 0 : rnd());
	put32(img + T + 1692, rnd() % 6 ? sum2 + (u32)((int)(rnd() % 3) - 1) : rnd() % 2 ? 0 : rnd());
	put32(img + OFF_PW, rnd() % 3 ? rnd() % 2000 : rsh());
	put32(img + OFF_PW + 4, rnd() % 3 ? rnd() % 2000 : rsh());
	put32(img + OFF_PW + 8, rnd() % 3 ? 36 + rnd() % 12 : rnd() % 100);
	put32(img + OFF_PW + 12, rnd() % 3 ? 36 + rnd() % 12 : rnd() % 100);
	for (i = 0; i < 4; i++) {
		static const u32 o[4] = { 2932, 2934, 2936, 2938 };

		put16(img + T + o[i], rnd() % 4 ? (1U << (sx & 15)) + rnd() % 5 - 2 : rnd() % 3 ? sc(sh) : rnd());
	}
	if (rnd() % 8 == 0)
		put32(img + OFF_PW + 8, 40 + rnd() % 3);
	if (rnd() % 8 == 0)
		put32(img + OFF_PW + 12, 40 + rnd() % 3);
	/* the step with e == one (!up, formula e): search h198 / total on the vendor maths with a probe run */
	if (sh >= 1 && sh <= 3 && rnd() % 2) {
		static u8 sv[ARENA];
		u8 d;
		u32 t;

		put16(img + B + 8566, 1);
		put16(img + B + 8568, 0);
		img[R + 1285] = 1;
		img[R + 1297] = 1;
		put16(img + T + 2860, 1);
		for (t = 0; t < 60 && !pr_hit; t++) {
			img[R + 1296] = 10 + rnd() % 12;
			p->total = img[R + 1296] + 1 + rnd() % 80;
			put16(img + T + 2852, rnd() % 40);
			put16(img + T + 2854, rnd() % 40);
			memcpy(sv, img, ARENA);
			pr_hit = 0;
			pr_one = (t32_u64)1 << (2 * sh);
			t32_ae_tune(sv + T, sv + B, sv + R, &d, p->dcw, p->ch, p->total, sv + OFF_PW, sv + OFF_PW + 8, &POPS);
		}
		pr_hit = 0;
	}
}

struct snap {
	u8 mem[ARENA];
	u8 e1510[4];
	u32 ev_n, evlog[T32O_EVLOG][9];
	unsigned int printfs, nlog;
	u64 ret;
};
static struct snap sref, sour;
static unsigned int ncmp;

static void run_side(int side, const struct par *p, struct snap *s)
{
	u8 *A;
	u32 ch = p->ch;

	t32o_reset();
	t32o_ev_n = 0;
	memset(t32o_evlog, 0, sizeof(t32o_evlog));
	A = t32o_alloc(ARENA);
	if (!side)
		base_a = A;
	else if (A != base_a) {
		fprintf(stderr, "arena address differs between runs\n");
		exit(2);
	}
	memcpy(A, img, ARENA);
	put32(A + OFF_CTX + 0, T32O_PTR(A + OFF_TUNE));
	put32(A + OFF_CTX + 4, T32O_PTR(A + OFF_BUF));
	t32_data_e1588[ch] = T32O_PTR(A + OFF_ST);
	t32_data_e15a8[ch] = T32O_PTR(A + OFF_CTX);
	memset(t32_data_e1510, 0, 4);
	memset(t32_data_dc284, 0, 1808);
	put32(t32_data_dc284 + ch * 904 + 900, p->dcw);
	if (!side)
		s->ret = CALLF(tisp_ae_tune, ch | p->junk, p->total, T32O_PTR(A + OFF_PW), T32O_PTR(A + OFF_PW + 8), 0);
	else
		s->ret = (u64)T32O_PTR(A + OFF_BUF) << 32 |
			 t32_ae_tune(A + OFF_TUNE, A + OFF_BUF, A + OFF_ST, &t32_data_e1510[ch], p->dcw, ch, p->total,
				     A + OFF_PW, A + OFF_PW + 8, &OPS);
	memcpy(s->mem, A, ARENA);
	memcpy(s->e1510, t32_data_e1510, 4);
	s->ev_n = t32o_ev_n;
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->printfs = t32o_log.printfs;
	s->nlog = t32o_log.n;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000, c, i;

	for (c = 0; c < n; c++) {
		struct par p;

		gen(&p);
		run_side(0, &p, &sref);
		run_side(1, &p, &sour);
		ncmp++;
		nres[(u32)sref.ret > 2 ? 0 : (u32)sref.ret]++;
		for (i = 0; i < ARENA; i++)
			if (sref.mem[i] != sour.mem[i]) {
				printf("MISMATCH case %u: arena +%x ref %02x ours %02x\n", c, i, sref.mem[i], sour.mem[i]);
				return 1;
			}
		if (sref.ret != sour.ret || memcmp(sref.e1510, sour.e1510, 4) || sref.ev_n != sour.ev_n ||
		    memcmp(sref.evlog, sour.evlog, sizeof(sref.evlog)) || sref.printfs != sour.printfs ||
		    sref.nlog != sour.nlog) {
			printf("MISMATCH case %u: ret %llx/%llx ev %u/%u\n", c, (unsigned long long)sref.ret,
			       (unsigned long long)sour.ret, sref.ev_n, sour.ev_n);
			return 1;
		}
	}
	printf("t32_aeu_oracle: tisp_ae_tune %u compared, 0 mismatches (result 0/1/2/other %u %u %u)\n", ncmp,
	       nres[0], nres[1], nres[2]);
	return 0;
}
