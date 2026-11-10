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

#endif /* TX_ISP_T32_TMO_H */
