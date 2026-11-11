/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LCE (local contrast) gain / EV strength: readable C of the vendor's
 * tisp_lce_tgain_interp_strength (0x576e4) with its helpers tisp_itp_bv
 * (0x5733c), tisp_lce_gauss_wgt (0x4c1d8), calculate_exponential (0x4bed4),
 * and tisp_lce_write_curve (0x56ff4).
 *
 * The LCE state of a channel (vendor t32_data_e1da8[ch], 0x22850 bytes, see
 * tx_isp_t32_lcem.h) starts with the pointer to the parameter section in use
 * (712 bytes, tuning block + 0x8494).  interp_strength takes the last gain
 * (state +0x22838, parameter byte 507 == 0) or the last EV (+0x2283c, byte
 * 507 != 0) and interpolates
 *  - six strength bytes into the section itself (+435..+440, from the 11
 *    point tables at +441 + 11 k),
 *  - nine u16 values (state +0x2279a.., tables +218 + 22 k),
 *  - two times five bytes (state +0x227b6.. / +0x227ed.., tables +518 / +583),
 * then refreshes the Gauss weights (tisp_lce_gauss_wgt) and the low pass
 * kernel (tisp_lce_gauss_lpf, t32_lce_lpf) whose parameters changed: in
 * day mode (state +0x22737 == 0) from the interpolated strengths, in WDR mode
 * from the section's u16 triples +48 .. +70.
 *
 * Vendor quirks kept (the port must be bit exact): over the gain the vendor
 * uses tisp_simple_intp_uint8/16 (t32_tp_intp_u8/16); over the EV it walks
 * the 11 u32 breakpoints at +4 for the first one below the EV and calls
 * tisp_itp_bv, but only the six section bytes get both end points; the u16
 * and the 2 x 5 byte tables get the lower index value twice.  An EV above
 * all breakpoints takes entry 10 for the section bytes and the u16 values,
 * entry 0 for the 2 x 5 bytes.  The WDR comparisons put a u16 against a
 * stored byte (a parameter above 255 always counts as changed).
 * tisp_lce_gauss_wgt divides by 2 * sigma^2 and by the exponential: a zero
 * divisor gives the value the recovered C models for the MIPS div (vendor
 * divide by zero, not fixed; t32_lce_sdiv).
 *
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_lcetg_oracle).
 */
#ifndef TX_ISP_T32_LCETG_H
#define TX_ISP_T32_LCETG_H

#include "tx_isp_t32_lce.h"

#define T32_LCETG_STATE_BYTES	0x22850U
#define T32_LCETG_GPAR		0x226ccU	/* vendor "par" block in the state (9932 + 0x20000) */
#define T32_LCETG_U16		0x2279aU	/* nine interpolated u16 */
#define T32_LCETG_B1		0x227b6U	/* five interpolated bytes */
#define T32_LCETG_B2		0x227edU	/* five interpolated bytes */
#define T32_LCETG_SEL		0x22737U	/* 0 = day weights, else WDR weights */
#define T32_LCETG_LAST_GAIN	0x22838U
#define T32_LCETG_LAST_EV	0x2283cU
#define T32_LCETG_CURVE		0x1c2a4U	/* 576 curve rows of 20 bytes */
#define T32_LCETG_CURVE_BYTES	11520U

static inline u32 t32_lcetg_rd32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline void t32_lcetg_wr16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/*
 * tisp_itp_bv: linear interpolation of y over x between the breakpoints
 * x_hi > x_lo (y_hi at x_hi, y_lo at x_lo), rounded; outside or for an
 * empty interval the nearer end (y_hi when x_lo >= x_hi or x >= x_hi).
 */
static inline u32 t32_itp_bv(u32 x, u32 x_hi, u32 x_lo, u32 y_hi, u32 y_lo)
{
	u32 d;

	if (!(x_lo < x_hi) || !(x < x_hi))
		return y_hi;
	if (!(x_lo < x))
		return y_lo;
	d = x_hi - x_lo;
	return ((x_hi - x) * y_lo + (x - x_lo) * y_hi + (d >> 1)) / d;
}

