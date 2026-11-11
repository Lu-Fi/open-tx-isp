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
 *  phase 4 (t32_tmo4_oracle): adr_5x5_init, adr_base_pars, adr_gauss_local, adr_local_info.
 *  phase 5a (t32_tmo5a_oracle): adr_map_y_filter, adr_map_y_filter_sp.
 *  phase 5b (t32_tmo5b_oracle): adr_map_curve1.
 *  phase 5c (t32_tmo5c_oracle): adr_ev_func.
 *  phase 6a (t32_tmo6a_oracle): adr_subsection_up, tisp_adr_faceae_curve_adjust (callee of adr_fpga).
 *  phase 7a (t32_tmo7a_oracle): tgain_update, api_strength_ctrl, param_array_get, api_get_curve, ev_update.
 *  phase 8a (t32_tmo8a_oracle): adr_faceae_refresh, fbf_interrupt_calc, tmo_soft_process, adr_soft_process.
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
#include "tx_isp_t32_awblong.h"	/* t32_awb_fixdiv64 = fix_point_div */

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

/*
 * ---- phase 4: adr_5x5_init, adr_base_pars, adr_gauss_local, adr_local_info ----
 *
 * tisp_math_exp2 (the shared fixed point math, not part of the TMO block) is taken as a function pointer.
 */
typedef u32 (*t32_tmo_exp2_fn)(u32 v, u32 in_frac, u32 out_frac);

/*
 * tisp_tmo_adr_5x5_init (w, h, tbl = the 31 s16 distance table, ctx): two (2P .. 3P) x (2Q .. 3Q) point loops, P = (w + 3) / 6,
 * Q = (h + 2) / 4, histogram the 5x5 distance index of eight sample points (adr_5x5_out): three counters / sums pairs
 * (cnt A with sums B, C, D; cnt E with sum F; cnt G with sum H), each 32 u32.  Every index i (0 .. 31) gives the rounded mean
 * (cnt / 2 + sum) / cnt as a u8 (cnt == 0: the previous entry, entry 0: 0); the five 32 byte results go to ctx + 640 (G / H),
 * 672 (E / F), 704 (A / B), 736 (A / C), 768 (A / D).  The vendor takes the eight 128 byte buffers from private_vmalloc (unchecked)
 * and frees them again: a local array here (1 KiB of stack).
 */
static inline void t32_tmo_adr_5x5_init(u32 w, u32 h, const s16 *tbl, u8 *ctx)
{
	u32 cnt[3][32], sum[5][32];	/* cnt A / E / G; sum B, C, D (cnt A), F (cnt E), H (cnt G) */
	u8 res[5][32];
	int P = t32_tmo_sdiv((int)(w + 3U), 6), Q = t32_tmo_sdiv((int)(h + 2U), 4);
	int hp = t32_tmo_sdiv(P + 1, 2), hq = t32_tmo_sdiv(Q + 1, 2);
	int x0 = 2 * P, y0 = 2 * Q, xe = 3 * P, ye = 3 * Q, y, x, i, k;
	int px[3] = { P + hp, hp, 2 * P + hp }, py[3] = { Q + hq, hq, 2 * Q + hq };

	memset(cnt, 0, sizeof(cnt));
	memset(sum, 0, sizeof(sum));
	for (y = y0; y <= ye; y++)
		for (x = x0; x <= xe; x++) {
			u32 i1 = t32_tmo_adr_5x5_out(y, x, py[0], px[0], tbl), i5;
			u32 i7;

			cnt[0][i1]++;
			sum[0][i1] += t32_tmo_adr_5x5_out(y, x, py[1], px[1], tbl);	/* (t4, t9) */
			sum[1][i1] += t32_tmo_adr_5x5_out(y, x, py[0], px[1], tbl);	/* (t7, t9) */
			sum[2][i1] += t32_tmo_adr_5x5_out(y, x, py[1], px[0], tbl);	/* (t4, t8) */
			i5 = t32_tmo_adr_5x5_out(y, x, py[2], px[0], tbl);		/* (t0, t8) */
			cnt[1][i5]++;
			sum[3][i5] += t32_tmo_adr_5x5_out(y, x, py[2], px[1], tbl);	/* (t0, t9) */
			i7 = t32_tmo_adr_5x5_out(y, x, py[0], px[2], tbl);		/* (t7, t3) */
			cnt[2][i7]++;
			sum[4][i7] += t32_tmo_adr_5x5_out(y, x, py[1], px[2], tbl);	/* (t4, t3) */
		}
	for (k = 0; k < 5; k++) {
		static const u8 cs[5] = { 2, 1, 0, 0, 0 };	/* counter of result k (ctx 640, 672, 704, 736, 768): G, E, A, A, A */
		static const u8 ss[5] = { 4, 3, 1, 0, 2 };	/* its sum */
		const u32 *c = cnt[cs[k]], *sm = sum[ss[k]];

		for (i = 0; i < 32; i++)
			res[k][i] = c[i] ? (u8)t32_tmo_sdiv((int)(c[i] / 2 + sm[i]), (int)c[i]) : i ? res[k][i - 1] : 0;
	}
	for (k = 0; k < 5; k++)
		memcpy(ctx + 640 + 32 * k, res[k], 32);
}

/*
 * tisp_tmo_adr_base_pars (w, h, ctx, d8724 = the vendor table of 31 int factors): ctx + 320: five u16 (0, h4, 2 h4, 3 h4, h), h4 = h / 4 made
 * even; ctx + 330: seven u16 (0, k w6 for k = 1 .. 5, w), w6 = w / 6 made even (all masked to 16 bit).  A table of 31 u16
 * at ctx + 574: (m * m * d8724[i] + 0x20000) >> 18 (64 bit, wrapping), m = (3 * min((w6 + 1) / 2, (h4 + 1) / 2) + 1) / 2 (the vendor
 * takes the smaller one: h4 < w6 -> the h one).  Then adr_5x5_init (w, h, that table, ctx).
 */
