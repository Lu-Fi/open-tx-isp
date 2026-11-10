/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tone mapping / adaptive dynamic range (TMO, "tisp_tmo_*") from the
 * vendor module (Matt Davis' recovered C, PR #15).  The block is ported in
 * phases, each proven bit-exact against the vendor functions by a host oracle
 * (tests/t32_oracle/t32_tmo<n>_oracle):
 *
 *  phase 1 (t32_tmo1_oracle): tisp_tmo_log2, tmo_interplate, tmo_interplate1,
 *    tmo_absfun, ramY_reg_read, tmo_reg_read, adr_5x5_out, adr_interp1_short,
 *    adr_ispint_adr_16 / _32 / _64, adr_subsection_map, tmo_size_update,
 *    adr_tgain_func.
 *  phase 2a (t32_tmo2_oracle): tmo_dark_ctr_interp, tmo_detail_ev_interp, tmo_satcurve_ev_interp.
 *  phase 2b (t32_tmo2b_oracle): tmo_dark_protect, adr_stat_calc.
 *  phase 3a (t32_tmo3a_oracle): tmo_ev_interp, drc_ctrl, adr_gamma_refresh, adr_params_refresh.
 *  phase 3b (t32_tmo3b_oracle): modechange_stat_refresh, tmo_params_hard_refresh.
 *  phase 3c (t32_tmo3c_oracle): timeratio_curve_adjust, adr_hardpars_ctl.
 *
 * The vendor addresses the per channel TMO state as t32_data_e35e0[ch] -> a
 * channel record {ctx, stat, ..., geom at +12}; the port takes the members it
 * needs as pointers.  Signed divisions: INT_MIN / -1 is INT_MIN as in the
 * vendor's reference emulation (MIPS div leaves HI/LO undefined there; vendor
 * UB, kept so the port never traps).  A zero divisor cannot occur in any call
 * of this phase and is not handled.  Dropped dead code (the vendor tests are
 * unreachable: the table entry found has x0 < x <= x1, so x1 != x0): the
 * "x1 == x0 -> ys[i]" returns of adr_ispint_adr_16 / _32 / _64.  Return values that are only leftover registers in the
 * vendor code (the high word, ramY_reg_read / tmo_reg_read) are not reproduced.
 */
#ifndef TX_ISP_T32_TMO_H
#define TX_ISP_T32_TMO_H

#include "tx_isp_t32_tp.h"
#include "tx_isp_t32_awbct.h"	/* t32_awb_fixmul / t32_awb_fixdiv = fix_point_mult2_32 / div_32 */

/* signed 32 bit division, INT_MIN / -1 = INT_MIN (see above) */
static inline int t32_tmo_sdiv(int n, int d)
{
	if (n == (int)0x80000000U && d == -1)
		return n;
	return n / d;
}

/* tisp_tmo_log2: number of doublings of 2 (at most 17) until a0 + step reaches a1, 0 when a0 >= a1 (both 16 bit) */
static inline u32 t32_tmo_log2(u32 a0, u32 a1)
{
	u32 step = 2, n = 0;

	a0 &= 0xffffU;
	a1 &= 0xffffU;
	if (a0 >= a1)
		return 0;
	for (;;) {
		step &= 0xffffU;
		n = (n + 1) & 0xffU;
		if (((a0 + step) & 0xffffU) >= a1 || n == 17U)
			return n;
		step <<= 1;
	}
}

/* tisp_tmo_tmo_interplate (x0, x1, y0, y1, x): y at x on the line (x0,y0)-(x1,y1), rounded; x1 == x0 is read as x0 + 1 */
static inline int t32_tmo_interplate(int x0, int x1, int y0, int y1, int x)
{
	u32 span = (u32)x1 + (u32)(x1 == x0) - (u32)x0;
	u32 num = (u32)y0 * span + (u32)(y1 - y0) * (u32)(x - x0);

	return t32_tmo_sdiv((int)(num + (u32)((int)span >> 1)), (int)span);
}

/* tisp_tmo_tmo_interplate1 (x1, x0, y0, y1, x): as above, weighted from the other end (x1 - x towards y1) */
static inline int t32_tmo_interplate1(int x1, int x0, int y0, int y1, int x)
{
	u32 hi = (u32)x1 + (u32)(x1 == x0);
	u32 span = hi - (u32)x0;
	u32 num = (u32)y0 * span + (u32)(y1 - y0) * (hi - (u32)x);

	return t32_tmo_sdiv((int)(num + (u32)((int)span >> 1)), (int)span);
}

/* tisp_tmo_tmo_absfun: |a - b| */
static inline u32 t32_tmo_absfun(u32 a, u32 b)
{
	return a < b ? b - a : a - b;
}

/* tisp_tmo_ramY_reg_read / tisp_tmo_tmo_reg_read: empty in the vendor, return 0 */
static inline u32 t32_tmo_ramy_reg_read(void)
{
	return 0;
}
static inline u32 t32_tmo_reg_read(void)
{
	return 0;
}

/*
 * tisp_tmo_adr_5x5_out: squared distance of (x1, y1) from (x0, y0) in 1/8 units
 * (each difference divided by 8, truncated) searched in the 31 entry s16 table tbl
 * from the top: the index (1 .. 31) after the first entry (from 30 down) that is not
 * below the distance, 0 when all are.
 */
static inline u32 t32_tmo_adr_5x5_out(int x0, int y0, int x1, int y1, const s16 *tbl)
{
	int dx = ((x1 < x0) ? (int)((u32)x0 - (u32)x1) : (int)((u32)x1 - (u32)x0)) / 8;
	int dy = ((y1 < y0) ? (int)((u32)y0 - (u32)y1) : (int)((u32)y1 - (u32)y0)) / 8;
	int d2 = (int)((u32)dy * (u32)dy + (u32)dx * (u32)dx);
	int i;

	for (i = 30; i >= 0; i--)
		if (!((int)tbl[i] < d2))
			return (u32)i + 1U;
	return 0;
}

/*
 * tisp_tmo_adr_interp1_short: for each of cnt inputs in[] the piecewise linear
 * value of the curve (xs[], ys[], n points) into out[]; below the first point ys[0],
 * above the last ys[n - 1], inside a segment the rounded line value capped at the
 * upper point.  n, cnt are s16 arguments of the vendor (n < 1 writes nothing).
 */
