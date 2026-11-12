/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) MSCA (scaler / output channels), module list 23: the list
 * part of tisp_msca_t33_init for the single-sensor path (the layout of
 * tisp_rcfg_msca_reg_init with the three per-channel 0xf0048 blocks moved
 * to 0xf06b4, as the vendor does for one sensor).  Per channel: crop /
 * scale sizes = the ISP output size, scale steps 0x4000, the 0xf0560 /
 * 0xf0570 / 0xf0580 coefficient defaults, 0xf0170 + 0x100 * ch = width;
 * channels 1 and 2 take the u32 at +0x2c of IQ bin section 25.  The
 * vendor's direct 0xf0128 + 0x100 * ch = 0 is the caller's.
 * The channel set-up at FS time: tx_isp_t33_msca_curve.h and t33_msca_list_start.
 * QEMU trace of the vendor module (openimp #388): list 23 word-equal.
 */
#ifndef TX_ISP_T33_MSCA_H
#define TX_ISP_T33_MSCA_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_MSCA		23
#define T33_SEC_MSCA		25

static const struct t33_tcmd t33_msca_tmpl[] = {
	{ 0xf0010, 1, 0 }, { 0xf0040, 22, 0 }, { 0xf0100, 6, 0 }, { 0xf06b4, 22, 0 }, { 0xf0200, 6, 0 },
	{ 0xf06b4, 22, 0 }, { 0xf0300, 6, 0 }, { 0xf06b4, 22, 0 }, { 0xf0400, 47, 0 }, { 0xf04c4, 1, 0 },
	{ 0xf04d0, 6, 0 }, { 0xf04f0, 25, 0 }, { 0xf04c0, 4, 1 }, { 0xf0560, 28, 1 }, { 0xf0570, 34, 1 },
	{ 0xf0580, 34, 1 }, { 0xf2000, 1, 0 }, { 0xf2004, 96, 1 }, { 0xf2000, 1, 0 }, { 0xf0170, 1, 0 },
	{ 0xf0270, 1, 0 }, { 0xf0370, 1, 0 },
};

/*
 * Block init into list l (capacity cap words): w / h the ISP output size,
 * sec25 IQ bin section 25 (NULL: 0).  Returns the list length.
 */
#ifdef __KERNEL__
#define T33_MSCA_FN	static noinline_for_stack __maybe_unused int
#else
#define T33_MSCA_FN	static inline int
#endif
T33_MSCA_FN t33_msca_init(u32 w, u32 h, const u8 *sec25, u32 *l, u32 cap)
{
	/* coefficient defaults (slot, value) of channels 0, 1, 2 */
	static const u16 one[6] = { 0xcc, 0xd5, 0xe9, 0x104, 0x10c, 0x127 };
	static const u16 half[6] = { 0xde, 0xe3, 0xf3, 0xff, 0x116, 0x122 };
	u32 wh = (w & 0xffff) << 16 | (h & 0xffff), b, i;
	int len;

	len = t33_rcfg_tmpl_init(l, cap, t33_msca_tmpl, ARRAY_SIZE(t33_msca_tmpl));
	if (len <= 0)
		return len;
	T33_SW(l, 0x17, 0);
	T33_SW(l, 0x18, wh);
	for (i = 0; i < 3; i++) {
		b = 0x1e * i;
		T33_SW(l, 0x1a + b, 0);
		T33_SW(l, 0x194 + 2 * i, w & 0xffff);
		T33_SW(l, 0x1b + b, wh);
		T33_SW(l, 0x1c + b, 0);
		T33_SW(l, 0x1d + b, wh);
		T33_SW(l, 0x1e + b, 0x4000);
		T33_SW(l, 0x1f + b, 0x4000);
		if (i)
			T33_SW(l, 0x36 + b, sec25 ? t33_le32(sec25 + 0x2c) : 0);
		T33_SW(l, 0x22 + b, 0xffffffffU);
		T33_SW(l, 6, 0x77);
		T33_SW(l, 0x2c + b, 0);
		T33_SW(l, 0x34 + b, 0);
	}
	for (i = 0; i < 6; i++) {
		T33_SW(l, one[i], 0x8000000);
		T33_SW(l, half[i], 0x800);
	}
	T33_SW(l, 3, 0);
	T33_SW(l, 1, 1);
	return len;
}

#endif /* TX_ISP_T33_MSCA_H */
