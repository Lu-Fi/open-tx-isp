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

/* tisp_gamma_strength_transform + tisp_gamma_write_lut_rgb into list l */
static inline void t33_gamma_store(const u16 *c, u32 str, u32 *l)
{
	u32 s = (str >> 7) + str, k, x, o[T33_GAMMA_N];

	for (k = 0; k < 0x80; k++) {
		x = c[k] * s + k * (0x100 - s) * 0x20;
		o[k] = ((x >> 8) + ((x >> 7) & 1)) & 0xffff;	/* tisp_round_int64(x, 0, 8) */
	}
	x = c[0x80] * s + (0x100 - s) * 0xfff;
	o[0x80] = ((x >> 8) + ((x >> 7) & 1)) & 0xffff;
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

#endif /* TX_ISP_T33_GAMMA_H */
