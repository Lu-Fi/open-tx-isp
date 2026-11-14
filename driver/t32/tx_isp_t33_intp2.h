/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) 1-D interpolation helpers over an 11-point table with a
 * clamp, from the vendor's tisp_ev_intp_uint16_t / tisp_bv_intp_uint16_t /
 * tisp_bv_intp_uint8_t.  The EV variant walks the knots upward (x < knot),
 * the BV variants walk them downward (knot < x); both interpolate with
 * tisp_sub_interp over the 12-bit fraction (d >> 1 rounding, div64_u64).
 */
#ifndef TX_ISP_T33_INTP2_H
#define TX_ISP_T33_INTP2_H

#include "tx_isp_t33_blk.h"

/* tisp_ev_intp_uint16_t: 11 u16 points over 11 LE u32 knots (rising) */
static inline u16 t33_ev_intp_u16(u32 ev, const u8 *e, const u8 *t)
{
	u32 k, lo, hi, d;
	unsigned long long f;

	for (k = 0; k < 11; k++)
		if (ev < t33_le32(e + 4 * k))
			break;
	if (k == 11)
		return t32_tp_le16(t + 20);
	if (!k)
		return t32_tp_le16(t);
	hi = t33_le32(e + 4 * k);
	lo = t33_le32(e + 4 * k - 4);
	d = hi - lo;
	if (!d)
		return 2;
	f = (unsigned long long)(ev - lo) * 0x1000 + (d >> 1);
	return (u16)t33_sub_interp((u32)t32_tp_div64(f, (unsigned long long)d), t32_tp_le16(t + 2 * k - 2),
				   t32_tp_le16(t + 2 * k));
}

/* tisp_bv_intp_uint16_t: 11 u16 points over 11 LE u32 knots (falling) */
static inline u16 t33_bv_intp_u16(u32 bv, const u8 *e, const u8 *t)
{
	u32 k, lo, hi, d;
	unsigned long long f;

	for (k = 0; k < 11; k++)
		if (t33_le32(e + 4 * k) < bv)
			break;
	if (k == 11)
		return t32_tp_le16(t + 20);
	if (!k)
		return t32_tp_le16(t);
	hi = t33_le32(e + 4 * k);
	lo = t33_le32(e + 4 * k - 4);
	d = lo - hi;
	if (!d)
		return 2;
	f = (unsigned long long)(lo - bv) * 0x1000 + (d >> 1);
	return (u16)t33_sub_interp((u32)t32_tp_div64(f, (unsigned long long)d), t32_tp_le16(t + 2 * k - 2),
				   t32_tp_le16(t + 2 * k));
}

/* tisp_bv_intp_uint8_t: 11 u8 points over 11 LE u32 knots (falling) */
static inline u8 t33_bv_intp_u8(u32 bv, const u8 *e, const u8 *t)
{
	u32 k, lo, hi, d;
	unsigned long long f;

	for (k = 0; k < 11; k++)
		if (t33_le32(e + 4 * k) < bv)
			break;
	if (k == 11)
		return t[10];
	if (!k)
		return t[0];
	hi = t33_le32(e + 4 * k);
	lo = t33_le32(e + 4 * k - 4);
	d = lo - hi;
	if (!d)
		return 2;
	f = (unsigned long long)(lo - bv) * 0x1000 + (d >> 1);
	return (u8)t33_sub_interp((u32)t32_tp_div64(f, (unsigned long long)d), t[k - 1], t[k]);
}

#endif /* TX_ISP_T33_INTP2_H */