static inline void t32_tmo_adr_interp1_short(const s16 *xs, const s16 *ys, const s16 *in, s16 *out, int n, int cnt)
{
	int i, k;

	n = (s16)n;
	cnt = (s16)cnt;
	for (i = 0; i < cnt; i++) {
		int x = in[i];

		for (k = 1; k < n; k++) {
			int dx, v;

			if (!(xs[0] < x)) {
				out[i] = ys[0];
				break;
			}
			if (!(x < xs[k]))
				continue;
			dx = (s16)((u32)xs[k] - (u32)xs[k - 1]);
			v = t32_tmo_sdiv(t32_tmo_sdiv(dx, 2) +
					 (s16)((u32)ys[k] - (u32)ys[k - 1]) * (s16)((u32)x - (u32)xs[k - 1]), dx);
			v = (s16)((u32)v + (u32)ys[k - 1]);
			out[i] = (s16)v;
			if (ys[k] < v)
				out[i] = ys[k];
			break;
		}
		if (k == n)
			out[i] = ys[n - 1];
	}
}

/* tisp_tmo_adr_ispint_adr_16: y of the 16 bit x on the curve (xs[], ys[], n >= 1 points), rounded; ys[n - 1] above the last */
static inline int t32_tmo_adr_ispint_16(int x, const s16 *xs, const s16 *ys, int n)
{
	int i;

	x = (s16)x;
	for (i = 0; i < n; i++) {
		int xi = xs[i], x0, d;

		if (xi < x)
			continue;
		if (i == 0)
			return ys[0];
		x0 = xs[i - 1];
		d = xi - x0;
		return (s16)(t32_tmo_sdiv((int)((u32)(ys[i] - ys[i - 1]) * (u32)(x - x0) + (u32)(d >> 1)), d) + ys[i - 1]);
	}
	return ys[n - 1];
}

/* tisp_tmo_adr_ispint_adr_32: the same with a 32 bit x axis and an unreduced result */
static inline int t32_tmo_adr_ispint_32(int x, const int *xs, const s16 *ys, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		int xi = xs[i], x0, d;

		if (xi < x)
			continue;
		if (i == 0)
			return ys[0];
		x0 = xs[i - 1];
		d = (int)((u32)xi - (u32)x0);
		return (int)((u32)t32_tmo_sdiv((int)((u32)(ys[i] - ys[i - 1]) * ((u32)x - (u32)x0) + (u32)(d >> 1)), d) + (u32)ys[i - 1]);
	}
	return ys[n - 1];
}

/*
 * tisp_tmo_adr_ispint_adr_64: the same with a 64 bit x axis.  The rounded offset
 * |dy * (x - x0) + d / 2| / |d| (d = x1 - x0, 64 bit, fix_point_div with 0 fraction
 * bits) is added to ys[i - 1] when numerator and d have the same sign, else subtracted.
 */
static inline int t32_tmo_adr_ispint_64(long long x, const long long *xs, const s16 *ys, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		long long d;
		unsigned long long num, an, ad;
		u32 q;

		if (xs[i] < x)
			continue;
		if (i == 0)
			return ys[0];
		d = (long long)((unsigned long long)xs[i] - (unsigned long long)xs[i - 1]);
		num = (unsigned long long)(x - xs[i - 1]) * (unsigned long long)(long long)(ys[i] - ys[i - 1]) +
		      (unsigned long long)(d >> 1);
		an = (long long)num < 0 ? 0ULL - num : num;
		ad = d < 0 ? 0ULL - (unsigned long long)d : (unsigned long long)d;
		q = (u32)t32_tp_div64(an, ad);
		return (int)((((long long)num < 0) == (d < 0)) ? (u32)ys[i - 1] + q : (u32)ys[i - 1] - q);
	}
	return ys[n - 1];
}

/*
 * tisp_tmo_adr_subsection_map: find the entry of the 512 word table tab[] nearest to
 * pos (all entries of the minimum distance count, their mean index in sh0 fraction
 * bits rounded), scale by mul, look it up in the 129 point curve (a[] -> b[],
 * a[-1] / b[-1] are read for a hit on the first point) with sh0 fraction bits, then
 * mix the result s5 with the scale and divide by 100 in sh2 fraction bits.
 */
static inline int t32_tmo_adr_subsection_map(int pos, int s5, int s1, const s16 *a, const s16 *b, const int *tab,
					     int mul, u32 sh2, u32 sh0, u32 mode)
{
	int best = 10000, cnt = 0, sum = 0, k, v, half, t, r;

	for (k = 0; k < 512; k++) {
		int dist = pos < tab[k] ? (int)((u32)tab[k] - (u32)pos) : (int)((u32)pos - (u32)tab[k]);

		if (dist < best) {
			cnt = 0;
			sum = 0;
		} else if (dist != best) {
			continue;
		}
		sum += k;
		cnt++;
		best = dist;
	}
	if (!(cnt > 0))
		cnt = 1;
	if (!(sum > 0))
		sum = 1;
	/* the shifts use the low 5 bits (MIPS sll / sra), the fix_point_* callees get the full count */
	half = t32_tmo_sdiv((int)(1U << (sh0 & 31U)), 2);
	t = (int)(t32_awb_fixdiv(sh0, (u32)sum << (sh0 & 31U), (u32)cnt << (sh0 & 31U)) + (u32)half) >> (sh0 & 31U);
	t = (int)((u32)t * (u32)mul) - 1;
	r = b[128];
	for (k = 0; k < 129; k++) {
		if (t < a[k]) {
			u32 slope = t32_awb_fixdiv(sh0, (u32)(b[k] - b[k - 1]) << (sh0 & 31U),
						   (u32)(a[k] - a[k - 1]) << (sh0 & 31U));
			u32 m = t32_awb_fixmul(sh0, slope, (u32)(a[k] - t) << (sh0 & 31U));

			r = b[k] - ((int)(m + (u32)half) >> (sh0 & 31U));
			break;
		}
	}
	if (mode == 1 || !(s5 < r))
		v = (int)((u32)s5 * 100U + (u32)(r - s5) * (u32)s1);
	else
		v = (int)((u32)s5 * 100U);
	t = (int)t32_awb_fixdiv(sh2, (u32)v << (sh2 & 31U), 100U << (sh2 & 31U));
	return (int)((u32)t32_tmo_sdiv((int)(1U << (sh2 & 31U)), 2) + (u32)t) >> (sh2 & 31U);
}