/*
 * calculate_exponential(x, n, a, q): e^x in Qq by n series terms, each
 * x / k shifted right by a - q, the 64 bit product shifted back by q; the
 * sum keeps 32 bits.
 */
static inline u32 t32_calc_exp(u32 x, u32 n, u32 a, u32 q)
{
	u32 acc = 1U << (q & 31), k;
	unsigned long long t = (unsigned long long)(long long)(int)acc;

	for (k = 1; (int)k <= (int)(n & 0xff); k++) {
		int f = (int)t32_lce_sdiv(x, k) >> (((a & 0xff) - (q & 0xff)) & 31);

		t = t * (unsigned long long)(long long)f;
		t = (unsigned long long)((long long)t >> (q & 0xff & 63));
		acc += (u32)t;
	}
	return acc;
}

/*
 * tisp_lce_gauss_wgt(ch, src, dst, n): 32 weights a / e^((8 i - c)^2 / (2 b^2))
 * (Q12, src = a, b, c); dst gets w[0..7] and every second of w[8..31]
 * (20 bytes); n = 1..4 also copies all 32 into the section at +149 / +181
 * / +643 / +675.
 */
static inline void t32_lce_gauss_wgt(u8 *sec, const u8 *src, u8 *dst, u32 n)
{
	u32 b = src[1], s3 = (b << 1) * b, t0 = (u32)((int)s3 >> 1), s6 = (u32)src[0] << 12;
	u8 w[32];
	int i, d;

	for (i = 0; i < 32; i++) {
		u32 e;

		d = 8 * i - src[2];
		e = t32_calc_exp(t32_lce_sdiv(((u32)(d * d) << 12) + t0, s3), 10, 12, 12);
		w[i] = (u8)t32_lce_sdiv((u32)((int)e >> 1) + s6, e);
	}
	memcpy(dst, w, 8);
	for (i = 0; i < 12; i++)
		dst[8 + i] = w[8 + 2 * i];
	switch (n & 0xff) {
	case 1:
		memcpy(sec + 149, w, 32);
		break;
	case 2:
		memcpy(sec + 181, w, 32);
		break;
	case 3:
		memcpy(sec + 643, w, 32);
		break;
	case 4:
		memcpy(sec + 675, w, 32);
		break;
	default:
		break;
	}
}

/* a WDR triple (u16 section values at off, +2, +4) against the stored bytes; stores and reports a change */
static inline int t32_lcetg_wdr_changed(const u8 *sec, u8 *st, u32 off, u32 stored)
{
	u32 k;

	for (k = 0; k < 3; k++)
		if (st[stored + k] != t32_tp_le16(sec + off + 2 * k))
			break;
	if (k == 3)
		return 0;
	for (k = 0; k < 3; k++)
		st[stored + k] = (u8)t32_tp_le16(sec + off + 2 * k);
	return 1;
}

/*
 * tisp_lce_tgain_interp_strength(ch): st = the channel's state (its first
 * word points to sec, the section).
 */
