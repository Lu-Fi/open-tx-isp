/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AWB colour temperature detection (vendor Tiziano_Awb_Ct_* , 0x1db9c .. 0x20908):
 * the arithmetic helpers and, in later commits, the detection itself.  Each function
 * is checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by
 * its own oracle in tests/t32_oracle.
 *
 *   t32_awb_fixmul3          fix_point_mult3_32 0x1db9c         t32_awbm_oracle
 *   t32_awb_fixdiv           fix_point_div_32 0x1dbc8           t32_awbm_oracle
 *   t32_awb_interp1/2        ISPAWBInterpolation1/2 0x1fdd0/44  t32_awbi_oracle
 *   t32_awb_zone_ct_weight   func_zone_ct_weight 0x1ff94        t32_awbi_oracle
 *
 * Divisions by zero give 0xffffffff (the vendor's DIVU result as recovered, an
 * assumption of the recovered C).
 */
#ifndef TX_ISP_T32_AWBCT_H
#define TX_ISP_T32_AWBCT_H

#include "tx_isp_t32_awb.h"

/* fix_point_mult3_32: x * y * z in fixed point with s fraction bits */
static inline u32 t32_awb_fixmul3(u32 s, u32 x, u32 y, u32 z)
{
	return t32_awb_fixmul(s, t32_awb_fixmul(s, x, y), z);
}

/*
 * fix_point_div_32: num / den with s fraction bits, built as the integer quotient
 * shifted up and the s fraction bits by long division of the remainder.  The vendor
 * ORs the two parts, so quotient bits above 32 - s are lost (shift as __ashldi3,
 * count taken modulo 64).  The fraction loop runs s times; when the doubled remainder
 * equals the divisor the rest of the fraction is a single set bit.
 */
static inline u32 t32_awb_fixdiv(u32 s, u32 num, u32 den)
{
	u32 q = t32_awb_udiv(num, den);
	u32 r = num - den * q;
	u32 frac = 0, i = 0, qs = (s & 63) < 32 ? q << (s & 63) : 0;

	for (;;) {
		r <<= 1;
		if (i == s)
			break;
		frac <<= 1;
		if (den < r) {
			frac |= 1;
			r -= den;
		} else if (den == r) {
			frac |= 1;
			frac <<= ((s - 1 - i) & 31);
			break;
		}
		i++;
	}
	return frac | qs;
}

/*
 * ISPAWBInterpolation1 (x, x0, x1, y0, y1): linear interpolation of y at x where the
 * x axis is scaled by 2^s and y is returned scaled by 2^s as well:
 *   dx = (x1 - x0) << s;  y0 << s  +- (|y1 - y0| << s) * (x - (x0 << s)) / dx   (rounded)
 */
static inline u32 t32_awb_interp1(u32 s, u32 x, u32 x0, u32 x1, u32 y0, u32 y1)
{
	u32 dx = (x1 - x0) << (s & 31), base = y0 << (s & 31);
	u32 num = (((y1 < y0 ? y0 - y1 : y1 - y0)) << (s & 31)) * (x - (x0 << (s & 31))) + (dx >> 1);

	return y1 < y0 ? base - t32_awb_udiv(num, dx) : base + t32_awb_udiv(num, dx);
}

/* ISPAWBInterpolation2: the same with y unscaled */
static inline u32 t32_awb_interp2(u32 s, u32 x, u32 x0, u32 x1, u32 y0, u32 y1)
{
	u32 dx = (x1 - x0) << (s & 31);
	u32 num = (x - (x0 << (s & 31))) * (y1 < y0 ? y0 - y1 : y1 - y0) + (dx >> 1);

	return y1 < y0 ? y0 - t32_awb_udiv(num, dx) : y0 + t32_awb_udiv(num, dx);
}

/*
 * func_zone_ct_weight: weight of a statistics zone from its position x against four
 * thresholds th[0..3] (th[1] < x < th[3] is the ramp zone): outside, the weight is
 * base; between th[0] and th[2] it is 1.0 (1 << s); on the two ramps it is
 * interpolated from base to 1.0 (below: x from th[1] to th[0], above: x from th[3]
 * down to th[2]).  The weight is stored to *w and returned multiplied (fixed point)
 * into scale.
 */
static inline u32 t32_awb_zone_ct_weight(u32 x, const u32 th[4], u32 base, u32 s, u32 scale, u32 *w)
{
	u32 one = 1U << (s & 31), rest = one - base;

	if (!(x < th[3]) || !(th[1] < x)) {
		*w = base;
	} else if (th[2] < x) {			/* upper ramp */
		u32 span = th[3] - th[2];

		*w = t32_awb_udiv(base * span + (th[3] - x) * rest + (span >> 1), span);
	} else if (x < th[0]) {			/* lower ramp */
		u32 span = th[0] - th[1];

		*w = t32_awb_udiv(base * span + (x - th[1]) * rest + (span >> 1), span);
	} else {
		*w = one;
	}
	return t32_awb_fixmul(s, scale, *w);
}