static inline void t32_tmo_adr_base_pars(u32 w, u32 h, u8 *ctx, const int *d8724)
{
	int w6 = t32_tmo_sdiv((int)w, 6), h4 = t32_tmo_sdiv((int)h, 4), mm, i;
	u32 w16, h16;
	u16 a[5], b[7], t[31];
	u64 d;

	h4 -= h4 & 1;
	w6 -= w6 & 1;
	h16 = (u32)h4 & 0xffffU;
	w16 = (u32)w6 & 0xffffU;
	a[0] = 0; a[1] = (u16)h4; a[2] = (u16)(2 * h16); a[3] = (u16)(3 * h16); a[4] = (u16)h;
	b[0] = 0; b[1] = (u16)w6; b[2] = (u16)(2 * w16); b[3] = (u16)(3 * w16); b[4] = (u16)(4 * w16); b[5] = (u16)(5 * w16); b[6] = (u16)w;
	memcpy(ctx + 320, a, 10);
	memcpy(ctx + 330, b, 14);
	w6 = t32_tmo_sdiv(w6 + 1, 2);
	h4 = t32_tmo_sdiv(h4 + 1, 2);
	mm = h4 < w6 ? 3 * h4 : 3 * w6;
	mm = t32_tmo_sdiv(mm + 1, 2);
	d = (u64)((long long)mm * mm);
	for (i = 0; i < 31; i++)
		t[i] = (u16)((d * (u64)(long long)d8724[i] + 0x20000ULL) >> 18);
	memcpy(ctx + 574, t, 62);
	t32_tmo_adr_5x5_init(w, h, (const s16 *)t, ctx);
}

/*
 * tisp_tmo_adr_gauss_local (out = 24 int, ctx, cache = t32_data_e3210 (4 s16), tab = the 5 x 24 int vendor tables at t32_data_c00bc): the
 * parameters V = ctx s16 [9898] + 80 limited to 80 .. 1104, t = [9900] to 0 .. 200, s = [9902] to 0 .. 320, k = [9904] to 0 .. 5; unchanged
 * from the cache: nothing.  Else the 24 values exp(-((T0[i] - t + 100)^2 + (Tk[i] - s + off)^2) / (2 V^2)) * 10000 (fixed point: fixdiv,
 * fixmul by 1 / ln 2, exp2) with Tk, off = (T1, 100), (T2, 120), (T3, 140), (T4, 160) for k = 0 .. 3 and (T2, 120) for 4, 5, then
 * normalised to the maximum of entries 9 .. 12 (at least 0) * 10000 / max (max == 0: vendor UB, the emulation's 1 / -1 as in sdiv0).
 */
static inline void t32_tmo_adr_gauss_local(int *out, const u8 *ctx, s16 *cache, const int *tab, t32_tmo_exp2_fn exp2)
{
	u32 K = t32_awb_fixdiv(16, 0x385b0000U, 0x27100000U), den;
	int V = t32_tmo_lds16(ctx, 9898) + 80, t = t32_tmo_lds16(ctx, 9900), s = t32_tmo_lds16(ctx, 9902), k = t32_tmo_lds16(ctx, 9904);
	int i, mx;
	const int *T;
	int off;

	V = V < 80 ? 80 : V;
	V = V < 1105 ? V : 1104;
	t = t < 0 ? 0 : t;
	t = t < 201 ? t : 200;
	s = s < 0 ? 0 : s;
	s = s < 321 ? s : 320;
	k = k < 0 ? 0 : k;
	k = k < 6 ? k : 5;
	if (cache[0] == V && cache[1] == t && cache[2] == s && cache[3] == k)
		return;
	cache[0] = (s16)V; cache[1] = (s16)t; cache[2] = (s16)s; cache[3] = (s16)k;
	den = (u32)(2 * V) * (u32)V << 10;
	switch (k) {
	case 0: T = tab + 24; off = 100; break;
	case 2: T = tab + 72; off = 140; break;
	case 3: T = tab + 96; off = 160; break;
	default: T = tab + 48; off = 120; break;	/* 1, 4, 5 */
	}
	for (i = 0; i < 24; i++) {
		u32 a0 = (u32)tab[i] - (u32)(t - 100), w = (u32)T[i] - (u32)(s - off), d = w * w + a0 * a0, m, e, r;

		d = (int)d < 500001 ? d : 500000U;
		m = t32_awb_fixmul(16, t32_awb_fixdiv(10, d << 10, den) << 6, K);
		m = (int)m < 0xf0000 ? m : 0xeffffU;
		e = exp2(m, 16, 16);
		r = t32_awb_fixdiv(16, 0x27100000U, e);
		out[i] = (int)r >> 16;
	}
	mx = out[9] < out[10] ? out[10] : out[9];
	mx = mx < 0 ? 0 : mx;
	mx = out[11] < mx ? mx : out[11];
	mx = mx < out[12] ? out[12] : mx;
	for (i = 0; i < 24; i++)
		out[i] = t32_tmo_sdiv0((int)(10000U * (u32)out[i]), mx);
}

/*
 * tisp_tmo_adr_local_info (p0, p1, p2, p3, p4): a 10000 scaled value of a gaussian in the difference d = p1 - s, s = max(p4, 1) (p3 for
 * p1 < s, p2 for p1 >= s as the sigma, e = exp(-d^2 / (2 sigma^2)) in the fixed point steps of gauss_local), centre p0:
 * p1 < s: p0 * 65536 / e >> 16; else (p0 * 65536 - p0 * 65536 / e >> 16) + p0, limited to 10000, then * 1024 / 0x19000 (rounded, >> 10).
 * A zero sigma / exp2 result is a vendor division by zero (not handled; the call sites never pass one).
 */
static inline int t32_tmo_adr_local_info(u32 p0, u32 p1, u32 p2, u32 p3, u32 p4, t32_tmo_exp2_fn exp2)
{
	u32 K = t32_awb_fixdiv(16, 0x385b0000U, 0x27100000U), s = (int)p4 > 0 ? p4 : 1U, s2 = p0 << 16, d, sg, m, e, r;
	int a1;

	if ((int)p1 < (int)s) {
		d = s - p1;
		sg = p3;
	} else {
		d = p1 - s;
		sg = p2;
	}
	d = d * d;
	d = (int)d < 0x3ffffd ? d : 0x3ffffcU;
	m = t32_awb_fixmul(16, t32_awb_fixdiv(10, d << 10, (2U * sg * sg) << 10) << 6, K);
	m = (int)m < 0xf0000 ? m : 0xeffffU;
	e = exp2(m, 16, 16);
	r = t32_awb_fixdiv(16, s2, e);
	if ((int)p1 < (int)s)
		a1 = (int)r >> 16;
	else
		a1 = ((int)(s2 - r) >> 16) + (int)p0;
	a1 = a1 < 10001 ? a1 : 10000;
	return ((int)t32_awb_fixdiv(10, (u32)a1 << 10, 0x19000U) + 512) >> 10;
}

/*
 * ---- phase 5a: adr_map_y_filter, adr_map_y_filter_sp ----
 *
 * The slope s[i] = (dy * 10000 + dx / 2) / dx of the curve piece (x, y) -> (x', y'), dx == 0 reads as t32_tmo_sdiv0 (vendor UB).  The
 * vendor's running sums are kept as 64 bit values (low / high register pairs there).  The filter pass replaces s[1 .. n - 2] in
 * place by (s[i - 1] + s[i] + s[i + 1] + 1) / 3 (the already replaced left neighbour), sum = their total; the curve is
 * then (re)built from the slopes of the stretched sum: s[i] = (s[i] * total + sum / 2) / sum (64 bit, the unsigned fix_point_div, total
 * the sum of the original slopes), y' = y + (dx * s[i] + 5000) / 10000.
 */