static inline void t32_lce_tgain_interp_strength(u8 *st, u8 *sec)
{
	u8 *g = st + T32_LCETG_GPAR;
	u32 k;

	if (sec[507] == 0) {
		u32 gain = t32_lcetg_rd32(st + T32_LCETG_LAST_GAIN), i = gain >> 16, f = gain & 0xffff;

		for (k = 0; k < 6; k++)
			sec[435 + k] = t32_tp_intp_u8(i, f, sec + 441 + 11 * k);
		for (k = 0; k < 9; k++)
			t32_lcetg_wr16(st + T32_LCETG_U16 + 2 * k, t32_tp_intp_u16(i, f, sec + 218 + 22 * k));
		for (k = 0; k < 5; k++)
			st[T32_LCETG_B1 + k] = t32_tp_intp_u8(i, f, sec + 518 + 11 * k);
		for (k = 0; k < 5; k++)
			st[T32_LCETG_B2 + k] = t32_tp_intp_u8(i, f, sec + 583 + 11 * k);
	} else {
		u32 ev = t32_lcetg_rd32(st + T32_LCETG_LAST_EV), j, lo;

		for (j = 0; j <= 10; j++)
			if (t32_lcetg_rd32(sec + 4 + 4 * j) < ev)
				break;
		if (j <= 10) {
			u32 bp_hi, bp_lo;

			lo = j ? j - 1 : 0;
			bp_hi = t32_lcetg_rd32(sec + 4 + 4 * lo);
			bp_lo = t32_lcetg_rd32(sec + 4 + 4 * j);
			for (k = 0; k < 6; k++)
				sec[435 + k] = (u8)t32_itp_bv(ev, bp_hi, bp_lo, sec[441 + 11 * k + lo],
							      sec[441 + 11 * k + j]);
			for (k = 0; k < 9; k++) {
				u32 v = t32_tp_le16(sec + 218 + 22 * k + 2 * lo);

				t32_lcetg_wr16(st + T32_LCETG_U16 + 2 * k, t32_itp_bv(ev, bp_hi, bp_lo, v, v));
			}
			for (k = 0; k < 5; k++)
				st[T32_LCETG_B1 + k] = (u8)t32_itp_bv(ev, bp_hi, bp_lo, sec[518 + 11 * k + lo],
								      sec[518 + 11 * k + lo]);
			for (k = 0; k < 5; k++)
				st[T32_LCETG_B2 + k] = (u8)t32_itp_bv(ev, bp_hi, bp_lo, sec[583 + 11 * k + lo],
								      sec[583 + 11 * k + lo]);
		} else {	/* no breakpoint below the EV */
			for (k = 0; k < 6; k++)
				sec[435 + k] = sec[451 + 11 * k];
			for (k = 0; k < 9; k++)
				t32_lcetg_wr16(st + T32_LCETG_U16 + 2 * k, t32_tp_le16(sec + 238 + 22 * k));
			for (k = 0; k < 5; k++)
				st[T32_LCETG_B1 + k] = sec[518 + 11 * k];
			for (k = 0; k < 5; k++)
				st[T32_LCETG_B2 + k] = sec[583 + 11 * k];
		}
	}

	if (st[T32_LCETG_SEL] == 0) {
		/* day: Gauss weights from the strengths +435 / +438, low pass from +432 */
		if (memcmp(sec + 435, g + 155, 3)) {
			t32_lce_gauss_wgt(sec, sec + 435, g + 161, 1);
			memcpy(g + 155, sec + 435, 3);
		}
		if (memcmp(sec + 438, g + 158, 3)) {
			t32_lce_gauss_wgt(sec, sec + 438, g + 181, 2);
			memcpy(g + 158, sec + 438, 3);
		}
		if (sec[432] != g[154]) {
			u16 lpf[15];

			t32_lce_lpf(sec[432], lpf);
			for (k = 0; k < 15; k++)
				t32_lcetg_wr16(g + 122 + 2 * k, lpf[k]);
			g[154] = sec[432];
		}
		return;
	}
	/* WDR: the four weight sets from the section's u16 triples */
	if (t32_lcetg_wdr_changed(sec, st, 48, T32_LCETG_GPAR + 342))
		t32_lce_gauss_wgt(sec, g + 342, g + 299, 3);
	if (t32_lcetg_wdr_changed(sec, st, 54, T32_LCETG_GPAR + 345))
		t32_lce_gauss_wgt(sec, g + 345, g + 319, 4);
	if (t32_lcetg_wdr_changed(sec, st, 60, T32_LCETG_GPAR + 348))
		t32_lce_gauss_wgt(sec, g + 348, g + 244, 1);
	if (t32_lcetg_wdr_changed(sec, st, 66, T32_LCETG_GPAR + 351))
		t32_lce_gauss_wgt(sec, g + 351, g + 264, 2);
}

/*
 * tisp_lce_write_curve(ch): the 576 curve rows of the state (+0x1c2a4) into
 * the second half of the curve DMA buffer (curve + 11520) as little endian
 * words.
 */
static inline void t32_lce_write_curve(const u8 *st, u8 *curve)
{
	memcpy(curve + T32_LCETG_CURVE_BYTES, st + T32_LCETG_CURVE, T32_LCETG_CURVE_BYTES);
}

#endif /* TX_ISP_T32_LCETG_H */