/* tisp_tmo_tmo_size_update: geometry record (+12 of the channel record), 20 bytes */
struct t32_tmo_geom {
	u16 w, h;		/* +0, +2 */
	u8 bw, bh, ov, four;	/* +4 15, +5 25, +6 10, +7 4 */
	u8 two;			/* +8 2 */
	u8 gw, gh, gs;		/* +9 (bw + 2 four), +10 (bh + 2 four), +11 (2 two + ov) */
	u8 iw, ih;		/* +12 gw - four, +13 gh - four */
	u16 cells;		/* +14 bw * bh */
	u16 gcells;		/* +16 gw * gh */
	u8 shift;		/* +18, ratio class 0..9 (kept when the grid is 0x8000 cells or more) */
	u8 pad;
};
struct t32_tmo_stat {		/* record at channel record +4: only +48 .. +53 are written */
	u8 pad[48];
	u16 gw, gh, ratio;
};
struct t32_tmo_ctx {		/* big per channel context (channel record +0): only +10039 is written */
	u8 pad[10039];
	u8 lce_sel;
};

/*
 * w, h: 16 bit picture size (high bits ignored).  The statistics grid is
 * ((w + 7) / 15, (h + 12) / 25) cells; the ratio (2^k + cells / 2) / cells and the LCE
 * selector (13 .. 25 for cells < 0x8000, 13 + class) follow from the cell count; from
 * 0x8000 cells on the vendor writes ratio 0, selector 0 and keeps geom->shift.
 */
static inline void t32_tmo_size_update(struct t32_tmo_ctx *ctx, struct t32_tmo_stat *st, struct t32_tmo_geom *g,
				       u32 w, u32 h)
{
	u32 gw = (((w & 0xffffU) + 7U) / 15U) & 0xffffU;
	u32 gh = (((h & 0xffffU) + 12U) / 25U) & 0xffffU;
	u32 cells = (gw * gh) & 0xffffU, ratio = 0, k = 0;
	u8 sel = 0;

	g->bw = 15;
	g->bh = 25;
	g->w = (u16)w;
	g->ov = 10;
	g->h = (u16)h;
	g->four = 4;
	g->two = 2;
	if (cells < 0x8000U) {
		while (cells >= (8U << k))
			k++;			/* k = 0 for < 8, 1 for < 16 ... 12 for < 0x8000 */
		sel = (u8)(13U + k);
		ratio = cells ? ((0x2000U << k) + (cells >> 1)) / cells : 0xffffU;
		ratio &= 0xffffU;
		g->shift = k < 4 ? 0 : (u8)(k - 3);
	}
	st->gw = (u16)gw;
	st->gh = (u16)gh;
	st->ratio = (u16)ratio;
	ctx->lce_sel = sel;
	g->gw = (u8)(g->bw + 2U * g->four);
	g->gh = (u8)(g->bh + 2U * g->four);
	g->gs = (u8)(2U * g->two + g->ov);
	g->iw = (u8)(g->gw - g->four);
	g->ih = (u8)(g->gh - g->four);
	g->cells = (u16)(g->bw * g->bh);
	g->gcells = (u16)(g->gw * g->gh);
}

/* tisp_tmo_adr_tgain_func: the two tgain pairs reset to 100, the flag of channel ch cleared when it is 1 */
struct t32_tmo_tgain {
	u32 a[2];		/* t32_data_e35a0 */
	u32 b[2];		/* t32_data_e3590 */
	s16 *flag;		/* t32_data_e3520 */
};
static inline void t32_tmo_adr_tgain_func(struct t32_tmo_tgain *t, u32 ch)
{
	t->a[0] = t->a[1] = 100;
	t->b[0] = t->b[1] = 100;
	if (t->flag[ch] == 1)
		t->flag[ch] = 0;
}

/*
 * ---- phase 2a: ev interpolation (tmo_dark_ctr_interp, tmo_detail_ev_interp, tmo_satcurve_ev_interp) ----
 *
 * All three read the big context (ctx, channel record +0) and an "exposure" value: ev0 / ev1 are the record words
 * +24 / +28 (a 64 bit value, ev1 != 0 means "too large", mode 0) and ev1m the record word +32 (mode 1).  The
 * context byte ctx[10931] selects the mode: 0 = ascending u32 table (the match is tbl[i] <= ev < tbl[i + 1]),
 * otherwise descending (tbl[i] >= ev > tbl[i + 1]).  The search runs over 9 intervals, i = 9 means no match.
 * The vendor's recovered code has v1 (the stat pointer) clobbered by the leaf call returns in dark_ctr_interp; the
 * hardware leaf leaves v1 alone, so the oracle removes that artifact from the reference copy (REFFIX).
 */
#define T32_TMO_CTX_MODE 10931U
static inline u32 t32_tmo_ld8(const u8 *b, u32 o) { return b[o]; }
static inline int t32_tmo_lds8(const u8 *b, u32 o) { return (s8)b[o]; }
static inline u32 t32_tmo_ld16(const u8 *b, u32 o) { return *(const u16 *)(b + o); }
static inline u32 t32_tmo_ld32(const u8 *b, u32 o) { return *(const u32 *)(b + o); }

/* the interval search shared by the three functions; w = ((end - ev) << 8) / span for the match (16 bit), else 0 */
static inline u32 t32_tmo_ev_find(const u32 *tbl, u32 mode, u32 ev0, u32 ev1, u32 ev1m, u32 *w)
{
	u32 i;

	*w = 0;
	for (i = 0; i < 9; i++) {
		u32 a = tbl[i], b = tbl[i + 1];

		if (mode == 0) {
			if (ev1 != 0 || ev0 < a || !(ev0 < b))
				continue;
			*w = (((b - ev0) << 8) / (b - a)) & 0xffffU;
		} else {
			if (a < ev1m || !(b < ev1m))
				continue;
			*w = (((a - ev1m) << 8) / (a - b)) & 0xffffU;
		}
		return i;
	}
	return 9;
}

/* "ev above the last table entry" (or too large) / "ev below the first one" of the vendor's range clamps */
static inline int t32_tmo_ev_hi(const u32 *tbl, u32 mode, u32 ev0, u32 ev1, u32 ev1m)
{
	return mode == 0 ? (ev1 != 0 || !(ev0 < tbl[9])) : !(tbl[9] < ev1m);
}
static inline int t32_tmo_ev_lo(const u32 *tbl, u32 mode, u32 ev0, u32 ev1, u32 ev1m)
{
	return mode == 0 ? (ev1 == 0 && ev0 < tbl[0]) : (tbl[0] < ev1m);
}

/* the u16 outputs of tmo_dark_ctr_interp (stat record + 238 ..) */
struct t32_tmo_dark_out {
	u16 a238, a240, a242, a244, a246, a248;
};