static inline u32 t32_tmo_slope(int dy, int dx)
{
	return (u32)t32_tmo_sdiv0((int)((u32)t32_tmo_sdiv(dx, 2) + (u32)dy * 10000U), dx);
}

/*
 * tisp_tmo_adr_map_y_filter (xs = 16 s16 x positions, xs[15] is set to 4095; b = 14 u32 y values in and out; iters / idx the s16
 * arguments): Y[0] = 0, Y[1 .. 14] = b[0 .. 13], Y[15] = 4095, 15 slopes; iters passes of the filter; per pass the curve is rebuilt:
 * for k <= idx from the filtered slopes, beyond idx as the line to (4095, 4095) from the point Y[k - 1] (clamped to 4095).  b[0 .. 13] = Y[1 .. 14].
 */
static inline void t32_tmo_adr_map_y_filter(s16 *xs, u32 *b, int iters, int idx)
{
	u32 Y[16], S[15];
	long long tot = 0, sum;
	int k, it, j;

	iters = (s16)iters;
	idx = (s16)idx;
	xs[15] = 4095;
	Y[0] = 0;
	Y[15] = 4095;
	for (k = 1; k <= 15; k++) {
		if (k != 15)
			Y[k] = b[k - 1];
		S[k - 1] = t32_tmo_slope((int)(Y[k] - Y[k - 1]), xs[k] - xs[k - 1]);
		tot += (int)S[k - 1];
	}
	for (it = 0; it < iters; it++) {
		u64 half;

		sum = (int)S[0];
		for (j = 1; j <= 13; j++) {
			S[j] = (u32)t32_tmo_sdiv((int)(S[j - 1] + S[j] + S[j + 1] + 1U), 3);
			sum += (int)S[j];
		}
		sum += (int)S[14];
		half = (u64)sum >> 1;
		for (k = 1; k <= 15; k++) {
			u32 y;
			int dx = xs[k] - xs[k - 1];

			if (idx < k) {
				int d = 4095 - xs[k - 1];

				S[k - 1] = (u32)t32_tmo_sdiv0((int)((u32)t32_tmo_sdiv(d, 2) + (4095U - Y[k - 1]) * 10000U), d);
			} else {
				u64 n = (u64)(long long)(int)S[k - 1] * (u64)tot + half;

				S[k - 1] = (u32)t32_awb_fixdiv64(0, n, (u64)sum);
			}
			y = (u32)t32_tmo_sdiv((int)((u32)dx * S[k - 1] + 5000U), 10000) + Y[k - 1];
			Y[k] = (int)y < 4096 ? y : 4095U;
		}
	}
	for (k = 0; k < 14; k++)
		b[k] = Y[k + 1];
}

/*
 * tisp_tmo_adr_map_y_filter_sp: the same on the curve resampled to 33 points: c0444 = vendor t32_data_c0444 (33 s16 positions);
 * Y = interp1_short (xs, ys, c0444) with ys = (0, b[0 .. 13], 4095); 32 slopes; iters passes (smoothing s[1 .. 30], sum with s[0] and
 * s[31]; every slope rebuilt from the stretched sum, no line to 4095 here), Y' = low 16 bits of the rebuilt value or 4095 when its
 * s16 value is >= 4096; finally the 16 positions xs are interpolated back (interp1_short (c0444, Y, xs)) into ys and
 * b[0 .. 13] = sign extended ys[1 .. 14].
 */
static inline void t32_tmo_adr_map_y_filter_sp(s16 *xs, u32 *b, int iters, const s16 *c0444)
{
	s16 in[33], ys[16], out[33];
	u32 S[32];
	long long tot = 0, sum;
	int k, it, j;

	iters = (s16)iters;
	memcpy(in, c0444, sizeof(in));
	ys[15] = 4095;
	xs[15] = 4095;
	ys[0] = 0;
	for (k = 1; k < 15; k++)
		ys[k] = (s16)b[k - 1];
	t32_tmo_adr_interp1_short(xs, ys, in, out, 16, 33);
	for (k = 0; k < 32; k++) {
		S[k] = t32_tmo_slope(out[k + 1] - out[k], in[k + 1] - in[k]);
		tot += (int)S[k];
	}
	for (it = 0; it < iters; it++) {
		u64 half;

		sum = (int)S[0];
		for (j = 1; j <= 30; j++) {
			S[j] = (u32)t32_tmo_sdiv((int)(S[j - 1] + S[j] + S[j + 1] + 1U), 3);
			sum += (int)S[j];
		}
		sum += (int)S[31];
		half = (u64)sum >> 1;
		for (k = 0; k < 32; k++) {
			u32 v;

			S[k] = (u32)t32_awb_fixdiv64(0, (u64)(long long)(int)S[k] * (u64)tot + half, (u64)sum);
			v = (u32)t32_tmo_sdiv((int)((u32)(in[k + 1] - in[k]) * S[k] + 5000U), 10000) + (u32)(u16)out[k];
			out[k + 1] = (s16)v < 4096 ? (s16)v : 4095;
		}
	}
	t32_tmo_adr_interp1_short(in, out, xs, ys, 33, 16);
	for (k = 0; k < 14; k++)
		b[k] = (u32)(int)ys[k + 1];
}

/*
 * ---- phase 5b: adr_map_curve1 ----
 *
 * tisp_tmo_adr_map_curve1 (out = 14 u32, a1 / a2 limited to 0 .. 255 (curve index / position m), d8324 = the vendor table
 * t32_data_d8324 of 256 s32): k = d8324[a1]; for i = 0 .. 32 the 33 samples Q[i] = x = 8 i (k >= 0) or 8 (32 - i) (k < 0),
 * a = 256 - m - x, W = a^3 (256 - m) + m^4, N = W * v1, D = v0 * m^3, with (v1, v0) = ((k + 256) x, k + x) for k >= 0 or ((256 - k) x, x - k)
 * for k < 0 (64 bit wrapping products as the vendor's register pairs), q = N / D by the unsigned fix_point_div (D == 0: 0, see
 * fixdiv64), clamped to 0x10000 (a high word or a low word above 0x10000), for k < 0 mirrored (0x10000 - q).  out[0 .. 5] are
 * the first 3 samples blended ((15 Q0 + Q1) / 1048560, (7 Q0 + Q1) / 524280, (3 Q0 + Q1) / 262140, (Q0 + Q1) / 131070,
 * Q1 / 65535, (Q1 + Q2) / 131070, all * 4095), out[6 .. 13] = Q[2, 3, 4, 6, 8, 12, 16, 24] * 4095 / 65535.
 */
