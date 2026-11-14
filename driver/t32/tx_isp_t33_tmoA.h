/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 TMO block A: func_bin_para_update, func_tmo_ic_para_update,
 * tisp_tmo_s2s3_ddrV_init (packing part) and tisp_tmo_t33_frame_done_interrupt.
 * The block state is the vendor's tmoInfo word array (v[byte offset / 4]).
 */
#ifndef TX_ISP_T33_TMOA_H
#define TX_ISP_T33_TMOA_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_tmo.h"

#define T33_TMOA_SEC_PTR	2732	/* tmoInfo byte offset of the bin section pointer */
#define T33_TMOA_DDRV_PTR	10832	/* tmoInfo byte offset of the V table buffer pointer */
#define T33_TMOA_FLAG		10845	/* tmoInfo byte offset of the "configured" flag */

/* func_bin_para_update: section s (tmoInfo + 2732 points at it) into the state words v */
static inline void t33_tmo_bin_para(u32 *v, const u8 *s)
{
	unsigned int i;

	v[26] = s[342];
	v[27] = t32_tp_le16(s + 344);
	v[28] = t32_tp_le16(s + 346);
	v[29] = t32_tp_le16(s + 348);
	v[30] = s[350];
	v[34] = s[418];
	for (i = 0; i < 16; i++)
		v[60 + i] = t32_tp_le16(s + 970 + 2 * i);
	for (i = 0; i < 7; i++)
		v[76 + i] = t33_tmo_log2(t32_tp_le16(s + 970 + 2 * i), t32_tp_le16(s + 972 + 2 * i));
	v[83] = s[1009];
	v[84] = s[1010];
	v[85] = s[1011];
	v[86] = t32_tp_le16(s + 1012);
	v[313] = s[5486];
	v[316] = s[5532];
	for (i = 0; i < 20; i++)
		v[318 + i] = t32_tp_le16(s + 5544 + 2 * i);
	for (i = 0; i < 25; i++)
		v[87 + i] = t32_tp_le16(s + 1014 + 2 * i);
}

/* copy nbytes (multiple of 4) inside the state, byte offsets */
static inline void t33_tmo_wcopy(u32 *v, u32 dst, u32 src, u32 nbytes)
{
	u32 i;

	for (i = 0; i < nbytes / 4; i++)
		v[dst / 4 + i] = v[src / 4 + i];
}

/* func_tmo_ic_para_update */
static inline void t33_tmo_ic_para(u32 *v)
{
	u32 a0, a1, t;

	t33_tmo_wcopy(v, 1400, 32, 48);
	t33_tmo_wcopy(v, 1472, 104, 4);
	t33_tmo_wcopy(v, 1488, 120, 4);
	t33_tmo_wcopy(v, 1500, 132, 4);
	t33_tmo_wcopy(v, 1504, 136, 4);
	t33_tmo_wcopy(v, 1700, 332, 16);
	t33_tmo_wcopy(v, 2620, 1252, 20);
	t33_tmo_wcopy(v, 2720, 1352, 12);
	t33_tmo_wcopy(v, 1384, 16, 8);
	t33_tmo_wcopy(v, 1392, 24, 8);
	t33_tmo_wcopy(v, 1448, 80, 8);
	t33_tmo_wcopy(v, 1456, 88, 8);
	t33_tmo_wcopy(v, 1464, 96, 8);
	t33_tmo_wcopy(v, 1476, 108, 12);
	t33_tmo_wcopy(v, 1492, 124, 8);
	t33_tmo_wcopy(v, 1508, 140, 100);
	t33_tmo_wcopy(v, 1608, 240, 32);
	t33_tmo_wcopy(v, 1640, 272, 32);
	t33_tmo_wcopy(v, 1672, 304, 28);
	t33_tmo_wcopy(v, 1716, 348, 100);
	t33_tmo_wcopy(v, 2640, 1272, 40);
	t33_tmo_wcopy(v, 2680, 1312, 40);
	t33_tmo_wcopy(v, 1816, 448, 804);
	if ((int)v[362] < 128)
		v[362] = 128;
	a1 = 1U << (v[25] & 31);
	a0 = 1U << (v[21] & 31);
	v[367] = a1 + v[366];
	v[363] = a0 + v[362];
	if ((int)v[367] >= 4096) {
		v[367] = 4095;
		v[366] = 4095 - a1;
	}
	if ((int)v[363] >= 0x10000) {
		v[363] = 0xffff;
		v[362] = 0xffff - a0;
	}
	t = v[363] - 128;
	if ((int)t < (int)v[365])
		v[365] = t;
	v[364] = 0;
}