/* tisp_tmo_tmo_dark_ctr_interp: six interpolations over the matching interval, clamps, the derived 240 / 248 / 244 */
static inline void t32_tmo_dark_ctr_interp(const u8 *ctx, struct t32_tmo_dark_out *o, u32 ev0, u32 ev1, u32 ev1m)
{
	u32 mode = t32_tmo_ld8(ctx, T32_TMO_CTX_MODE) != 0, w, t2 = 0, t4 = 0, i;
	const u32 *tbl = (const u32 *)(ctx + (mode ? 1008 : 888));
	u32 v;

	i = t32_tmo_ev_find(tbl, mode, ev0, ev1, ev1m, &w);
	if (i < 9) {
		u32 x = mode ? ev1m : ev0;
		int (*ip)(int, int, int, int, int) = mode ? t32_tmo_interplate1 : t32_tmo_interplate;
		int a = (int)tbl[i], b = (int)tbl[i + 1];

		o->a246 = (u16)ip(a, b, t32_tmo_ld16(ctx, 2798 + 2 * i), t32_tmo_ld16(ctx, 2800 + 2 * i), (int)x);
		t2 = ip(a, b, t32_tmo_ld16(ctx, 2820 + 2 * i), t32_tmo_ld16(ctx, 2822 + 2 * i), (int)x) & 0xffffU;
		o->a238 = (u16)ip(a, b, t32_tmo_ld16(ctx, 2710 + 2 * i), t32_tmo_ld16(ctx, 2712 + 2 * i), (int)x);
		t4 = ip(a, b, t32_tmo_ld16(ctx, 2732 + 2 * i), t32_tmo_ld16(ctx, 2734 + 2 * i), (int)x) & 0xffffU;
		o->a242 = (u16)ip(a, b, t32_tmo_ld16(ctx, 2754 + 2 * i), t32_tmo_ld16(ctx, 2756 + 2 * i), (int)x);
		o->a244 = (u16)ip(a, b, t32_tmo_ld16(ctx, 2776 + 2 * i), t32_tmo_ld16(ctx, 2778 + 2 * i), (int)x);
	}
	if (t32_tmo_ev_hi(tbl, mode, ev0, ev1, ev1m)) {
		o->a246 = (u16)t32_tmo_ld16(ctx, 2816);
		o->a238 = (u16)t32_tmo_ld16(ctx, 2728);
		t2 = t32_tmo_ld16(ctx, 2838);
		o->a242 = (u16)t32_tmo_ld16(ctx, 2772);
		t4 = t32_tmo_ld16(ctx, 2750);
		o->a244 = (u16)t32_tmo_ld16(ctx, 2794);
	}
	if (t32_tmo_ev_lo(tbl, mode, ev0, ev1, ev1m)) {
		o->a246 = (u16)t32_tmo_ld16(ctx, 2798);
		o->a238 = (u16)t32_tmo_ld16(ctx, 2710);
		t2 = t32_tmo_ld16(ctx, 2820);
		o->a242 = (u16)t32_tmo_ld16(ctx, 2754);
		t4 = t32_tmo_ld16(ctx, 2732);
		o->a244 = (u16)t32_tmo_ld16(ctx, 2776);
	}
	if (o->a238 < 128)
		o->a238 = 128;
	v = (1U << (t2 & 31)) & 0xffffU;
	o->a248 = (u16)(v + o->a246);
	o->a240 = (u16)((1U << (t4 & 31)) + o->a238);
	if (o->a248 >= 4096) {
		o->a248 = 4095;
		o->a246 = (u16)(4095U - v);
	}
	if ((int)o->a240 - 128 < (int)o->a244)
		o->a244 = (u16)(o->a240 - 128U);
}

/* tisp_tmo_tmo_detail_ev_interp: blend of the context's tables at weight w / 256 into the stat record (ctx tables +2530.. u16, +10932.. bytes) */
static inline int t32_tmo_blend(int next, int cur, u32 w)
{
	return (int)((u32)next * (256U - w) + (u32)cur * w) / 256;
}
static inline void t32_tmo_detail_ev_interp(const u8 *ctx, u8 *st, u32 ev0, u32 ev1, u32 ev1m)
{
	static const u16 b8[14][3] = {	/* ctx offset, stat offset, signed */
		{ 10932, 363, 1 }, { 10943, 364, 1 }, { 10954, 365, 1 }, { 10965, 366, 1 }, { 10053, 367, 0 },
		{ 10064, 368, 0 }, { 10976, 369, 1 }, { 10987, 370, 1 }, { 10075, 379, 0 }, { 10086, 380, 0 },
		{ 10097, 381, 0 }, { 10108, 382, 0 }, { 10119, 383, 0 }, { 10130, 384, 0 }
	};
	static const u16 b16[8][2] = {	/* ctx offset, stat offset */
		{ 2530, 56 }, { 2552, 60 }, { 2574, 64 }, { 2596, 68 }, { 2618, 76 }, { 2640, 80 }, { 2662, 84 }, { 2684, 88 }
	};
	u32 mode = t32_tmo_ld8(ctx, T32_TMO_CTX_MODE) != 0, w, i, k, d, dv;
	const u32 *tbl = (const u32 *)(ctx + (mode ? 1008 : 888));
	u32 r[8];

	i = t32_tmo_ev_find(tbl, mode, ev0, ev1, ev1m, &w);
	if (t32_tmo_ev_hi(tbl, mode, ev0, ev1, ev1m)) {
		w = 256;
		i = 9;
	}
	if (t32_tmo_ev_lo(tbl, mode, ev0, ev1, ev1m)) {
		w = 256;
		i = 0;
	}
	for (k = 0; k < 14; k++) {
		int c = b8[k][2] ? t32_tmo_lds8(ctx, b8[k][0] + i) : (int)t32_tmo_ld8(ctx, b8[k][0] + i);
		int n = b8[k][2] ? t32_tmo_lds8(ctx, b8[k][0] + i + 1) : (int)t32_tmo_ld8(ctx, b8[k][0] + i + 1);

		st[b8[k][1]] = (u8)t32_tmo_blend(n, c, w);
	}
	for (k = 0; k < 8; k++) {
		r[k] = (u32)t32_tmo_blend((int)t32_tmo_ld16(ctx, b16[k][0] + 2 * i + 2), (int)t32_tmo_ld16(ctx, b16[k][0] + 2 * i), w);
		*(u32 *)(st + b16[k][1]) = r[k];
	}
	d = r[2] - r[3];
	dv = r[1] - r[0];
	*(u32 *)(st + 72) = r[0] != r[1] ? ((d << 10) + (dv >> 1)) / dv : d << 10;
	d = r[7] - r[6];
	dv = r[5] - r[4];
	*(u32 *)(st + 92) = r[4] != r[5] ? ((d << 7) + (dv >> 1)) / dv : d << 7;
	*(u32 *)(st + 96) = t32_tmo_ld16(ctx, 2706);
	*(u32 *)(st + 100) = t32_tmo_ld16(ctx, 2708);
}