static inline void t32_tmo_adr_map_curve1(u32 *out, int a1, int a2, const int *d8324)
{
	static const u8 qi[8] = { 2, 3, 4, 6, 8, 12, 16, 24 };
	u32 Q[33], m, s8, s5, s6;
	int k, i;

	a1 = a1 < 0 ? 0 : a1 < 256 ? a1 : 255;
	a2 = a2 < 0 ? 0 : a2 < 256 ? a2 : 255;
	m = (u32)a2;
	s5 = m * m;
	s6 = m * s5;
	s8 = 256U - m;
	k = d8324[a1];
	for (i = 0; i < 33; i++) {
		u32 x = (u32)(k >= 0 ? i : 32 - i) << 3, a3 = 256U - (m + x), v0, v1, lo;
		u64 W, N, q;

		if (k >= 0) {
			v1 = (u32)(k + 256) * x;
			v0 = (u32)k + x;
		} else {
			v1 = (u32)(256 - k) * x;
			v0 = x - (u32)k;
		}
		W = (u64)((long long)(int)a3 * (int)a3);
		W *= (u64)(long long)(int)s8;
		W *= (u64)(long long)(int)a3;
		W += (u64)((long long)(int)s5 * (int)s5);
		N = W * (u64)(long long)(int)v1;
		q = t32_awb_fixdiv64(0, N, (u64)((long long)(int)v0 * (int)s6));
		lo = (q >> 32) || (u32)q > 0x10000U ? 0x10000U : (u32)q;
		Q[i] = k >= 0 ? lo : 0x10000U - lo;
	}
	out[0] = (u32)t32_tmo_sdiv((int)((15U * Q[0] + Q[1]) * 4095U), 1048560);
	out[1] = (u32)t32_tmo_sdiv((int)((7U * Q[0] + Q[1]) * 4095U), 524280);
	out[2] = (u32)t32_tmo_sdiv((int)((3U * Q[0] + Q[1]) * 4095U), 262140);
	out[3] = (u32)t32_tmo_sdiv((int)((Q[0] + Q[1]) * 4095U), 131070);
	out[4] = (u32)t32_tmo_sdiv((int)(Q[1] * 4095U), 65535);
	out[5] = (u32)t32_tmo_sdiv((int)((Q[1] + Q[2]) * 4095U), 131070);
	for (i = 0; i < 8; i++)
		out[6 + i] = (u32)t32_tmo_sdiv((int)(Q[qi[i]] * 4095U), 65535);
}

/*
 * tisp_tmo_adr_ev_func (phase 5c): evaluate the context's ev dependent curves at the current ev point.
 * seed[] is t32_data_e3530 (u64 per channel; the first two entries are set to 2000 here, the vendor does it on every
 * call), cur[] t32_data_e3550, pend[] t32_data_e3528 (s16 per channel), ev the block at e35b0[ch] + 4 (+0x18 ..: eight
 * rows of 11 s16, 22 bytes apart).  Channel ch: cur[ch] = seed[ch]; only a pending flag of 1 goes on (and clears it).
 * The 64 bit point x = cur[ch] is looked up (adr_ispint_adr_64, 11 points, x axis ctx + 800) on 11 point y tables of the
 * context and the u16 results stored.  Selector s16 ctx[9574]: 2 = the eight rows of ev are filled from ctx and looked
 * up into ctx[8826..8840] (plus the four of selector 0), 0 = four lookups into ctx[8314..8320], else those four copied
 * from ctx[9576..9582].  ctx[9846] (0: two lookups into ctx[8322 / 8324], else copied from ctx[9848 / 9850]);
 * ctx[9896] (0: 13 lookups into ctx[8336..8356, 8328, 8330], else copied from ctx[9866..9890]); ctx[8332 / 8334] are
 * cleared; ctx[9268] (0: 14 lookups into ctx[8798..8824], else copied from ctx[9240..9266]).  The return value is only
 * leftover registers in the vendor code (not reproduced); the vendor's delay slot load of ctx[9576] on the
 * selector 0 path is dead.
 */
static inline void t32_tmo_adr_ev_func(int ch, u8 *c, u8 *ev, u64 *seed, u64 *cur, s16 *pend)
{
#define EV_S16(o) (*(s16 *)(c + (o)))
#define EV_U16(o) (*(u16 *)(c + (o)))
#define EV_LK(ys) ((u16)t32_tmo_adr_ispint_64((long long)cur[ch], (const long long *)(c + 800), (const s16 *)(c + (ys)), 11))
	int i;

	seed[0] = 2000;
	seed[1] = 2000;
	cur[ch] = seed[ch];
	if (pend[ch] != 1)
		return;
	pend[ch] = 0;
	for (i = 0; i < 8; i++)
		EV_U16(8826 + 2 * i) = 0;
	if (EV_S16(9574) == 2) {
		for (i = 0; i < 11; i++) {
			*(u16 *)(ev + 24 + 2 * i) = (u16)EV_S16(8266 + 2 * i);
			*(u16 *)(ev + 46 + 2 * i) = (u16)EV_S16(8290 + 2 * i);
			*(u16 *)(ev + 68 + 2 * i) = (u16)EV_S16(8776 + 2 * i);
			*(u16 *)(ev + 90 + 2 * i) = (u16)EV_S16(9194 + 2 * i);
			*(u16 *)(ev + 112 + 2 * i) = (u16)EV_S16(9216 + 2 * i);
			*(u16 *)(ev + 134 + 2 * i) = (u16)EV_S16(9530 + 2 * i);
			*(u16 *)(ev + 156 + 2 * i) = (u16)EV_S16(9552 + 2 * i);
			*(u16 *)(ev + 178 + 2 * i) = (u16)EV_S16(9678 + 2 * i);
		}
		EV_U16(8314) = EV_LK(8358);
		EV_U16(8316) = EV_LK(8380);
		EV_U16(8318) = EV_LK(8402);
		EV_U16(8320) = EV_LK(8424);
		for (i = 0; i < 8; i++)
			EV_U16(8826 + 2 * i) = (u16)t32_tmo_adr_ispint_64((long long)cur[ch], (const long long *)(c + 800),
									   (const s16 *)(ev + 24 + 22 * i), 11);
	} else if (EV_S16(9574) != 0) {
		EV_U16(8314) = EV_U16(9576);
		EV_U16(8316) = EV_U16(9578);
		EV_U16(8318) = EV_U16(9580);
		EV_U16(8320) = EV_U16(9582);
	} else {
		EV_U16(8314) = EV_LK(8358);
		EV_U16(8316) = EV_LK(8380);
		EV_U16(8318) = EV_LK(8402);
		EV_U16(8320) = EV_LK(8424);
	}
	if (EV_S16(9846) != 0) {
		EV_U16(8322) = EV_U16(9848);
		EV_U16(8324) = EV_U16(9850);
	} else {
		EV_U16(8322) = EV_LK(9150);
		EV_U16(8324) = EV_LK(9172);
	}
	if (EV_S16(9896) != 0) {
		for (i = 0; i < 11; i++)
			EV_U16(8336 + 2 * i) = EV_U16(9866 + 2 * i);
		EV_U16(8328) = EV_U16(9888);
		EV_U16(8330) = EV_U16(9890);
	} else {
		for (i = 0; i < 11; i++)
			EV_U16(8336 + 2 * i) = EV_LK(8446 + 22 * i);
		EV_U16(8328) = EV_LK(8688);
		EV_U16(8330) = EV_LK(8710);
	}
	EV_U16(8332) = 0;
	EV_U16(8334) = 0;
	if (EV_S16(9268) != 0) {
		for (i = 0; i < 14; i++)
			EV_U16(8798 + 2 * i) = EV_U16(9240 + 2 * i);
	} else {
		for (i = 0; i < 14; i++)
			EV_U16(8798 + 2 * i) = EV_LK(8842 + 22 * i);
	}
#undef EV_S16
#undef EV_U16
#undef EV_LK
}