/*
 * Tiziano_Awb_Ct_Cal (0x20054): colour temperature from the red and blue ratios
 * (r, b in fixed point with par->out_shift fraction bits).  Both ratios are limited to
 * the axis ranges par->ct_axis_r / ct_axis_b (first .. last entry, scaled).  The
 * cell of the 15x15 grid that contains them is found (index 15 = at or beyond the last
 * axis point) and the grid value interpolated linearly: inside the grid in r then b
 * (bilinear); at the last r point along b only; at the last b point along r only, using
 * the last row of the grid (row 14).  The vendor reads the grid / axes with raw offsets
 * so that index 15 reads the bytes behind the tables (they are part of the section).
 * The value is 1e6 / ct in fixed point: ct = 1e6 / value, rounded; 0 gives 5000.
 * The result goes to par->ct.
 */
static inline u32 t32_awb_axis_index(const u8 *raw, u32 axis_off, u32 v, u32 s)
{
	u32 i;

	for (i = 0; i < 14; i++)
		if (!(v < t32_awb_u32at(raw, axis_off + 4 * i) << s) &&
		    v < t32_awb_u32at(raw, axis_off + 4 * (i + 1)) << s)
			break;
	return i + 1;
}

static inline void t32_awb_ct_cal(struct t32_awb_par *par, u32 r, u32 b)
{
	const u8 *raw = par->raw;
	u32 fp = par->fp_shift, s = par->out_shift & 31;
	u32 rlo = par->ct_axis_r[0] << s, rhi = par->ct_axis_r[14] << s;
	u32 blo = par->ct_axis_b[0] << s, bhi = par->ct_axis_b[14] << s;
	u32 ir, ib, v, q;
	u32 x0, x1;

	(void)fp;
	if (r > rhi)
		r = rhi;
	else if (r < rlo)
		r = rlo;
	if (b > bhi)
		b = bhi;
	else if (b < blo)
		b = blo;

	ir = r == rhi ? 15 : t32_awb_axis_index(raw, 60, r, s);
	ib = b == bhi ? 15 : t32_awb_axis_index(raw, 120, b, s);

	if (ir == 15) {			/* along b at the last red point */
		x0 = t32_awb_u32at(raw, 120 + 4 * (ib - 1));
		x1 = t32_awb_u32at(raw, 120 + 4 * ib);
		v = t32_awb_interp1(s, b, x0, x1, t32_awb_u32at(raw, 2164 + 60 * ib - 4),
				    t32_awb_u32at(raw, 2164 + 60 * ib + 56));
	} else if (ib == 15) {		/* along r in the last row (row 14) */
		x0 = t32_awb_u32at(raw, 60 + 4 * (ir - 1));
		x1 = t32_awb_u32at(raw, 60 + 4 * ir);
		v = t32_awb_interp1(s, r, x0, x1, t32_awb_u32at(raw, 2164 + 4 * (ir - 1 + 210)),
				    t32_awb_u32at(raw, 2164 + 4 * (ir - 1 + 211)));
	} else {			/* bilinear */
		u32 cell = 2164 + 4 * (15 * (ib - 1) + (ir - 1)), lo, hi;

		x0 = t32_awb_u32at(raw, 60 + 4 * (ir - 1));
		x1 = t32_awb_u32at(raw, 60 + 4 * ir);
		lo = t32_awb_interp1(s, r, x0, x1, t32_awb_u32at(raw, cell), t32_awb_u32at(raw, cell + 4));
		hi = t32_awb_interp1(s, r, x0, x1, t32_awb_u32at(raw, cell + 60), t32_awb_u32at(raw, cell + 64));
		v = t32_awb_interp2(s, b, t32_awb_u32at(raw, 120 + 4 * (ib - 1)), t32_awb_u32at(raw, 120 + 4 * ib), lo, hi);
	}
	if (!v) {
		par->ct = 5000;
		return;
	}
	q = t32_awb_fixdiv(par->out_shift, 1000000U << s, v);	/* the vendor passes the unmasked shift */
	par->ct = (q + (1U << ((par->out_shift - 1) & 31))) >> s;
}