/*
 * tisp_tmo_tmo_satcurve_ev_interp: the first search (tables +888 / +1008) picks a pair of 25 entry u16 curves
 * (ctx + 1136 + 50 * min(i, 8), next at +50) blended into stat + 268; the second one (tables +968 / +1048,
 * range 0 on a miss instead of 256) picks 14 entry curve triples (+1652 / +1932 / +2212 + 28 * min(i, 8)) blended into
 * stat + 154 / + 182 / + 210.  An ev below the first entry uses weight 256 and pair 0.
 */
static inline void t32_tmo_satcurve_ev_interp(const u8 *ctx, u8 *st, u32 ev0, u32 ev1, u32 ev1m)
{
	u32 mode = t32_tmo_ld8(ctx, T32_TMO_CTX_MODE) != 0, w, i, k, s;

	for (s = 0; s < 2; s++) {
		const u32 *tbl = (const u32 *)(ctx + (mode ? 1008 + 40 * s : 888 + 80 * s));
		u32 m;

		i = t32_tmo_ev_find(tbl, mode, ev0, ev1, ev1m, &w);
		if (t32_tmo_ev_hi(tbl, mode, ev0, ev1, ev1m)) {
			w = 0;
			i = 9;
		}
		if (t32_tmo_ev_lo(tbl, mode, ev0, ev1, ev1m)) {
			w = 256;
			i = 0;
		}
		m = i > 8 ? 8 : i;
		if (s == 0) {
			for (k = 0; k < 25; k++)
				*(u16 *)(st + 268 + 2 * k) = (u16)t32_tmo_blend((int)t32_tmo_ld16(ctx, 1186 + 50 * m + 2 * k),
										(int)t32_tmo_ld16(ctx, 1136 + 50 * m + 2 * k), w);
		} else {
			static const u16 o[3][2] = { { 1652, 154 }, { 1932, 182 }, { 2212, 210 } };

			for (k = 0; k < 14; k++) {
				u32 j;

				for (j = 0; j < 3; j++)
					*(u16 *)(st + o[j][1] + 2 * k) = (u16)t32_tmo_blend(
						(int)t32_tmo_ld16(ctx, o[j][0] + 28 + 28 * m + 2 * k),
						(int)t32_tmo_ld16(ctx, o[j][0] + 28 * m + 2 * k), w);
			}
		}
	}
}

/*
 * ---- phase 2b: tmo_dark_protect, adr_stat_calc ----
 */

/* signed division as the vendor emulation reads it: a zero divisor (MIPS leaves HI / LO undefined) gives 1 / ~0 */
static inline int t32_tmo_sdiv0(int n, int d)
{
	if (d == 0)
		return n < 0 ? 1 : -1;
	return t32_tmo_sdiv(n, d);
}

/*
 * tisp_tmo_tmo_dark_protect: a 201 entry u16 curve (out, record +8) is pulled back to the default curve dflt
 * (vendor t32_data_c029c, 402 bytes) the more the dark statistic exceeds a limit.
 *   geom (record +12): u16 w, h; the cumulative histogram big + 30000 (512 u32, record +16) is clamped to
 *   (w * h) / 4, scaled to 0 .. 10000 into big + 32048 (512 u32).  ctx[2842] != 1: nothing more.  Else
 *   x = (u16)big32048[ctx16[2848]] (the index is not range checked by the vendor): below lo = ctx16[2844] nothing,
 *   above hi = ctx16[2846] the default curve replaces out, in between out -= (out - dflt) * (x - lo) / (hi - lo)
 *   (16 bit signed intermediates).  hi == lo with x == lo divides by zero: vendor UB, read as in t32_tmo_sdiv0.
 */
static inline void t32_tmo_dark_protect(const u8 *ctx, const u16 *geom, u8 *big, u16 *out, const u16 *dflt)
{
	u32 area = (u32)geom[0] * geom[1], k, acc = 0, lim, scale;
	u32 lo = t32_tmo_ld16(ctx, 2844), hi = t32_tmo_ld16(ctx, 2846), x;
	const u32 *hist = (const u32 *)(big + 30000);
	u32 *cum = (u32 *)(big + 32048);
	int w, span;

	lim = (u32)t32_tmo_sdiv((int)area, 4);
	scale = lim >> 8;
	for (k = 0; k < 512; k++) {
		acc += hist[k];
		if (!(acc < lim))
			acc = lim;
		cum[k] = ((scale ? (acc << 8) / scale : ~0U) * 10000U) >> 16;
	}
	if (t32_tmo_ld8(ctx, 2842) != 1)
		return;
	x = (u16)cum[t32_tmo_ld16(ctx, 2848)];
	if (x < lo)
		return;
	if (hi < x) {
		for (k = 0; k < 201; k++)
			out[k] = dflt[k];
		return;
	}
	w = (s16)(x - lo);
	span = (s16)(hi - lo);
	for (k = 0; k < 201; k++) {
		u32 o = out[k];
		int d = (s16)(o - dflt[k]);

		out[k] = (u16)(o - (u32)t32_tmo_sdiv0(d * w, span));
	}
}

/*
 * tisp_tmo_adr_stat_calc: statistics normalisation of the stat buffer b (record +8) with p, q = the per channel
 * scalars t32_data_e3580 / e3570: N = max(p * q / 4, 5120), D = N / 24.
 *   24 entries of the four u32 rows b + 5312 + 96 * r: rows 2 / 3 copied to b + 9856 / 9952, rows 0 / 1 / D to
 *   b + 9664 / 9760; the sums of rows 0 / 1 / N to b + 10048 / 10052.
 *   512 entry cumulative sum of b[0..511] clamped to N, (c << 8) / (N >> 8) * 10000 / 65536 to b + 5696.
 *   24 groups of 20 u32 at b + 2048 + 80 j (some pairs summed) * 10000 / D to 14 u32 at b + 3968 + 56 j.
 */
