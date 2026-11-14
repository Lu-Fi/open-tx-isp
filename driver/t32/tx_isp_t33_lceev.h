/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) LCE / LSF EV update (openimp #500): the vendor's
 *
 *  - func_lce_para_update: the list mirror of the lceInfo block
 *    (0x52c .. 0x8e0, 0x62e8, 0x62f4) from the parameter words, the 7 x 7
 *    Gauss low pass of sigma word 0x48 (mirror 0x568 ..) and the four LSF
 *    weight sets (tisp_lce_gauss_wgt of words 0x164 .. 0x190) into the
 *    mirror and back into the section (0x117 / 0x137, 0x1a3 / 0x1c3);
 *  - tisp_lce_ev_update (tisp_long_ev_update, AE event 6, LCE not
 *    bypassed): BV / EV word, the LSF-mode switch of the curve slot
 *    (list 16 0x19 = histogram buffer and list 37 slot 5 in the LSF mode
 *    sec[0x2fb] = sec[0xc8] = 1, else the curve buffer), bin_para_update,
 *    the EV (sec[0x3d9] = 0, knots 0x2fc) or BV (knots 0x328)
 *    interpolations, gausswgt_calc, func_lce_para_update and the list words
 *    of lists 16 and 37 from the mirror, then list 16 enable = 1;
 *  - tisp_lce_switch_para (day / night bin switch): the size fields, the
 *    default curves when the LSF mode byte changed, sec[5] = 3, then the
 *    same as the EV update at the last EV / BV.
 *
 * Both with the vendor's lceCfgMode = lsfCfgMode = 1 (slot indices; the
 * module's data, the only mode the T33 module uses).  The words are in the
 * vendor's lceInfo layout (struct t33_lce_pf, tx_isp_t33_lcepf.h) so that the
 * host oracle (tests/t33_binoracle/lceev.conf, tools/ko_emu/diff_t33_lceev.py)
 * compares the whole block.
 */
#ifndef TX_ISP_T33_LCEEV_H
#define TX_ISP_T33_LCEEV_H

struct t33_lce_pf;
static inline void t33_lcepf_para_update(struct t33_lce_pf *c, unsigned char *s);
#define T33_LCEPF_PARA_UPDATE(c, s)	t33_lcepf_para_update(c, s)
#include "tx_isp_t33_lcepf.h"

/* a word of the lceInfo block at its vendor byte offset */
#define T33_LIW(c, o)	(((u32 *)(void *)(c))[(o) / 4])

/* four low bytes of the words at a, b, c, d into one port word (a lowest) */
static inline u32 t33_lceev_p4(const struct t33_lce_pf *c, u32 a, u32 b, u32 cc, u32 d)
{
	const u32 *w = (const u32 *)(const void *)c;

	return (w[a / 4] & 0xffU) | (w[b / 4] & 0xffU) << 8 | (w[cc / 4] & 0xffU) << 16 | w[d / 4] << 24;
}

/* the four words from o upwards, o lowest */
#define T33_LIP4(c, o)	t33_lceev_p4(c, o, (o) + 4, (o) + 8, (o) + 12)

/* (a & m) << 16 | (b & m): two fields of one word */
#define T33_LIP2(c, hi, lo, m)	((T33_LIW(c, hi) & (m)) << 16 | (T33_LIW(c, lo) & (m)))

/* func_lce_para_update; s = section 15 (the vendor's lceInfo + 0x62f8) */
static inline void t33_lcepf_para_update(struct t33_lce_pf *c, u8 *s)
{
	static const u16 cp[][2] = {	/* mirror word <- parameter word */
		{ 0x538, 0x10 }, { 0x53c, 0x14 }, { 0x530, 0x08 }, { 0x534, 0x0c }, { 0x564, 0x3c },
		{ 0x590, 0x460 }, { 0x594, 0x464 }, { 0x598, 0x468 }, { 0x5d0, 0x4a0 }, { 0x628, 0x194 },
		{ 0x64c, 0x1b8 }, { 0x650, 0x1bc }, { 0x654, 0x1c0 }, { 0x658, 0x1c4 }, { 0x65c, 0x1c8 },
		{ 0x660, 0x1cc }, { 0x664, 0x1d0 }, { 0x668, 0x1d4 }, { 0x66c, 0x1d8 }, { 0x670, 0x1dc },
		{ 0x78c, 0x2f8 }, { 0x790, 0x2fc }, { 0x794, 0x300 }, { 0x798, 0x304 }, { 0x79c, 0x308 },
		{ 0x7a0, 0x30c }, { 0x7a4, 0x310 }, { 0x7a8, 0x314 }, { 0x7ac, 0x318 }, { 0x7b0, 0x31c },
		{ 0x8cc, 0x4fc }, { 0x8d0, 0x500 }, { 0x62e8, 0x528 },
	};
	static const u16 blk[][3] = {	/* mirror <- parameter words, count */
		{ 0x540, 0x18, 9 }, { 0x59c, 0x46c, 9 }, { 0x5c0, 0x490, 4 }, { 0x5d4, 0x4a4, 21 },
		{ 0x62c, 0x198, 8 }, { 0x674, 0x1e0, 3 }, { 0x680, 0x1ec, 3 }, { 0x7b4, 0x320, 3 },
		{ 0x7c0, 0x32c, 3 }, { 0x8d4, 0x518, 9 },
	};
	/* the two LSF weight set pairs: parameter words, mirror words, section bytes */
	static const u16 ws[2][3] = { { 0x164, 0x7cc, 0x1a3 }, { 0x17c, 0x68c, 0x117 } };
	u32 lpf[10], i, k;
	u8 up[32], dn[32];

	T33_LIW(c, 0x52c) = T33_LIW(c, 0x04) != 4;
	for (i = 0; i < ARRAY_SIZE(cp); i++)
		T33_LIW(c, cp[i][0]) = T33_LIW(c, cp[i][1]);
	for (i = 0; i < ARRAY_SIZE(blk); i++)
		for (k = 0; k < blk[i][2]; k++)
			T33_LIW(c, blk[i][0] + 4 * k) = T33_LIW(c, blk[i][1] + 4 * k);
	t33_lce_gauss_lpf(T33_LIW(c, 0x48) & 0xff, lpf);
	for (i = 0; i < 10; i++)
		T33_LIW(c, 0x568 + 4 * i) = lpf[i];
	for (k = 0; k < 2; k++) {
		const u32 p = ws[k][0];

		t33_lce_gauss_wgt(T33_LIW(c, p) & 0xff, T33_LIW(c, p + 4) & 0xff, T33_LIW(c, p + 8) & 0xff, up);
		t33_lce_gauss_wgt(T33_LIW(c, p + 12) & 0xff, T33_LIW(c, p + 16) & 0xff, T33_LIW(c, p + 20) & 0xff, dn);
		for (i = 0; i < 32; i++) {
			T33_LIW(c, ws[k][1] + 4 * i) = up[i];
			T33_LIW(c, ws[k][1] + 0x80 + 4 * i) = dn[i];
			s[ws[k][2] + i] = up[i];
			s[ws[k][2] + 0x20 + i] = dn[i];
		}
	}
	c->r62f4 = 0x1e0;
}

/* list 16 (LCE) words from the mirror, as tisp_lce_ev_update writes them */
static inline void t33_lceev_l16(struct t33_lce_pf *c, u32 *l16)
{
	u32 k;

	T33_SW(l16, 3, (T33_LIW(c, 0x530) & 0xffU) << 16 | (T33_LIW(c, 0x534) & 0x7fU));
	T33_SW(l16, 4, (T33_LIW(c, 0x538) & 0xfU) << 8 | (T33_LIW(c, 0x564) & 0xfU) << 20 | (T33_LIW(c, 0x53c) & 0x1fU));
	for (k = 0; k < 3; k++) {	/* the nine block reciprocals, 0x1fff each */
		T33_SW(l16, 5 + 2 * k, T33_LIP2(c, 0x544 + 0xc * k, 0x540 + 0xc * k, 0x1fffU));
		T33_SW(l16, 6 + 2 * k, T33_LIW(c, 0x548 + 0xc * k) & 0x1fffU);
	}
	T33_SW(l16, 0xb, T33_LIP2(c, 0x5c4, 0x5c0, 0x3ffU));
	T33_SW(l16, 0xc, T33_LIP2(c, 0x5cc, 0x5c8, 0x3ffU));
	T33_SW(l16, 0xd, (T33_LIW(c, 0x594) & 3U) << 8 | (T33_LIW(c, 0x598) & 1U) << 16 | (T33_LIW(c, 0x590) & 1U));
	for (k = 0; k < 4; k++)		/* the nine curve points */
		T33_SW(l16, 0x1d + k, T33_LIP2(c, 0x5a0 + 8 * k, 0x59c + 8 * k, 0x1ffU));
	T33_SW(l16, 0x21, T33_LIW(c, 0x5bc) & 0x1ffU);
	T33_LIW(c, 0x62e4) = 3;
	T33_SW(l16, 0x11, (T33_LIW(c, 0x62e8) & 0xfU) << 16 | 3);
	T33_SW(l16, 0x12, (T33_LIW(c, 0x62f0) & 0x7fU) << 8 | (T33_LIW(c, 0x62ec) & 3U));
	T33_SW(l16, 0x18, (c->r62f4 & 0xffffU) | 0x10000 | (T33_LIW(c, 0x648) & 1U) << 20);
	for (k = 0; k < 5; k++)
		T33_SW(l16, 0x23 + k, T33_LIP4(c, 0x5d4 + 0x10 * k));
}

/* list 37 (LSF) words from the mirror (the vendor writes some twice, with the same value) */
static inline void t33_lceev_l37(struct t33_lce_pf *c, u32 *l37)
{
	u32 k;

	T33_SW(l37, 9, (T33_LIW(c, 0x538) & 0xfU) << 8 | (T33_LIW(c, 0x53c) & 0x1fU));
	T33_SW(l37, 0xa, (T33_LIW(c, 0x8d4) & 0xffffU) | T33_LIW(c, 0x8d8) << 16);
	T33_SW(l37, 0xb, (T33_LIW(c, 0x8dc) & 0xffffU) | T33_LIW(c, 0x8e0) << 16);
	T33_SW(l37, 0xc, T33_LIW(c, 0x8cc) & 0x7fffffU);
	T33_SW(l37, 0xd, (T33_LIW(c, 0x52c) & 1U) << 16 | (T33_LIW(c, 0x8d0) & 0x7ffU));
	T33_SW(l37, 0xe, (T33_LIW(c, 0x640) & 1U) | T33_LIW(c, 0x628) << 24 | (T33_LIW(c, 0x644) & 1U) << 4 |
			 (T33_LIW(c, 0x5d0) & 0x3fU) << 8);
	T33_SW(l37, 0xf, (T33_LIW(c, 0x650) & 0x1fU) << 8 | (T33_LIW(c, 0x64c) & 0x1fU) << 16 | (T33_LIW(c, 0x654) & 1U));
	T33_SW(l37, 0x10, (T33_LIW(c, 0x65c) & 0xffU) << 16 | (T33_LIW(c, 0x658) & 0xffU));
	T33_SW(l37, 0x11, T33_LIP4(c, 0x660));
	T33_SW(l37, 0x12, t33_lceev_p4(c, 0x674, 0x678, 0x67c, 0x670));
	T33_SW(l37, 0x13, (T33_LIW(c, 0x684) & 1U) << 8 | (T33_LIW(c, 0x688) & 1U) << 16 | (T33_LIW(c, 0x680) & 1U));
	T33_SW(l37, 0x15, (T33_LIW(c, 0x790) & 0x1fU) << 8 | (T33_LIW(c, 0x78c) & 0x1fU) << 16 | (T33_LIW(c, 0x794) & 1U));
	T33_SW(l37, 0x16, (T33_LIW(c, 0x79c) & 0xffU) << 16 | (T33_LIW(c, 0x798) & 0xffU));
	T33_SW(l37, 0x17, T33_LIP4(c, 0x7a0));
	T33_SW(l37, 0x18, t33_lceev_p4(c, 0x7b4, 0x7b8, 0x7bc, 0x7b0));
	T33_SW(l37, 0x19, (T33_LIW(c, 0x7c4) & 1U) << 8 | (T33_LIW(c, 0x7c8) & 1U) << 16 | (T33_LIW(c, 0x7c0) & 1U));
	for (k = 0; k < 5; k++)		/* the Gauss low pass */
		T33_SW(l37, 0x1b + k, T33_LIP2(c, 0x56c + 8 * k, 0x568 + 8 * k, 0xfffU));
	for (k = 0; k < 8; k++) {	/* the four weight sets */
		T33_SW(l37, 0x21 + k, T33_LIP4(c, 0x68c + 0x10 * k));
		T33_SW(l37, 0x2a + k, T33_LIP4(c, 0x70c + 0x10 * k));
		T33_SW(l37, 0x33 + k, T33_LIP4(c, 0x7cc + 0x10 * k));
		T33_SW(l37, 0x3c + k, T33_LIP4(c, 0x84c + 0x10 * k));
	}
}

/*
 * The part tisp_lce_ev_update and tisp_lce_switch_para share: the LSF-mode
 * curve slot (lists 16 slot 0x19 and 37 slot 5 = hist_pa in the LSF mode,
 * else 0x19 = curve_pa), bin_para_update, the EV / BV interpolations,
 * gausswgt_calc, func_lce_para_update, the list words, list 16 enable.
 * mode[0] / mode[1]: the vendor's lcemodeflag / lcemodecounter.
 */
static inline void t33_lceev_apply(struct t33_lce_pf *c, u8 *s, u32 curve_pa, u32 hist_pa, u32 *l16, u32 *l37,
				   u32 mode[2])
{
	if (s[0x2fb] == 1 && s[0xc8] == 1) {
		T33_SW(l37, T33_LSF_SLOT_HIST, hist_pa);
		T33_SW(l16, T33_LCE_SLOT_CURVE, hist_pa);
		mode[0] = 0;
	} else {
		T33_SW(l16, T33_LCE_SLOT_CURVE, curve_pa);
		mode[0] = 1;
	}
	T33_SW(l16, 0x1b, 0);
	mode[1] = 0;
	t33_lcepf_bin_para(c, s);
	t33_lcepf_ev_par(c, s);
	t33_lcepf_gausswgt(c, s);
	t33_lcepf_para_update(c, s);
	t33_lceev_l37(c, l37);
	t33_lceev_l16(c, l16);
	T33_SW(l16, T33_LCE_SLOT_EN, 1);
}

/*
 * tisp_lce_ev_update(ch, ev, x): lo / hi the 64-bit EV (the vendor's
 * long_ev_update words), bv its x word.  curve_pa / hist_pa the physical
 * addresses of the curve buffer and the LSF histogram buffer (the caller
 * invalidates the histogram buffer in the LSF mode, as the vendor's
 * private_dma_cache_sync), l16 / l37 the two lists.
 */
static inline void t33_lcepf_ev_update(struct t33_lce_pf *c, u8 *s, u32 lo, u32 hi, u32 bv, u32 curve_pa,
				       u32 hist_pa, u32 *l16, u32 *l37, u32 mode[2])
{
	c->bv = bv;
	c->ev = hi << 22 | lo >> 10;
	t33_lceev_apply(c, s, curve_pa, hist_pa, l16, l37, mode);
}

/*
 * tisp_lce_switch_para (day / night bin switch): s is the new section 15
 * (the vendor's tparamsP + 0x5e5c, already replaced); the size fields
 * again from the lceInfo sensor size (tisp_lce_size_para_calc, its
 * 1920 x 1080 defaults being what t33_lce_size gives), the default curves
 * (tisp_lce_curve_init_default with tisp_lce_write_all_reg's curve into
 * half2) when the mode byte sec[0xc8] changed (lceInfo + 0x23dc1), the soft
 * update word sec[5] = 3, then the EV update part at the last EV / BV.
 */
static inline void t33_lcepf_switch_para(struct t33_lce_pf *c, u8 *s, u32 *half2, u32 curve_pa, u32 hist_pa,
					 u32 *l16, u32 *l37, u32 mode[2])
{
	u8 *dn = (u8 *)(void *)c + 0x23dc1;

	t33_lce_size(s, c->h, c->w);
	if (*dn != s[0xc8]) {
		*dn = s[0xc8];
		t33_lcepf_curve_default(c);
		t33_lcepf_write_curve(c, half2);
	}
	s[5] = 3;
	t33_lceev_apply(c, s, curve_pa, hist_pa, l16, l37, mode);
}

#endif /* TX_ISP_T33_LCEEV_H */