/*
 * Tiziano_Awb_Ct_Detect_GrayWorld_mode (0x20360) and Tiziano_Awb_Ct_Detect_GrayWorld
 * (0x20908): colour temperature by the gray world assumption.  stats holds, per mode,
 * 2048 words: the red sum of zone (row, col) at row * 32 + col and the blue sum 1024
 * words further.  Mode m runs when par->gw_en[m] == 1 and *valid == 1.
 *  1. weight of each zone: 1.0 outside the r/b window [axis[0], axis[14]] (scaled by
 *     out_shift), 4.0 inside (1.0 again above the blue window top), times the zone flag;
 *     weighted sums of r, b and the weight.
 *  2. a zero sum gives *valid = 1, ct 5000 and the gains (1.0, 1.0); otherwise
 *     *valid = 0 and r = sum_r / sum_w, b = sum_b / sum_w.
 *  3. gw_en[2] == 1 refines: every zone gets a class by the squared distance of its
 *     (r, b) from the mean (thresholds 818 .. 514, below that a table), the sums are
 *     taken again with the class as weight.  A zero sum there keeps the first r, b
 *     (with *valid = 1).
 *  4. ct from (r, b) by t32_awb_ct_cal; out = (r, b).
 */
static inline void t32_awb_grayworld_mode(struct t32_awb_par *par, struct t32_awb_state *st, const u32 *stats,
					  u32 mode, u32 *valid, u32 out[2])
{
	u32 fp = par->fp_shift, os = par->out_shift, o31 = os & 31, f31 = fp & 31;
	u32 one = 1U << f31, w4 = 4U << f31;
	u32 lo_r = par->ct_axis_r[0] << o31, hi_r = par->ct_axis_r[14] << o31;
	u32 lo_b = par->ct_axis_b[0] << o31, hi_b = par->ct_axis_b[14] << o31;
	u32 rows = par->zones_h, cols = par->zones_w;
	u32 sum_r = 0, sum_b = 0, sum_w = 0, r, b, row, col;

	if (par->gw_en[mode] != 1 || *valid != 1)
		return;

	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			const u32 *zs = stats + mode * 2048 + row * 32 + col;
			u32 sr = zs[0], sb = zs[1024], w, flag = (u32)par->zone_flag[row * 32 + col] << f31;

			if (sr < lo_r || hi_r < sr || sb < lo_b)
				w = one;
			else
				w = hi_b < sb ? one : w4;
			sum_r += t32_awb_fixmul3(fp, sr, flag, w);
			sum_b += t32_awb_fixmul3(fp, sb, flag, w);
			sum_w += t32_awb_fixmul(fp, flag, w);
		}
	}
	if (!sum_w || !sum_r || !sum_b) {
		*valid = 1;
		par->ct = 5000;
		out[0] = out[1] = one;
		return;
	}
	*valid = 0;
	r = t32_awb_fixdiv(fp, sum_r, sum_w);
	b = t32_awb_fixdiv(fp, sum_b, sum_w);

	if (par->gw_en[2] == 1) {
		u32 half = 1U << ((os - 1) & 31), mr = (r + half) >> o31, mb = (b + half) >> o31;

		for (row = 0; row < rows; row++) {
			for (col = 0; col < cols; col++) {
				const u32 *zs = stats + mode * 2048 + row * 32 + col;
				u32 vr = ((zs[0] + half) >> o31), vb = ((zs[1024] + half) >> o31), d, cls;

				vr = vr < mr ? mr - vr : vr - mr;
				vb = vb < mb ? mb - vb : vb - mb;
				d = vr * vr + vb * vb;
				if (d >= 818)
					cls = 0;
				else if (d >= 679)
					cls = 1;
				else if (d >= 615)
					cls = 2;
				else if (d >= 572)
					cls = 3;
				else if (d >= 540)
					cls = 4;
				else if (d >= 514)
					cls = 5;
				else
					cls = st->gw_tab[d];
				st->gw_map[row * 32 + col] = cls << o31;
			}
		}
		sum_r = sum_b = sum_w = 0;
		for (row = 0; row < rows; row++) {
			for (col = 0; col < cols; col++) {
				const u32 *zs = stats + mode * 2048 + row * 32 + col;
				u32 flag = (u32)par->zone_flag[row * 32 + col] << f31, cw = st->gw_map[row * 32 + col];

				sum_r += t32_awb_fixmul3(fp, zs[0], flag, cw);
				sum_b += t32_awb_fixmul3(fp, zs[1024], flag, cw);
				sum_w += t32_awb_fixmul(fp, flag, cw);
			}
		}
		if (!sum_w || !sum_r || !sum_b) {
			*valid = 1;
			par->ct = 5000;
		} else {
			r = t32_awb_fixdiv(fp, sum_r, sum_w);
			b = t32_awb_fixdiv(fp, sum_b, sum_w);
		}
	}
	t32_awb_ct_cal(par, r, b);
	out[0] = r;
	out[1] = b;
}

static inline void t32_awb_grayworld(struct t32_awb_par *par, struct t32_awb_state *st, const u32 *stats,
				     u32 *valid, u32 out[2])
{
	if (par->gw_en[0] == 1)
		t32_awb_grayworld_mode(par, st, stats, 0, valid, out);
	if (par->gw_en[1] == 1)
		t32_awb_grayworld_mode(par, st, stats, 1, valid, out);
}

#endif /* TX_ISP_T32_AWBCT_H */