static inline void t32_tmo_adr_stat_calc(u8 *b, u32 p, u32 q)
{
	u32 *w = (u32 *)b;
	int n = t32_tmo_sdiv((int)(p * q), 4), d, k, j, c = 0;

	u32 sa = 0, sb = 0;

	if (n < 5120)
		n = 5120;
	d = n / 24;
	for (k = 0; k < 24; k++) {
		u32 *x = w + 1328 + k;		/* b + 5312 + 4k */

		sa += x[0];
		sb += x[24];
		x[1136] = x[48];		/* + 4544 */
		x[1160] = x[72];		/* + 4640 */
		x[1088] = (u32)((int)x[0] / d);	/* + 4352 */
		x[1112] = (u32)((int)x[24] / d);	/* + 4448 */
	}
	w[2512] = (u32)((int)sa / n);
	w[2513] = (u32)((int)sb / n);
	for (k = 0; k < 512; k++) {
		c += (int)w[k];
		if (!(c < n))
			c = n;
		w[1424 + k] = (u32)((int)(((u32)((int)((u32)c << 8) / (n >> 8))) * 10000U) / 65536);
	}
	for (j = 0; j < 24; j++) {
		const u32 *r = w + 512 + 20 * j;	/* b + 2048 + 80 j */
		u32 *o = w + 992 + 14 * j;		/* b + 3968 + 56 j */
		static const u8 pair[14] = { 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0 };
		u32 i, m = 0;

		for (i = 0; i < 14; i++) {
			u32 v = r[m];

			if (pair[i])
				v += r[m + 1];
			m += 1 + pair[i];
			o[i] = (u32)((int)(v * 10000U) / d);
		}
	}
}

/*
 * ---- phase 3a: tmo_ev_interp, drc_ctrl, adr_gamma_refresh, adr_params_refresh ----
 *
 * The channel record (t32_data_e35e0[ch]) as host pointers: +0 ctx, +4 stat, +8 out (201 u16 curve), +12 geom (u16 w, h),
 * +16 big block, +24 / +28 / +32 the ev words, u16 at +54 "drc on" and at +56 the strength; dflt = vendor
 * t32_data_c029c (the default curve), d89a0 / c0544 = the vendor strength curves (all 201 u16).
 */
struct t32_tmo_rec {
	u8 *ctx;
	u8 *st;
	u16 *out;
	u16 *geom;
	u8 *big;
	u32 ev0, ev1, ev1m;
	u32 drc_on, strength;
	const u16 *dflt, *d89a0, *c0544;
	u8 *tbuf;			/* record +20: 10 x 750 byte tone buffer */
	const u16 *c0430;		/* vendor t32_data_c0430: 10 u16 fill values */
	void (*dma_sync)(u8 *buf, u32 len);	/* private_dma_cache_sync (0, buf, len, 0) */
	u8 *e3501;			/* vendor t32_data_e3501: "curve refresh pending" byte (timeratio_curve_adjust) */
	const u16 *c090c, *c07a4, *c06d8;	/* vendor constant tables of timeratio_curve_adjust (179 / 179 / 256 entries) */
	const u8 *c06f0;		/* 178 index bytes into c06d8 */
};

/*
 * tisp_tmo_tmo_ev_interp: ctx16[4138] == 0: out = blend of two neighbouring curves of the 10 curve set
 * (ctx + 7398, + 7800, then + 4182 + 402 n; the pair of interval i is (C[i], C[i + 1]), i <= 8) at weight w / 256 from the
 * ev search (a miss above the table is w = 0 / i = 9, below it w = 256 / i = 0);
 * == 1: out = (ctx curve + 7398 * w + dflt * (256 - w)) / 256 with w interpolated linearly (unsigned arithmetic, as the vendor's divu) in the 10 entry u16 table (ctx16[4138] is the selector)
 * ctx + 4118 (a miss: entry 0 / 9); other values leave out alone.  Then tmo_detail_ev_interp and tmo_dark_protect run
 * (the latter a tail call).
 */
static inline u32 t32_tmo_ev_interp_w1(const u8 *ctx, const u32 *tbl, u32 mode, u32 ev0, u32 ev1, u32 ev1m)
{
	u32 i, w = 0;

	for (i = 0; i < 9; i++) {
		u32 a = tbl[i], b = tbl[i + 1];
		u32 cur = t32_tmo_ld16(ctx, 4118 + 2 * i), nxt = t32_tmo_ld16(ctx, 4120 + 2 * i);

		if (mode == 0) {
			if (ev1 != 0 || ev0 < a || !(ev0 < b))
				continue;
			w = (cur - (cur - nxt) * (ev0 - a) / (b - a)) & 0xffffU;
		} else {
			if (a < ev1m || !(b < ev1m))
				continue;
			w = (cur - (cur - nxt) * (a - ev1m) / (a - b)) & 0xffffU;
		}
		break;
	}
	if (t32_tmo_ev_hi(tbl, mode, ev0, ev1, ev1m))
		w = t32_tmo_ld16(ctx, 4136);
	if (t32_tmo_ev_lo(tbl, mode, ev0, ev1, ev1m))
		w = t32_tmo_ld16(ctx, 4118);
	return w;
}
static inline void t32_tmo_ev_interp(struct t32_tmo_rec *r)
{
	const u8 *ctx = r->ctx;
	u32 sel = t32_tmo_ld16(ctx, 4138), mode = t32_tmo_ld8(ctx, T32_TMO_CTX_MODE) != 0, w, i, k;
	const u32 *tbl = (const u32 *)(ctx + (mode ? 1008 : 888));

	if (sel == 0) {
		u32 ca, cb;

		i = t32_tmo_ev_find(tbl, mode, r->ev0, r->ev1, r->ev1m, &w);
		if (t32_tmo_ev_hi(tbl, mode, r->ev0, r->ev1, r->ev1m)) {
			w = 0;
			i = 9;
		}
		if (t32_tmo_ev_lo(tbl, mode, r->ev0, r->ev1, r->ev1m)) {
			w = 256;
			i = 0;
		}
		if (i > 8)
			i = 8;
		ca = i == 0 ? 7398 : i == 1 ? 7800 : 4182 + 402 * (i - 2);
		cb = i == 0 ? 7800 : 4182 + 402 * (i - 1);
		for (k = 0; k < 201; k++)
			r->out[k] = (u16)t32_tmo_blend((int)t32_tmo_ld16(ctx, cb + 2 * k), (int)t32_tmo_ld16(ctx, ca + 2 * k), w);
	} else if (sel == 1) {
		w = t32_tmo_ev_interp_w1(ctx, tbl, mode, r->ev0, r->ev1, r->ev1m);
		for (k = 0; k < 201; k++)
			r->out[k] = (u16)t32_tmo_blend((int)r->dflt[k], (int)t32_tmo_ld16(ctx, 7398 + 2 * k), w);
	}
	t32_tmo_detail_ev_interp(ctx, r->st, r->ev0, r->ev1, r->ev1m);
	t32_tmo_dark_protect(ctx, r->geom, r->big, r->out, r->dflt);
}