/*
 * tisp_adr_faceae_curve_adjust (phase 6a): the face ae curve table.  blk = *(e35b0[ch] + 0); the 24 rows of 14 s16 at
 * blk + 128 are raised to the s16 maximum with row i of tab[] (t32_data_e3248) when en[i] (t32_data_e34e8) is 1.
 * *cnt (t32_data_e3244) counts the calls and wraps to 0 at 100001.  The vendor return value is leftover registers.
 */
static inline void t32_adr_faceae_curve_adjust(u8 *blk, u32 *cnt, const u8 *en, const s16 *tab)
{
	u32 n = *cnt + 1U;
	int i, j;

	*cnt = n < 100001U ? n : 0U;
	for (i = 0; i < 24; i++) {
		s16 *row = (s16 *)(blk + 128) + 14 * i;

		if (en[i] != 1)
			continue;
		for (j = 0; j < 14; j++)
			if (!(tab[14 * i + j] < row[j]))
				row[j] = tab[14 * i + j];
	}
}

/*
 * tisp_tmo_adr_subsection_up (phase 6a): the 11 u32 curve points out[] of the subsection.  Fixed point with sh fraction
 * bits (the shifts use the low 5 bits, fixdiv / fixmul get the full count), R(v) = (v + 512) >> sh (the vendor's constant
 * rounding), y = x -> y lookup of the 129 point curve (x[] -> y[], an interpolation from the point above v, the point
 * below it is read at k - 1: x[-1] / y[-1] for a hit on the first point, vendor read before the table; y[128] above the
 * last).  tab[] is the 512 word table of adr_subsection_map (also read at R(..) of the ratios out / mul, any index);
 * mul is the divisor scale (a zero mul divides by zero in fix_point_div_32: the vendor gets 0xffffffff, so do we).
 * The chain: out[6] = lookup(map(5000, R(4095 / 2))), then out[4], out[2], out[5], out[1], out[3] from maps of the half
 * sums of tab[] at the previous outputs; out[7..9] = out[6] + 1..3 (all four set to 4091..4094 when out[6] + 3 >= 4095).
 * Dropped dead code: the vendor limit "out[10] >= 4096 -> 4095" (out[10] = 4095 here).  The vendor return value is
 * leftover registers.  The vendor's s5 / s6 / s8 temporaries are named below as in its call order.
 */
static inline int t32_tmo_up_lookup(int v, const s16 *x, const s16 *y, u32 sh)
{
	int k;

	for (k = 0; k < 129; k++) {
		if (v < x[k]) {
			u32 slope = t32_awb_fixdiv(sh, (u32)(y[k] - y[k - 1]) << (sh & 31U), (u32)(x[k] - x[k - 1]) << (sh & 31U));
			u32 m = t32_awb_fixmul(sh, slope, (u32)(x[k] - v) << (sh & 31U));

			return y[k] - ((int)(m + 512U) >> (sh & 31U));
		}
	}
	return y[128];
}

static inline void t32_tmo_adr_subsection_up(u32 *out, int s1, const s16 *y, const s16 *x, const int *tab, int mul, u32 sh,
					     u32 sh0, u32 mode)
{
#define UP_R(v) ((int)((u32)(v) + 512U) >> (sh & 31U))
#define UP_SM(pos, s5) t32_tmo_adr_subsection_map((pos), (s5), s1, y, x, tab, mul, sh, sh0, mode)
#define UP_LK(v) t32_tmo_up_lookup((v), x, y, sh)
#define UP_HALF(i) t32_tmo_sdiv(tab[i], 2)
	u32 two = 2U << (sh & 31U), s6;
	int s8, s5, o6, o4, o2, o5, o1, o3, e, ia, ib, t;

	out[10] = 4095;
	out[0] = 0;
	s8 = UP_SM(5000, UP_R(t32_awb_fixdiv(sh, 4095U << (sh & 31U), two)));
	o6 = UP_LK(s8);
	out[6] = (u32)o6;
	s6 = (u32)mul << (sh & 31U);
	t = UP_HALF(UP_R(t32_awb_fixdiv(sh, (u32)o6 << (sh & 31U), s6)));
	s5 = UP_SM(t, UP_R(t32_awb_fixdiv(sh, (u32)s8 << (sh & 31U), two)));
	o4 = UP_LK(s5);
	out[4] = (u32)o4;
	t = UP_HALF(UP_R(t32_awb_fixdiv(sh, (u32)o4 << (sh & 31U), s6)));
	e = UP_SM(t, UP_R(t32_awb_fixdiv(sh, (u32)s5 << (sh & 31U), two)));
	o2 = UP_LK(e);
	out[2] = (u32)o2;
	ia = UP_R(t32_awb_fixdiv(sh, (u32)o4 << (sh & 31U), s6));
	ib = UP_R(t32_awb_fixdiv(sh, (u32)o6 << (sh & 31U), s6));
	t = t32_tmo_sdiv((int)((u32)tab[ib] + (u32)tab[ia]), 2);
	o5 = UP_LK(UP_SM(t, UP_R(t32_awb_fixdiv(sh, (u32)(s8 + s5) << (sh & 31U), two))));
	out[5] = (u32)o5;
	t = UP_HALF(UP_R(t32_awb_fixdiv(sh, (u32)o2 << (sh & 31U), s6)));
	o1 = UP_LK(UP_SM(t, UP_R(t32_awb_fixdiv(sh, (u32)e << (sh & 31U), two))));
	out[1] = (u32)o1;
	ia = UP_R(t32_awb_fixdiv(sh, (u32)o2 << (sh & 31U), s6));
	ib = UP_R(t32_awb_fixdiv(sh, (u32)o4 << (sh & 31U), s6));
	t = t32_tmo_sdiv((int)((u32)tab[ib] + (u32)tab[ia]), 2);
	o3 = UP_LK(UP_SM(t, UP_R(t32_awb_fixdiv(sh, (u32)(s5 + e) << (sh & 31U), two))));
	out[3] = (u32)o3;
	out[7] = (u32)o6 + 1U;
	out[8] = (u32)o6 + 2U;
	out[9] = (u32)o6 + 3U;
	if (!((int)out[9] < 4095)) {
		out[6] = 4091;
		out[7] = 4092;
		out[8] = 4093;
		out[9] = 4094;
	}
#undef UP_R
#undef UP_SM
#undef UP_LK
#undef UP_HALF
}

