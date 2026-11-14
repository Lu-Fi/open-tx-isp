/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) gamma, module list 11, IQ bin section 13 (0x48 bytes) and
 * its payload (sec[0x46] curves of 0x10a bytes: 0x81 u16, two s32 EV
 * bounds): tisp_gamma_init with tisp_gamma_interp_by_ev (strength; mode
 * sec[0x34]: 0 full, 1 over the 12 EV points at +4 with the bytes at
 * +0x36, else sec[0x35]), tisp_gamma_update_curve (curve pick / blend at
 * the EV), tisp_gamma_strength_transform (blend with the linear ramp) and
 * tisp_gamma_write_lut_rgb (the curve as 128 pairs through the two ports
 * 0x32044 / 0x32084, framed by 0x101 / 0x7f0102).  The EV is the vendor's
 * init EV >> 10 as two words (CCM: 0).  QEMU trace of the vendor module
 * (openimp #388): list 11 word-equal.
 */
#ifndef TX_ISP_T33_GAMMA_H
#define TX_ISP_T33_GAMMA_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_GAMMA		11
#define T33_SEC_GAMMA		13
#define T33_GAMMA_SEC_BYTES	0x48U
#define T33_GAMMA_CURVE_BYTES	0x10aU
#define T33_GAMMA_ID		0x01350181U
#define T33_GAMMA_N		0x81

static const struct t33_tcmd t33_gamma_tmpl[] = {
	{ 0x32040, 1, 0 }, { 0x32044, 128, 1 }, { 0x32040, 1, 0 }, { 0x32080, 1, 0 }, { 0x32084, 128, 1 },
	{ 0x32080, 1, 0 },
};

/* gamma_ap: the vendor's default API curve (tisp_gamma_init copies it; a normal write with sec[0x44] == 1 replaces it) */
static const u16 t33_gamma_ap_def[T33_GAMMA_N] = {
	0, 144, 288, 426, 541, 641, 730, 812, 887, 957, 1024, 1086, 1146, 1203, 1257, 1310,
	1360, 1409, 1456, 1502, 1547, 1590, 1632, 1673, 1713, 1753, 1791, 1829, 1866, 1902, 1937, 1972,
	2006, 2040, 2073, 2106, 2138, 2169, 2200, 2231, 2261, 2291, 2320, 2349, 2378, 2406, 2434, 2462,
	2489, 2516, 2543, 2569, 2595, 2621, 2647, 2672, 2697, 2722, 2746, 2771, 2795, 2819, 2842, 2866,
	2889, 2912, 2935, 2958, 2980, 3003, 3025, 3047, 3068, 3090, 3112, 3133, 3154, 3175, 3196, 3217,
	3237, 3257, 3278, 3298, 3318, 3338, 3358, 3377, 3397, 3416, 3435, 3454, 3474, 3492, 3511, 3530,
	3549, 3567, 3585, 3604, 3622, 3640, 3658, 3676, 3694, 3711, 3729, 3746, 3764, 3781, 3798, 3815,
	3833, 3850, 3866, 3883, 3900, 3917, 3933, 3950, 3966, 3983, 3999, 4015, 4031, 4047, 4063, 4079,
	4095,
};

/* tisp_gamma_interp_by_ev (init: force): the strength byte */
static inline u32 t33_gamma_strength(const u8 *sec, u32 e)
{
	u32 k, t0, t1;

	if (sec[0x34] == 0)
		return 0xff;
	if (sec[0x34] != 1)
		return sec[0x35];
	for (k = 0; k < 12; k++) {
		t1 = t33_le32(sec + 4 + 4 * k);
		if (e < t1) {
			if (!k)
				return sec[0x36];
			t0 = t33_le32(sec + 4 * k);
			if (t1 == t0)
				return 0xff;	/* the vendor returns an error, keeping 0xff */
			return (sec[0x36 + k - 1] * (t1 - t0 - (e - t0)) + sec[0x36 + k] * (e - t0) + ((t1 - t0) >> 1)) /
			       (t1 - t0);
		}
	}
	return sec[0x41];
}

/* tisp_gamma_update_curve: the curve at EV e into c (0x81 u16) */
static inline void t33_gamma_curve(const u8 *sec, const u8 *tab, u32 e, u16 *c)
{
	u32 n = sec[0x46], u, k, d, w;
	const u8 *src = tab;

#define CV(p, i)	t32_tp_le16((p) + 2 * (i))
	if (t33_le32(sec) >= T33_GAMMA_ID) {
		for (u = 0; u < n; u++) {
			src = tab + u * T33_GAMMA_CURVE_BYTES;
			if ((int)e < t33_s32(src + 0x102)) {
				if (u) {
					const u8 *prev = src - T33_GAMMA_CURVE_BYTES;

					d = (u32)(t33_s32(src + 0x102) - t33_s32(prev + 0x106));
					w = e - (u32)t33_s32(prev + 0x106);
					if (!d)
						return;
					for (k = 0; k < T33_GAMMA_N; k++)
						c[k] = (u16)((CV(src, k) * w + CV(prev, k) * (d - w) + (d >> 1)) / d);
					return;
				}
				break;
			}
			if ((int)e <= t33_s32(src + 0x106))
				break;
		}
	}
	for (k = 0; k < T33_GAMMA_N; k++)
		c[k] = (u16)CV(src, k);
#undef CV
}

/* tisp_gamma_strength_transform, normal mode: the curve c blended with the linear ramp by the strength into o */
static inline void t33_gamma_transform(const u16 *c, u32 str, u16 *o)
{
	u32 s = (str >> 7) + str, k, x;

	for (k = 0; k < 0x80; k++) {
		x = c[k] * s + k * (0x100 - s) * 0x20;
		o[k] = (u16)((x >> 8) + ((x >> 7) & 1));	/* tisp_round_int64(x, 0, 8) */
	}
	x = c[0x80] * s + (0x100 - s) * 0xfff;
	o[0x80] = (u16)((x >> 8) + ((x >> 7) & 1));
}

/* tisp_gamma_write_lut_rgb: o as 128 pairs into both ports of list l */
static inline void t33_gamma_lut(const u16 *o, u32 *l)
{
	u32 k, x;

	T33_SW(l, 1, 0x101);
	T33_SW(l, 0x86, 0x101);
	for (k = 0; k < 0x80; k++) {
		x = (o[k + 1] & 0xfff) << 12 | (o[k] & 0xfff);
		T33_SW(l, 3 + k, x);
		T33_SW(l, 0x88 + k, x);
	}
	T33_SW(l, 0x84, 0x7f0102);
	T33_SW(l, 0x109, 0x7f0102);
}

/* the normal transform + write_lut_rgb into list l (the init path: sec[0x42] / [0x43] cleared, no API curve) */
static inline void t33_gamma_store(const u16 *c, u32 str, u32 *l)
{
	u16 o[T33_GAMMA_N];

	t33_gamma_transform(c, str, o);
	t33_gamma_lut(o, l);
}

/*
 * tisp_gamma_strength_transform + write_lut_rgb with the mode bytes: normal
 * (sec[0x42] == 1, or 0 with sec[0x43] != 2): the blended curve, kept as the
 * API curve ap when sec[0x44] == 1; else (API mode) the API curve ap itself
 * (gamma_ap, 0x81 u16).  The vendor's sec[0x45] == 1 copy of ap into the
 * bin's first curve is not done (the bin stays read-only); returns 1 then.
 */
static inline int t33_gamma_write(const u8 *sec, const u16 *c, u32 str, u16 *ap, u32 *l)
{
	u16 o[T33_GAMMA_N];

	if (sec[0x42] == 1 || (sec[0x42] == 0 && sec[0x43] != 2)) {
		t33_gamma_transform(c, str, o);
		if (sec[0x44] == 1)
			memcpy(ap, o, sizeof(o));
		t33_gamma_lut(o, l);
		return 0;
	}
	t33_gamma_lut(ap, l);
	return sec[0x45] == 1;
}

/*
 * Block init into list l (capacity cap words): sec section 13, tab its
 * payload (tabbytes); ev_lo / ev_hi the init EV words (EV >> 10).
 * Returns the list length, -1 when the payload does not hold the curves.
 */
static inline int t33_gamma_init(const u8 *sec, const u8 *tab, u32 tabbytes, u32 ev_lo, u32 ev_hi, u32 *l,
				 u32 cap)
{
	u32 e = ev_hi << 22 | ev_lo >> 10;
	u16 c[T33_GAMMA_N] = { 0 };
	int len;

	if (!sec[0x46] || tabbytes < sec[0x46] * T33_GAMMA_CURVE_BYTES)
		return -1;
	len = t33_rcfg_tmpl_init(l, cap, t33_gamma_tmpl, ARRAY_SIZE(t33_gamma_tmpl));
	t33_gamma_curve(sec, tab, e, c);
	if (len > 0)
		t33_gamma_store(c, t33_gamma_strength(sec, e), l);
	return len;
}

/*
 * tisp_gamma_interp_by_ev at EV lo / hi into the strength byte *str
 * (gamma_para[1] byte 0): 0 = strength set, 1 = unchanged (not forced),
 * -1 = two equal EV points (the vendor returns 0xffffffff, *str kept).
 * sec[0x42] / [0x43] (cleared by tisp_gamma_init, raw after a bin switch)
 * select the API mode: then the strength is 0xff and nothing is written.
 */
static inline int t33_gamma_interp(const u8 *sec, u32 lo, u32 hi, u8 *str, int force)
{
	u32 e, k, t0, t1, d, v = 0;

	if (sec[0x42] != 1 && (sec[0x42] != 0 || sec[0x43] == 2)) {
		*str = 0xff;
		return force != 1;
	}
	if (sec[0x34] == 0) {
		if (!force && *str == 0xff)
			return 1;
		*str = 0xff;
		return 0;
	}
	if (sec[0x34] != 1) {
		if (!force && *str == sec[0x35])
			return 1;
		*str = sec[0x35];
		return 0;
	}
	e = hi << 22 | lo >> 10;
	for (k = 0; k < 12; k++) {
		t1 = t33_le32(sec + 4 + 4 * k);
		if (e < t1) {
			if (!k) {
				v = sec[0x36];
			} else {
				t0 = t33_le32(sec + 4 * k);
				d = t1 - t0;
				if (!d)
					return -1;
				v = (sec[0x36 + k - 1] * (d - (e - t0)) + sec[0x36 + k] * (e - t0) + (d >> 1)) / d;
			}
			break;
		}
		if (k == 11)
			v = sec[0x41];
	}
	if (v == *str && !force)
		return 1;
	*str = (u8)v;
	return 0;
}

/*
 * tisp_gamma_ev_update (tisp_long_ev_update, top bypass bit 8 clear): EV
 * clamped to 0x100:0, interp_by_ev (not forced; ip = the EV words it ran
 * at), update_curve into the block curve c (always), and only on a new
 * strength strength_transform + write_lut_rgb into list l (tispGammaRefresh:
 * the list reruns at the next frame).  Returns 0 written, 1 unchanged, -1
 * vendor error.  tabbytes must hold sec[0x46] curves (t33_gamma_init).
 */
static inline int t33_gamma_ev_update(const u8 *sec, const u8 *tab, u32 lo, u32 hi, u8 *str, u32 *ip,
				      u16 *c, u16 *ap, u32 *l)
{
	int r;

	if (hi > 0x100 || (hi == 0x100 && lo)) {
		lo = 0;
		hi = 0x100;
	}
	ip[0] = lo;
	ip[1] = hi;
	r = t33_gamma_interp(sec, lo, hi, str, 0);
	t33_gamma_curve(sec, tab, hi << 22 | lo >> 10, c);
	if (r)
		return r < 0 ? -1 : 1;
	t33_gamma_write(sec, c, *str, ap, l);
	return 0;
}

/*
 * tisp_gamma_switch_para (day / night bin switch): sec / tab the new section 13
 * and curves (raw: 0x42 / 0x43 as the bin has them), interp_by_ev (forced)
 * and update_curve at the kept EV words ip, then the mode-dependent write
 * (t33_gamma_write) into list l.  tabbytes must hold sec[0x46] curves.
 */
static inline void t33_gamma_switch(const u8 *sec, const u8 *tab, const u32 *ip, u8 *str, u16 *c, u16 *ap, u32 *l)
{
	t33_gamma_interp(sec, ip[0], ip[1], str, 1);
	t33_gamma_curve(sec, tab, ip[1] << 22 | ip[0] >> 10, c);
	t33_gamma_write(sec, c, *str, ap, l);
}

#endif /* TX_ISP_T33_GAMMA_H */