/* tisp_tmo_drc_ctrl: drc on (rec u16 +54 == 1) and ctx[10042] == 1: ev_interp, then the curve is moved towards d89a0 (strength < 128) or c0544 (> 128) */
static inline void t32_tmo_drc_ctrl(struct t32_tmo_rec *r)
{
	u32 k, s = r->strength & 0xffffU;

	if ((r->drc_on & 0xffffU) != 1 || t32_tmo_ld8(r->ctx, 10042) != 1)
		return;
	t32_tmo_ev_interp(r);
	if (s < 128) {
		for (k = 0; k < 201; k++)
			r->out[k] = (u16)(((int)((u32)r->out[k] * s + (u32)r->d89a0[k] * (128U - s))) / 128);
	} else if (s != 128) {
		for (k = 0; k < 201; k++)
			r->out[k] = (u16)(((int)((s - 128U) * r->c0544[k] + (256U - s) * (u32)r->out[k])) / 128);
	}
}

/* tisp_tmo_adr_gamma_refresh: 258 bytes of the per channel gamma block (t32_data_dc274[ch] + 32) into ctx + 9272; adr_params_refresh is the same call */
static inline void t32_tmo_adr_gamma_refresh(u8 *ctx, const u8 *gamma32)
{
	u32 k;

	for (k = 0; k < 258; k++)
		ctx[9272 + k] = gamma32[k];
}
static inline void t32_tmo_adr_params_refresh(u8 *ctx, const u8 *gamma32)
{
	t32_tmo_adr_gamma_refresh(ctx, gamma32);
}

/*
 * tisp_tmo_modechange_stat_refresh: the ten 750 byte blocks of the tone buffer (record +20) are filled with the u16
 * c0430[blk] (375 entries each), the buffer is cache synced (7520 bytes, as the vendor: 20 more than the fill),
 * the out curve gets the d89a0 curve (201 u16) and tmo_ev_interp runs (a tail call).
 */
static inline void t32_tmo_modechange_stat_refresh(struct t32_tmo_rec *r)
{
	u32 b, k;

	for (b = 0; b < 10; b++)
		for (k = 0; k < 375; k++)
			*(u16 *)(r->tbuf + 750 * b + 2 * k) = r->c0430[b];
	r->dma_sync(r->tbuf, 7520);
	for (k = 0; k < 201; k++)
		r->out[k] = r->d89a0[k];
	t32_tmo_ev_interp(r);
}

/*
 * tisp_tmo_tmo_params_hard_refresh: the hardware parameters from ctx to the stat record st (record +4): plain copies,
 * the seven log2 of neighbouring u16 pairs st16[324 + 2i] / [326 + 2i] into st[356 + i], six ctx u16 (3370 .. 3380)
 * zeroed before their 24 byte block is copied, two products ctx16[3384 / 3386] * (st16[48] * st16[50] & 0xffff),
 * rounded by 128, divided by 256.
 */
static inline void t32_tmo_params_hard_refresh(u8 *ctx, u8 *st)
{
	static const u16 cp[][3] = {	/* stat offset, ctx offset, length */
		{ 250, 1636, 8 }, { 258, 1644, 8 }, { 318, 2492, 6 }, { 324, 2498, 16 }, { 340, 2514, 16 },
	};
	static const u16 cq[][3] = {
		{ 371, 10032, 6 }, { 385, 10042, 4 }, { 104, 3308, 50 },
	};
	u32 i, k, a;

	for (i = 0; i < 5; i++)
		for (k = 0; k < cp[i][2]; k++)
			st[cp[i][0] + k] = ctx[cp[i][1] + k];
	for (i = 0; i < 7; i++)
		st[356 + i] = (u8)t32_tmo_log2(t32_tmo_ld16(st, 324 + 2 * i), t32_tmo_ld16(st, 326 + 2 * i));
	for (i = 0; i < 3; i++)
		for (k = 0; k < cq[i][2]; k++)
			st[cq[i][0] + k] = ctx[cq[i][1] + k];
	for (i = 0; i < 6; i++)
		*(u16 *)(ctx + 3370 + 2 * i) = 0;
	for (k = 0; k < 24; k++)
		st[390 + k] = ctx[3358 + k];
	a = (t32_tmo_ld16(st, 48) * t32_tmo_ld16(st, 50)) & 0xffffU;
	*(u16 *)(st + 414) = (u16)((int)(t32_tmo_ld16(ctx, 3384) * a + 128U) / 256);
	*(u16 *)(st + 416) = (u16)((int)(t32_tmo_ld16(ctx, 3386) * a + 128U) / 256);
	for (k = 0; k < 375; k++)
		st[418 + k] = ctx[10141 + k];
	for (k = 0; k < 375; k++)
		st[793 + k] = ctx[10516 + k];
	st[266] = ctx[10040];
	for (k = 0; k < 8; k++)
		st[1168 + k] = ctx[8258 + k];
}

static inline int t32_tmo_lds16(const u8 *b, u32 o) { return *(const s16 *)(b + o); }

/*
 * ---- phase 3c: timeratio_curve_adjust, adr_hardpars_ctl ----
 *
 * tisp_tmo_timeratio_curve_adjust (ch = the record r; x = u16, mode / sel / a4 / a5 / a6 = u8, a7 = u16, high bits of the
 * arguments ignored as in the vendor).  base = 2000 + 140 x / 256 (x < 1281), 3400 - 140 x / 256 (x < 2561), else 2500;
 * a6 == 1 replaces it by a7.  Only for sel == 1, mode == 3 and a4 * 32 >= x: t = x * base / 256 (u16); t >= 2: the first
 * interval (c090c[i] < t <= c090c[i + 1], i < 178) gives s = (interpolation of c07a4 over it) / c06d8[c06f0[i]], else s = 0
 * (a zero divisor in the vendor tables is not handled: they are constants; the vendor emulation would give -1 / 1).
 * Then (a6 == 1: ev_interp first) out[k] = 8191 for every k with d89a0[k] >= s; V = 1 + first k < 200 with d89a0[k] < s <=
 * d89a0[k + 1] (0: none); V < 3: e3501 = 1 and done; else m = min(a5, V < 6 ? 1 : V <= 12 ? (V - 2) / 2 : 10) and the m entries out[V - m ..
 * V - 1] are pulled linearly towards 8191 from out[V - m - 1]; e3501 = 1.  Every other path (sel / mode / a4 not
 * matching): ev_interp if e3501 == 1, then e3501 = 0.  Return value (leftover registers) not reproduced.
 */