/* ---- phase 7a: the small api / update functions ---- */

/*
 * tisp_tmo_tgain_update (a0 = channel, a1 unused, a2, a3): rec36 = rec + 36 gets a2, the channel's flag (t32_data_e3520[ch])
 * is set to 1, t32_data_e3590[ch] = a3 << 16 | a2 >> 16, t32_data_e35a0[ch] = a2 & 0xffff.
 */
static inline void t32_tmo_tgain_update(u32 *rec36, u16 *e3520, u32 *e3590, u32 *e35a0, u32 a2, u32 a3)
{
	*rec36 = a2;
	*e3520 = 1;
	*e3590 = (a3 << 16) | (a2 >> 16);
	*e35a0 = a2 & 0xffffU;
}

/*
 * tisp_tmo_api_strength_ctrl: record +54 (u16) = 1, +56 (u16) = strength; when the context's byte 10042 is 1 the
 * register refresh of the ramY set (vi1 when the global byte t32_data_d03a0 is 1, else vi0) runs for the channel (a tail
 * call in the vendor: refresh(ch) is the hook; the vendor's return value is only leftover registers).
 */
static inline void t32_tmo_api_strength_ctrl(u16 *rec54, const u8 *ctx, u8 d03a0, u32 strength, void (*vi1)(void *, int),
					     void (*vi0)(void *, int), void *cb, int ch)
{
	rec54[0] = 1;
	rec54[1] = (u16)strength;
	if (ctx[10042] != 1)
		return;
	if (d03a0 == 1)
		vi1(cb, ch);
	else
		vi0(cb, ch);
}

/*
 * tisp_tmo_param_array_get: the whole parameter set of a channel into buf: the 11000 byte context, the 402 byte out
 * curve (record +8) at +11000 and the 800 bytes at e35b0[ch]->+0 at +11402; *size = 12202.  Returns 0.
 */
static inline int t32_tmo_param_array_get(u8 *buf, const u8 *ctx, const u8 *out, const u8 *blk, u32 *size)
{
	memcpy(buf, ctx, 11000);
	memcpy(buf + 11000, out, 402);
	memcpy(buf + 11402, blk, 800);
	*size = 12202;
	return 0;
}

/*
 * tisp_tmo_api_get_curve: the 402 byte curve idx (< 10) of the channel's context is copied to dst.  The vendor selects it
 * through the jump table t32_data_c04c8 (driver/t32/data_relocations.json, .rodata 0x30a8: labels 0x9e948 + 16 * idx):
 * idx 0 -> ctx + 7398, idx 1 -> ctx + 7800, idx 2..9 -> ctx + 4182 + 402 * (idx - 2), i.e. the same curve order as
 * t32_tmo_ev_interp (C[0] = +7398, C[1] = +7800, C[i] = +4182 + 402 (i - 2)).  The return value of the vendor is the
 * memcpy result (dst) or leftover registers.
 */
static inline void t32_tmo_api_get_curve(u8 *dst, const u8 *ctx, u32 idx)
{
	if (idx < 10)
		memcpy(dst, ctx + (idx == 0 ? 7398 : idx == 1 ? 7800 : 4182 + 402 * (idx - 2)), 402);
}

/*
 * tisp_tmo_ev_update (ch, a2, a3, arg4): the record's ev values (rec +24 = low word of the 64 bit ev point a3:a2 >> 10 ...,
 * +28 = a3 >> 10, +32 = arg4), then ev_interp, dark_ctr_interp (st + 238), satcurve_ev_interp, the adr default register
 * refresh and, when the stat byte 385 is 1, the tmo default register refresh (hooks); finally the channel's pending flag
 * (e3528[ch]) is set and seed[ch] (e3530) = { a2 >> 10 | a3 << 22, a3 >> 10 }.  Return value: leftover registers.
 */
static inline void t32_tmo_ev_update(struct t32_tmo_rec *r, u32 a2, u32 a3, u32 arg4, s16 *pend, u32 *seed,
				     void (*adr_refresh)(void *, int), void (*tmo_refresh)(void *, int), void *cb, int ch)
{
	u32 lo = a2 >> 10, hi = a3 >> 10, flag = r->st[385];

	r->ev1 = hi;
	r->ev1m = arg4;
	lo |= a3 << 22;
	r->ev0 = lo;
	t32_tmo_ev_interp(r);
	t32_tmo_dark_ctr_interp(r->ctx, (struct t32_tmo_dark_out *)(r->st + 238), r->ev0, r->ev1, r->ev1m);
	t32_tmo_satcurve_ev_interp(r->ctx, r->st, r->ev0, r->ev1, r->ev1m);
	adr_refresh(cb, ch);
	if (flag == 1)
		tmo_refresh(cb, ch);
	*pend = 1;
	seed[0] = lo;
	seed[1] = hi;
}

/* ---- phase 8a: glue functions (callees of the register refresh set are hooks) ---- */

/* tisp_adr_faceae_refresh: e3500 = flag, the 24 enable bytes (e34e8) and the 336 s16 table (e3248) are replaced.  The vendor returns the memcpy result. */
static inline void t32_adr_faceae_refresh(u8 *e3500, u32 flag, u8 *e34e8, const u8 *en, s16 *e3248, const s16 *tab)
{
	*e3500 = (u8)flag;
	memcpy(e34e8, en, 24);
	memcpy(e3248, tab, 672);
}

