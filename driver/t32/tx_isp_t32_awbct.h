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

#endif /* TX_ISP_T32_AWBCT_H */