/* tisp_tmo_s2s3_ddrV_init: 375 rows x 2 packs of five 12 bit values (info + 3304 + 20 * row + 10 * j) */
static inline void t33_tmo_ddrv_pack(const u8 *info, u32 *out)
{
	u32 r, j, h0, h1, h2, h3, h4;
	const u8 *p;

	for (r = 0; r < 375; r++) {
		for (j = 0; j < 2; j++) {
			p = info + 3304 + 20 * r + 10 * j;
			h0 = t32_tp_le16(p);
			h1 = t32_tp_le16(p + 2);
			h2 = t32_tp_le16(p + 4);
			h3 = t32_tp_le16(p + 6);
			h4 = t32_tp_le16(p + 8);
			out[4 * r + 2 * j] = (h1 & 0xfff) << 12 | (h2 & 0xfff) << 24 | (h0 & 0xfff);
			out[4 * r + 2 * j + 1] = (h2 & 0xfff) >> 8 | (h3 & 0xfff) << 4 | (h4 & 0xfff) << 16;
		}
	}
}

/* hardware / other-block callees of the frame-done interrupt */
struct t33_tmo_fd_ops {
	int (*reg_write)(u32 ch, u32 a1, u32 val, u32 a3);	/* tisp_reg_write_t33 */
	int (*cfg_trig)(u32 ch, u32 n);				/* tisp_rcfg_module_cfg_trig */
	int (*ctrl)(u32 idx);					/* tisp_tmo_ramV_vi23_reg_ctrl */
	int (*refresh)(u32 ch);					/* tisp_tmo_ramV_vi23_reg_refresh */
};

/*
 * tisp_tmo_t33_frame_done_interrupt: mode = stIspInitPar word 226 (byte 904), info / tp the tmoInfo /
 * tparamsP pointer arrays.  (ch outside 0..3: the vendor prints an error and uses 0; not reachable
 * since tmoInfo[ch] is read first.)
 */
static inline int t33_tmo_frame_done(const struct t33_tmo_fd_ops *o, u8 *const *info, u8 *const *tp, u32 mode,
				     u8 cfgmode, u8 *mulsensor, u8 *refresh, u32 ch)
{
	u32 val = cfgmode ? 32 : 28912, idx;

	if (mode < 2) {
		if (!info[ch][T33_TMOA_FLAG]) {
			o->reg_write(ch, 8, val, 0);
			info[ch][T33_TMOA_FLAG] = 1;
			*refresh = 1;
		}
	} else {
		if (!info[ch][T33_TMOA_FLAG]) {
			if (mode != 2 && !info[0][T33_TMOA_FLAG])
				*mulsensor = 1;
			info[ch][T33_TMOA_FLAG] = 1;
			o->reg_write(ch, 8, val, 0);
		}
		if (mode < 3) {
			o->ctrl(ch);
		} else {
			if (ch == 1)
				idx = 2;
			else if (ch == 0)
				idx = 1;
			else if (ch == 2)
				idx = mode == 4 ? 3 : 0;
			else
				idx = 0;
			if (!tp[idx][17])
				o->ctrl(idx);
			o->refresh(ch);
		}
	}
	if (mode == 1 && *refresh == 1) {
		o->cfg_trig(ch & 0xff, 8);
		*refresh = 0;
	}
	return 1;
}

#endif /* TX_ISP_T33_TMOA_H */