/*
 * tisp_tmo_fbf_interrupt_calc (ch): with the stat byte 385 of channel ch set to 1, tmo_soft_process of the current
 * channel cur (t32_data_e3504) runs first.  Then, always, the record of cur: u16 +46 =
 * (+46 + 1) & 0xffff, or 1 when that is 2 or more, u16 +44 = u16 +50 = 0, t32_data_d8b34 = 1, returns 1.
 */
static inline int t32_tmo_fbf_interrupt_calc(const u8 *st_ch, u8 *rec_cur, u32 *d8b34, void (*soft)(void *, u32), void *cb, u32 cur)
{
	u32 n;

	if (st_ch[385] == 1)
		soft(cb, cur);
	n = ((u32)*(u16 *)(rec_cur + 46) + 1U) & 0xffffU;
	if (!(n < 2U))
		n = 1;
	*(u16 *)(rec_cur + 46) = (u16)n;
	*(u16 *)(rec_cur + 44) = 0;
	*(u16 *)(rec_cur + 50) = 0;
	*d8b34 = 1;
	return 1;
}

/*
 * tisp_tmo_tmo_soft_process (ch): the first time (flag byte t32_data_e3510[ch] 0) the 16 bytes ctx + 2514 are copied to
 * st + 340, tmo_default_reg_refresh runs and the flag is set.  Then tmo_stat_limit and the register refreshes ramY / ramV
 * of the set selected by the byte t32_data_d03a0 (1: vi1, else vi0) (hooks: ids 0 default, 1 stat_limit, 2 / 3 ramY / ramV vi1,
 * 4 / 5 ramY / ramV vi0).
 */
static inline void t32_tmo_tmo_soft_process(u8 *flag, const u8 *ctx, u8 *st, u8 d03a0, void (*hook)(void *, int, int), void *cb, int ch)
{
	if (*flag == 0) {
		memcpy(st + 340, ctx + 2514, 16);
		hook(cb, 0, ch);
		*flag = 1;
	}
	hook(cb, 1, ch);
	if (d03a0 == 1) {
		hook(cb, 2, ch);
		hook(cb, 3, ch);
	} else {
		hook(cb, 4, ch);
		hook(cb, 5, ch);
	}
}

/*
 * tisp_tmo_adr_soft_process (ch): adr_stat_calc, adr_ev_func, adr_tgain_func, adr_algorithm, adr_reg_write_every in this order
 * (hook ids 0 .. 4).  Dropped: the vendor's isp_printf every 1000th call (counter t32_data_e3516, a u16 only read here).
 */
static inline void t32_tmo_adr_soft_process(void (*hook)(void *, int, int), void *cb, int ch)
{
	int i;

	for (i = 0; i < 5; i++)
		hook(cb, i, ch);
}

#endif /* TX_ISP_T32_TMO_H */

/* ---- phase 9a: param_array_set, api_set_curve, wake_up (unported callees are hooks) ---- */

/*
 * Environment of the shared refresh tail of tisp_tmo_param_array_set / tisp_tmo_api_set_curve.  The register refresh
 * callees that are not ported yet and tisp_lcac_no_refresh are hooks, ids: 0 tmoadr_default_reg_refresh, 1 lcac_no_refresh,
 * 2 tmo_default_reg_refresh, 3 ramY_vi1_reg_refresh, 4 ramY_vi0_reg_refresh, 5 adr_reg_write_sometimes (all take the channel).
 */
struct t32_tmo_set_env {
	struct t32_tmo_rec *r;		/* the channel record: ctx, st, ev, ... (t32_data_e35e0[ch]) */
	u16 *rec50;			/* record +50 (u16) */
	u8 *rec52;			/* record +52 (u8) */
	u16 *pend;			/* t32_data_e3528[ch] */
	u16 *e3520;			/* t32_data_e3520[ch] */
	u16 *e3518;			/* t32_data_e3518[ch] (adr_hardpars_ctl flag) */
	const u8 *hard_tab;		/* t32_data_bff90 (adr_hardpars_ctl tables) */
	int *gauss_out;			/* t32_data_e31b0 (24 int) */
	s16 *gauss_cache;		/* t32_data_e3210 */
	const int *gauss_tab;		/* t32_data_c00bc */
	t32_tmo_exp2_fn exp2;
	u8 d03a0;
	void (*hook)(void *cb, int id, int ch);
	void *cb;
};

/*
 * The refresh the two setters end with: s4 = ctx byte 10042 (read first); tmo_params_hard_refresh, dark_ctr_interp (st + 238),
 * satcurve_ev_interp, the adr default register refresh and lcac_no_refresh.  s4 == 1: ev_interp, tmo default register
 * refresh, the ramY refresh (vi1 when d03a0 == 1, else vi0), and a record byte 52 == 0 clears the u16 at +50.  s4 == 0:
 * the channel's pending flag and e3520 = 1, adr_hardpars_ctl, adr_reg_write_sometimes, adr_gauss_local (e31b0).  Other
 * s4: nothing.  At the end record byte 52 = stat byte 385.  Dropped: the vendor's isp_printf (s4 == 0 only).
 */
static inline void t32_tmo_set_refresh(struct t32_tmo_set_env *e, int ch)
{
	struct t32_tmo_rec *r = e->r;
	u8 s4 = r->ctx[10042];

	t32_tmo_params_hard_refresh(r->ctx, r->st);
	t32_tmo_dark_ctr_interp(r->ctx, (struct t32_tmo_dark_out *)(r->st + 238), r->ev0, r->ev1, r->ev1m);
	t32_tmo_satcurve_ev_interp(r->ctx, r->st, r->ev0, r->ev1, r->ev1m);
	e->hook(e->cb, 0, ch);
	e->hook(e->cb, 1, ch);
	if (s4 == 1) {
		t32_tmo_ev_interp(r);
		e->hook(e->cb, 2, ch);
		e->hook(e->cb, e->d03a0 != s4 ? 4 : 3, ch);
		if (*e->rec52 == 0)
			*e->rec50 = 0;
	} else if (s4 == 0) {
		*e->pend = 1;
		*e->e3520 = 1;
		t32_tmo_adr_hardpars_ctl(r->ctx, e->hard_tab, e->e3518);
		e->hook(e->cb, 5, ch);
		t32_tmo_adr_gauss_local(e->gauss_out, r->ctx, e->gauss_cache, e->gauss_tab, e->exp2);
	}
	*e->rec52 = r->st[385];
}

/* tisp_tmo_param_array_set (ch, src): the 11000 byte context is replaced by src, then the refresh above. */
static inline void t32_tmo_param_array_set(struct t32_tmo_set_env *e, const u8 *src, int ch)
{
	memcpy(e->r->ctx, src, 11000);
	t32_tmo_set_refresh(e, ch);
}

