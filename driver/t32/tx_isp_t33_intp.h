/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) signed interpolation helpers: the int8 twins of the proven
 * uint8 EV / bit-value interpolators, plus the simple 11-point u16
 * interpolator.  Vendor names in the comments; the unsigned EV port lives in
 * tx_isp_t33_blk.h (t33_ev_intp_u8) and is reused for the shared arithmetic.
 */
#ifndef TX_ISP_T33_INTP_H
#define TX_ISP_T33_INTP_H

#include "tx_isp_t33_blk.h"

/*
 * tisp_ev_intp_int8_t: 11-point signed table t over the EV points e (11 LE
 * u32); EV at or above the last point gives t[10], below the first t[0].
 * Same walk as t33_ev_intp_u8 but the table bytes are signed (lb / seb).
 */
static inline int t33_ev_intp_s8(u32 ev, const u8 *e, const u8 *t)
{
	u32 k, lo, hi, d;
	unsigned long long f;

	for (k = 0; k < 11; k++)
		if (ev < t33_le32(e + 4 * k))
			break;
	if (k == 11)
		return (signed char)t[10];
	if (!k)
		return (signed char)t[0];
	hi = t33_le32(e + 4 * k);
	lo = t33_le32(e + 4 * k - 4);
	d = hi - lo;
	if (!d)
		return 2;
	f = (unsigned long long)(ev - lo) * 0x1000 + (d >> 1);
	return t33_sub_interp((u32)t32_tp_div64(f, (unsigned long long)d),
			      (signed char)t[k - 1], (signed char)t[k]);
}

/*
 * tisp_bv_intp_int8_t: the bit-value twin of tisp_ev_intp_int8_t; the walk
 * is over the same 11 LE u32 knots with signed table bytes, but the knot
 * comparison is reversed (knot < value, i.e. break when value <= knot) and
 * the interpolation runs from t[i] to t[i-1] (hi = knot[i-1], lo = knot[i]).
 */
static inline int t33_bv_intp_s8(u32 v, const u8 *k, const u8 *t)
{
	u32 i, lo, hi, d;
	unsigned long long f;

	for (i = 0; i < 11; i++)
		if (t33_le32(k + 4 * i) < v)
			break;
	if (i == 11)
		return (signed char)t[10];
	if (!i)
		return (signed char)t[0];
	hi = t33_le32(k + 4 * i - 4);
	lo = t33_le32(k + 4 * i);
	d = hi - lo;
	if (!d)
		return 2;
	f = (unsigned long long)(hi - v) * 0x1000 + (d >> 1);
	return t33_sub_interp((u32)t32_tp_div64(f, (unsigned long long)d),
			      (signed char)t[i - 1], (signed char)t[i]);
}

/*
 * tisp_simple_intp_uint16_t: 11-point u16 table t, index i (0..9) picks the
 * pair t[i], t[i+1]; i >= 10 returns t[10].  f is 16.16 (0..0xffff); the
 * result is t[i] + (t[i+1] - t[i]) * f / 0x10000, rounded, clamped to u16.
 */
static inline u16 t33_simple_intp_u16(u32 i, u32 f, const u8 *t)
{
	u32 a, b, d, p, r;
	int up;

	if (i >= 10)
		return t32_tp_le16(t + 20);
	a = t32_tp_le16(t + 2 * i);
	b = t32_tp_le16(t + 2 * i + 2);
	if (a == b)
		return (u16)a;
	up = a < b;
	d = up ? b - a : a - b;
	p = d * f;
	r = (p >> 16) + ((p >> 15) & 1);
	return (u16)(up ? a + r : a - r);
}

#endif /* TX_ISP_T33_INTP_H */
