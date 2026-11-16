/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) block r13E: gamma EV strength, defog strength / transition
 * interpolation, BCSH / CLM / CCM interpolators, AWB interpolation helpers
 * and the zone CT weight.  Vendor names in the comments.
 */
#ifndef TX_ISP_T33_R13E_H
#define TX_ISP_T33_R13E_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_gamma.h"
#include "tx_isp_t33_math.h"

/* shared T32 runtime (proven elsewhere) */
extern int tisp_round_int64(int lo, int hi, int shift);

/* tisp_gamma_interp_by_ev: the EV words are kept in ip, then the strength byte as t33_gamma_interp */
static inline int t33_gamma_interp_by_ev(const u8 *sec, u8 *str, u32 *ip, u32 lo, u32 hi, u32 force)
{
	ip[0] = lo;
	ip[1] = hi;
	return t33_gamma_interp(sec, lo, hi, str, (int)(force & 0xff));
}

/* tisp_defog_strength_itp: 9 bytes scaled by the signed strength s (x / 128; negative: on the inverted values) */
static inline void t33_defog_strength_itp(u32 s, const u8 *src, u8 *dst)
{
	u32 k, m = s & 0xff, x;

	if (!(m & 0x80)) {
		for (k = 0; k < 9; k++)
			dst[k] = (u8)((src[k] * m) >> 7);
		return;
	}
	if (m == 0x80) {
		for (k = 0; k < 9; k++)
			dst[k] = src[k];
		return;
	}
	m = (u8)~m;
	for (k = 0; k < 9; k++) {
		x = (u8)~src[k];
		dst[k] = (u8)~((x * m) >> 7);
	}
}

/* tisp_defog_interp_trans_k: five 9-entry tables blended at the knot index idx (idx 0: entry 0) */
static inline void t33_defog_k_pick(const u8 *const *tab, u32 idx, u32 wa, u32 wb, u8 *out)
{
	u32 j, d = wa + wb;

	for (j = 0; j < 5; j++) {
		if (!idx)
			out[j] = tab[j][0];
		else
			out[j] = (u8)((tab[j][idx - 1] * wa + tab[j][idx] * wb + (d >> 1)) / d);
	}
}

/*
 * p: the block's parameter set (flag p[195], 9 knots at p + 44), ev2 the
 * block's value (descending knots, used when p[195] is set), else ev against
 * the ascending knots kn.
 */
static inline void t33_defog_trans_k(const u8 *p, u32 ev2, u32 ev, const u8 *kn, const u8 *const *tab, u8 *out)
{
	u32 i, k, kp, j;

	if (p[195]) {
		for (i = 0; i < 9; i++) {
			k = t33_le32(p + 44 + 4 * i);
			if (k < ev2) {
				kp = i ? t33_le32(p + 44 + 4 * i - 4) : 0;
				t33_defog_k_pick(tab, i, ev2 - k, kp - ev2, out);
				return;
			}
		}
	} else {
		for (i = 0; i < 9; i++) {
			k = t33_le32(kn + 4 * i);
			if (ev < k) {
				kp = i ? t33_le32(kn + 4 * i - 4) : 0;
				t33_defog_k_pick(tab, i, k - ev, ev - kp, out);
				return;
			}
		}
	}
	for (j = 0; j < 5; j++)
		out[j] = tab[j][8];
}

/* tisp_bcsh_itp: a at lo, b at hi, rounded linear blend at x (a outside the open interval) */
static inline u32 t33_bcsh_itp(u32 x, u32 lo, u32 hi, u32 a, u32 b)
{
	u32 d;

	if (!(lo < hi) || !(lo < x))
		return a;
	if (!(x < hi))
		return b;	/* the vendor's delay slot loads b before this branch */
	d = hi - lo;
	return ((hi - x) * a + (x - lo) * b + (d >> 1)) / d;
}

/* tisp_clm_itp */
static inline int t33_clm_itp(int a, int b, u32 w)
{
	int s = (short)a;

	return (short)(s + tisp_round_int64((int)((u32)((short)b - s) * w), 0, 12));
}

/* tisp_ccm_sub_interp */
static inline int t33_ccm_sub_interp(u32 w, int a, int b)
{
	int p = (int)((u32)(b - a) * w);

	return a + tisp_round_int64(p, p >> 31, 12);
}

/* ISPAWBInterpolation1: p << sh moved by the blend of (q - p) << sh over (hi - lo) << sh */
static inline u32 t33_awb_interp1(u32 sh, u32 x, u32 lo, u32 hi, u32 p, u32 q)
{
	u32 d = (hi - lo) << (sh & 31), v = p << (sh & 31);
	u32 diff = q < p ? p - q : q - p;
	u32 t = ((diff << (sh & 31)) * (x - (lo << (sh & 31))) + (d >> 1)) / d;

	return q < p ? v - t : v + t;
}

/* ISPAWBInterpolation2: the same without the shift of p / q */
static inline u32 t33_awb_interp2(u32 sh, u32 x, u32 lo, u32 hi, u32 p, u32 q)
{
	u32 d = (hi - lo) << (sh & 31);
	u32 n = x - (lo << (sh & 31));
	u32 df = q < p ? p - q : q - p;
	u32 r = (n * df + (d >> 1)) / d;

	return q < p ? p - r : p + r;
}

/*
 * func_zone_ct_weight: z = { A, B, C, D } words, t[1] = T; weight w into *out, then
 * fix_point_mult2_32(bits, arg, w).
 */
static inline int t33_zone_ct_weight(u32 v, const u8 *z, const u8 *t, u32 bits, u32 arg, u8 *out)
{
	u32 A = t33_le32(z), B = t33_le32(z + 4), C = t33_le32(z + 8), D = t33_le32(z + 12);
	u32 T = t33_le32(t + 4), W = 1U << (bits & 31), w = T;

	if (v < D && B < v) {
		if (C < v)
			w = (T * (D - C) + (D - v) * (W - T) + ((D - C) >> 1)) / (D - C);
		else if (v < A)
			w = (T * (A - B) + (v - B) * (W - T) + ((A - B) >> 1)) / (A - B);
		else
			w = W;
	}
	out[0] = (u8)w;
	out[1] = (u8)(w >> 8);
	out[2] = (u8)(w >> 16);
	out[3] = (u8)(w >> 24);
	return fix_point_mult2_32((int)bits, (int)arg, (int)w);
}

#endif /* TX_ISP_T33_R13E_H */