/*
 * tisp_tmo_api_set_curve (ch & 0xff, idx = a1 & 0xff, src): idx < 10: the 402 byte curve idx of the context is replaced by
 * src (jump table t32_data_c04f0, driver/t32/data_relocations.json .rodata 0x30d0: labels 0x9ea50 + .. in the order
 * ctx + 7398, + 7800, then + 4182 + 402 (idx - 2)); idx >= 10 copies nothing.  Then the same refresh.
 */
static inline void t32_tmo_api_set_curve(struct t32_tmo_set_env *e, u32 idx, const u8 *src, int ch)
{
	idx &= 0xffU;
	if (idx < 10)
		memcpy(e->r->ctx + (idx == 0 ? 7398 : idx == 1 ? 7800 : 4182 + 402 * (idx - 2)), src, 402);
	t32_tmo_set_refresh(e, ch & 0xff);
}

/*
 * tisp_tmo_wake_up (ch): five system register writes (d03a0 != 0 selects the second register set: the first value
 * is the base word reg = record +8 of t32_data_e35d0[ch] (a u32 read there), reg + 0x4000, reg + 0x8000, reg + 0xc000, 3 to
 * 7786..7790 (d03a0 != 0) or 28928 + 4 i (else)), hooks 0 / 1 (adr default refresh, lcac_no_refresh), when the context
 * byte 10042 is 1 the hooks 2 tmo2fbf_process, 3 tmo_default_reg_refresh, 4 tmo_soft_process; byte != 0: end.  Else the hooks
 * 5 adr_reg_write_one, 6 adr_reg_write_5x5, 7 adr_reg_write_sometimes and 8 adr_reg_write_every (a tail call).
 */
static inline void t32_tmo_wake_up(const u8 *ctx, u32 reg, u8 d03a0, void (*regw)(void *, u32, u32), void (*hook)(void *, int, int),
				   void *cb, int ch)
{
	static const u16 a1[5] = { 7786, 7787, 7788, 7789, 7790 }, a0[5] = { 28928, 28932, 28936, 28940, 28944 };
	u32 val[5] = { reg, reg + 16384, reg + 0x8000, reg + 0xc000, 3 };
	int i;

	for (i = 0; i < 5; i++)
		regw(cb, d03a0 != 0 ? a1[i] : a0[i], val[i]);
	hook(cb, 0, ch);
	hook(cb, 1, ch);
	if (ctx[10042] == 1) {
		hook(cb, 2, ch);
		hook(cb, 3, ch);
		hook(cb, 4, ch);
	}
	if (ctx[10042] != 0)
		return;
	hook(cb, 5, ch);
	hook(cb, 6, ch);
	hook(cb, 7, ch);
	hook(cb, 8, ch);
}

/* ---- phase 9b: malloc_cfg, fbf_malloc_cfg, faceae_refresh (allocators and the register write are callbacks) ---- */

/*
 * tisp_tmo_malloc_cfg (ch): rec = private_vmalloc (12) is stored in the channel slot (t32_data_e35d0[ch], a 32 bit address); buf =
 * private_kmalloc (0x10000, 0x24000c0); the registers (second set when d03a0 != 0: 7786 + i, else 28928 + 4 i) get tisp_reg_write
 * (ch, reg, value) = phys 0x80000000 + buf, + 0x4000, + 0x8000, + 0xc000, in this order; rec = { 4, buf, buf + 0x80000000 };
 * finally register 7790 / 28944 = 3 (a tail call in the vendor).  Dropped: the vendor's isp_printf when an allocation failed;
 * a NULL vmalloc result is a NULL dereference in the vendor (not tested, unchanged here); a NULL kmalloc result goes on with buf = 0.
 */
static inline void t32_tmo_malloc_cfg(u32 *slot, int ch, void *(*vmalloc)(void *, u32), void *(*kmalloc)(void *, u32, u32),
				      void (*regw)(void *, int, u32, u32), u8 d03a0, void *cb)
{
	u32 *rec = vmalloc(cb, 12);
	u32 buf, phys;
	int s = d03a0 != 0;

	*slot = (u32)(uintptr_t)rec;
	buf = (u32)(uintptr_t)kmalloc(cb, 0x10000, 0x24000c0);
	phys = buf + 0x80000000U;
	regw(cb, ch, s ? 7786 : 28928, phys);
	regw(cb, ch, s ? 7787 : 28932, buf + 0x80004000U);
	regw(cb, ch, s ? 7788 : 28936, buf + 0x80008000U);
	regw(cb, ch, s ? 7789 : 28940, buf + 0x8000c000U);
	rec[0] = 4;
	rec[1] = buf;
	rec[2] = phys;
	regw(cb, ch, s ? 7790 : 28944, 3);
}

/*
 * tisp_tmo_fbf_malloc_cfg: blk = private_vmalloc (20) is stored in the slot (t32_data_e35c0[ch]); blk = { 1, cfg[1], cfg[2],
 * tbuf, tbuf + 0x80000000 } with cfg = the malloc_cfg record (t32_data_e35d0[ch]) and tbuf = the channel record's word +20.
 * Dropped: isp_printf (vmalloc failed / tbuf == 0).  A NULL vmalloc result is a NULL dereference in the vendor.
 */
static inline void t32_tmo_fbf_malloc_cfg(u32 *slot, const u32 *cfg, u32 tbuf, void *(*vmalloc)(void *, u32), void *cb)
{
	u32 *blk = vmalloc(cb, 20);

	*slot = (u32)(uintptr_t)blk;
	blk[0] = 1;
	blk[1] = cfg[1];
	blk[2] = cfg[2];
	blk[3] = tbuf;
	blk[4] = tbuf + 0x80000000U;
}

/*
 * tisp_tmo_faceae_refresh (ch, flag, a, b, c = arg4, d = arg5, e = arg6) on the stat record st: st[377] = flag; 12 bytes a -> st + 0, b -> st + 12,
 * c -> st + 24, e... (arg6) 6 bytes -> st + 371, d (arg5) 12 bytes -> st + 36; then tisp_tmo_faceae_reg_refresh (ch) (a tail call, hook).
 */
static inline void t32_tmo_faceae_refresh(u8 *st, u32 flag, const u8 *a, const u8 *b, const u8 *c, const u8 *d, const u8 *e,
					  void (*hook)(void *, int), void *cb, int ch)
{
	st[377] = (u8)flag;
	memcpy(st, a, 12);
	memcpy(st + 12, b, 12);
	memcpy(st + 24, c, 12);
	memcpy(st + 371, e, 6);
	memcpy(st + 36, d, 12);
	hook(cb, ch);
}