static inline void t32_tmo_timeratio_curve_adjust(struct t32_tmo_rec *r, u32 x, u32 mode, u32 sel, u32 a4, u32 a5, u32 a6, u32 a7)
{
	u32 base, t, s = 0, i, V = 0, m, a, k;
	u16 *out = r->out;

	x &= 0xffffU; mode &= 0xffU; sel &= 0xffU; a4 &= 0xffU; a5 &= 0xffU; a6 &= 0xffU; a7 &= 0xffffU;
	if (x < 1281)
		base = ((x * 140U) >> 8) + 2000U;
	else if (x < 2561)
		base = 3400U - ((x * 140U) >> 8);
	else
		base = 2500U;
	if (a6 == 1)
		base = a7;
	if (sel != 1 || mode != 3 || (a4 << 5) < x) {
		if (*r->e3501 == 1)
			t32_tmo_ev_interp(r);
		*r->e3501 = 0;
		return;
	}
	t = ((x * base) >> 8) & 0xffffU;
	if (t >= 2) {
		for (i = 0; i != 178; i++) {
			u32 lo = r->c090c[i], hi = r->c090c[i + 1], s0;

			if (!(lo < t) || hi < t)
				continue;
			s0 = r->c07a4[i];
			s = (u32)t32_tmo_sdiv((int)((hi - lo) * s0 + (r->c07a4[i + 1] - s0) * (t - lo)), (int)r->c06d8[r->c06f0[i]]) & 0xffffU;
			break;
		}
	}
	if (a6 == 1)
		t32_tmo_ev_interp(r);
	for (k = 0; k < 201; k++)
		if (!(r->d89a0[k] < s))
			out[k] = 8191;
	for (k = 0; k < 200; k++)
		if (r->d89a0[k] < s && !(r->d89a0[k + 1] < s)) {
			V = k + 1;
			break;
		}
	if (V >= 3) {
		m = V < 6 ? 1 : V <= 12 ? ((V - 2) >> 1) & 0xffffU : 10;
		if (a5 < m)
			m = a5;
		for (a = 1; a < m + 1; a++) {
			int t1 = out[V - m - 1];

			out[V - a] = (u16)(t1 + t32_tmo_sdiv((8191 - t1) * (int)(m + 1 - a), (int)(m + 1)));
		}
	}
	*r->e3501 = 1;
}

/*
 * tisp_tmo_adr_hardpars_ctl: tab = the 300 byte vendor table block at t32_data_bff90 (three 12 byte tables, then eleven 24 byte
 * tables).  ctx s16 [9992] == 2 / 1 / other selects (12 byte table, five 24 byte tables) = (2: P2, Q0 Q2 Q5 Q8 Q10;
 * 1: P1, Q1 Q3 Q5 Q7 Q9; else P0, Q2 Q4 Q5 Q6 Q8): the 12 bytes go to ctx + 300, the 24 byte tables to ctx + 418 + 24 j.
 * The two strengths ctx s16 [9984] / [9986] are clamped to 0 .. 128 (and copied to ctx + 312 / 314), ctx [9988] / [9990]
 * to ctx + 316 / 318.  ctx s16 [9994] != 1: 18 u16 at ctx + 538 = 1024.  == 1: the 18 entries ctx16[538 + 2 i] are a
 * piecewise linear running sum (breakpoints s16 [9996] .. [10002], values u16 [10004] .. [10010]) with the vendor's
 * remainder rounding; *flag (t32_data_e3518[ch]) = 1.
 */
static inline void t32_tmo_adr_hardpars_ctl(u8 *ctx, const u8 *tab, u16 *flag)
{
	static const u8 qs[3][5] = { { 2, 4, 5, 6, 8 }, { 1, 3, 5, 7, 9 }, { 0, 2, 5, 8, 10 } };
	int sel = t32_tmo_lds16(ctx, 9992), c0, c1, t0, bp[3], i, j;
	u32 k;
	const u8 *q = qs[sel == 2 ? 2 : sel == 1 ? 1 : 0];

	*flag = 1;
	for (k = 0; k < 12; k++)
		ctx[300 + k] = tab[(sel == 2 ? 24 : sel == 1 ? 12 : 0) + k];
	for (j = 0; j < 5; j++)
		for (k = 0; k < 24; k++)
			ctx[418 + 24 * j + k] = tab[36 + 24 * q[j] + k];
	c0 = t32_tmo_lds16(ctx, 9984);
	c0 = c0 < 0 ? 0 : c0 < 129 ? c0 : 128;
	*(u16 *)(ctx + 9984) = (u16)c0;
	c1 = t32_tmo_lds16(ctx, 9986);
	c1 = c1 < 0 ? 0 : c1 < 129 ? c1 : 128;
	*(u16 *)(ctx + 9986) = (u16)c1;
	*(u16 *)(ctx + 314) = (u16)c1;
	*(u16 *)(ctx + 312) = (u16)c0;
	*(u16 *)(ctx + 316) = (u16)t32_tmo_ld16(ctx, 9988);
	*(u16 *)(ctx + 318) = (u16)t32_tmo_ld16(ctx, 9990);
	if (t32_tmo_lds16(ctx, 9994) != 1) {
		for (i = 0; i < 18; i++)
			*(u16 *)(ctx + 538 + 2 * i) = 1024;
		return;
	}
	t0 = t32_tmo_lds16(ctx, 9996);
	bp[0] = t32_tmo_lds16(ctx, 9998);
	bp[1] = t32_tmo_lds16(ctx, 10000);
	bp[2] = t32_tmo_lds16(ctx, 10002);
	for (i = 0; i < 18; i++) {
		u16 *o = (u16 *)(ctx + 538 + 2 * i);
		int num, den, hi, lo, lim, qt, rem, z;

		if (!(t0 < i)) {
			*o = (u16)t32_tmo_lds16(ctx, 10004);
			continue;
		}
		if (!(bp[0] < i)) {
			num = (s16)(i - (t0 & 0xffff));
			den = (s16)(bp[0] - (t0 & 0xffff));
			lo = 10004; hi = 10006;
		} else if (!(bp[1] < i)) {
			num = (s16)(i - (bp[0] & 0xffff));
			den = (s16)(bp[1] - (bp[0] & 0xffff));
			lo = 10006; hi = 10008;
		} else if (!(bp[2] < i)) {
			num = (s16)(i - (bp[1] & 0xffff));
			den = (s16)(bp[2] - (bp[1] & 0xffff));
			lo = 10008; hi = 10010;
		} else {
			*o = o[-1];
			continue;
		}
		lim = (s16)(t32_tmo_ld16(ctx, hi) - t32_tmo_ld16(ctx, lo));
		qt = den ? (s16)(lim / den) : 0;
		qt &= 0xffff;
		rem = (s16)(lim - (int)((u32)qt * (u32)den));
		z = o[-1] + (rem < num ? 0 : 1);
		*o = (u16)(qt + z);
	}
}

#endif /* TX_ISP_T32_TMO_H */
