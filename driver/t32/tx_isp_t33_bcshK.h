/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) BCSH runtime entry points (openimp #99): the AE event
 * handler and the two API setters, module list 18.
 *
 * Callees ported elsewhere or touching hardware are recorded and compared
 * by the oracle: tisp_bcsh_interp_by_ev, tisp_bcsh_calc_para_refresh,
 * tisp_params_transform, tisp_rcfg_get_dma_state, tisp_reg_write_t33.
 * On an idle list DMA the block state bcshInfo[ch] (u16 words) is written
 * through 50 tisp_reg_write_t33 calls: slot 0xf0xx (bcshCfgMode == 0) or
 * the small slot number (bcshCfgMode != 0), value = two masked halfwords.
 */
#ifndef TX_ISP_T33_BCSHK_H
#define TX_ISP_T33_BCSHK_H

#include "tx_isp_t33_blk.h"

/* module globals */
extern void *bcsh_para[];
extern void *bcshInfo[];
extern u8 bcshCfgMode;
extern u8 tispBcshRefresh;

/* callees recorded by the oracle */
extern int tisp_bcsh_interp_by_ev(u32 ch, u32 a1, u32 a2, u32 a3, u32 force);
extern int tisp_bcsh_calc_para_refresh(u32 ch, u32 mode);
extern int tisp_params_transform(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 id);
extern int tisp_reg_write_t33(u32 ch, u32 id, u32 slot, u32 val);

static inline u32 t33_bcshk_hu(const u8 *p, u32 o)
{
	return p[o] | (u32)p[o + 1] << 8;
}

/* the register stores of the three entry points (idle DMA path) */
static inline void t33_bcshk_write_all(u8 c)
{
	const u8 *inf = bcshInfo[c];

#define H(o)	t33_bcshk_hu(inf, (o))
#define B(o)	((u32)inf[o])
#define WR(nz, z, v)	tisp_reg_write_t33(c, 18, bcshCfgMode ? (nz) : (z), (v))
#define PR(h, l, m)	((H(h) & (m)) << 16 | (H(l) & (m)))
#define W21	((B(174) & 0x3f) << 4 | (H(114) & 0x3ff) << 16 | (B(175) & 3))
#define W17	((B(172) & 1) << 16 | (H(72) & 0x3ff))
	WR(2, 0xf080, PR(58, 44, 0x3ff));
	WR(6, 0xf090, PR(92, 14, 0x3ff));
	WR(40, 0xf098, PR(150, 30, 0x3ff));
	WR(41, 0xf098, PR(100, 46, 0x3ff));
	WR(42, 0xf098, PR(168, 42, 0x3ff));
	WR(43, 0xf098, PR(124, 20, 0x3ff));
	WR(44, 0xf098, PR(18, 126, 0x3ff));
	WR(45, 0xf098, PR(4, 84, 0x3ff));
	WR(46, 0xf098, PR(12, 6, 0x3ff));
	WR(47, 0xf098, PR(34, 156, 0x3ff));
	WR(48, 0xf098, PR(162, 88, 0x3ff));
	WR(10, 0xf0d0, PR(160, 66, 0x3ff));
	WR(11, 0xf0d4, PR(142, 56, 0x1fff));
	WR(12, 0xf0d8, PR(140, 70, 0x1fff));
	WR(13, 0xf0dc, PR(78, 38, 0x3ff));
	WR(14, 0xf0e0, PR(82, 158, 0x3ff));
	WR(15, 0xf0e4, W17);
	WR(16, 0xf0e8, PR(32, 0, 0x3ff));
	WR(17, 0xf0ec, PR(80, 144, 0x3ff));
	WR(18, 0xf0f0, H(8) & 0x3ff);
	WR(21, 0xf0fc, W21);
	inf = bcshInfo[c];
	WR(23, 0xf040, PR(22, 40, 0x3ff));
	WR(24, 0xf040, PR(164, 60, 0x3ff));
	WR(25, 0xf040, PR(64, 54, 0x3ff));
	WR(26, 0xf040, PR(10, 116, 0x3ff));
	WR(27, 0xf040, PR(132, 102, 0x3ff));
	WR(28, 0xf040, PR(28, 96, 0x3ff));
	WR(30, 0xf058, PR(50, 74, 0x7ff));
	WR(31, 0xf058, PR(106, 68, 0x7ff));
	WR(32, 0xf058, PR(148, 26, 0x7ff));
	WR(34, 0xf064, PR(130, 122, 0x3fff));
	WR(35, 0xf064, PR(48, 24, 0x3fff));
	WR(36, 0xf064, PR(98, 76, 0x3fff));
	WR(37, 0xf064, PR(36, 128, 0x3fff));
	WR(38, 0xf064, H(90) & 0x3fff);
	WR(1, 0xf07c, (H(86) & 0x3ff) << 16 | (B(173) & 1));
	WR(3, 0xf084, (H(104) & 0x3ff) << 16 | (B(170) & 1));
	WR(4, 0xf088, PR(2, 62, 0x3ff));
	WR(5, 0xf08c, (H(166) & 0x3ff) << 16 | (B(177) & 1));
	WR(7, 0xf094, (B(171) & 1) << 16);
	WR(49, 0xf098, PR(134, 146, 0x7fff));
	WR(50, 0xf098, PR(118, 152, 0x7fff));
	WR(51, 0xf098, PR(120, 16, 0x7fff));
	WR(52, 0xf098, PR(136, 154, 0x7fff));
	WR(9, 0xf0cc, ((H(52) << 3) & 0xffff) | (H(138) & 0x1fff) << 16 | (B(176) & 1));
	WR(15, 0xf0e4, W17);
	WR(19, 0xf0f4, H(110) << 16 | H(94));
	WR(20, 0xf0f8, H(108) << 16 | H(112));
	WR(21, 0xf0fc, W21);
	WR(21, 0xf0fc, W21);
#undef H
#undef B
#undef WR
#undef PR
#undef W21
#undef W17
}

/* tisp_bcsh_ev_update */
static inline int t33_bcshk_ev_update(u32 ch, u32 a1, u32 lo, u32 hi, u32 bvl, u32 bvh)
{
	u8 *st, *sec;
	int ir, r;

	if (hi > 0x100 || (hi == 0x100 && lo)) {
		lo = 0;
		hi = 0x100;
	}
	st = bcsh_para[ch];
	sec = *(u8 **)st;
	if (sec[0x4c0]) {
		lo = bvl;
		hi = bvh;
	}
	ir = tisp_bcsh_interp_by_ev(ch, a1, lo, hi, 0);
	st = bcsh_para[ch];
	st[0x28] = st[0x28] == 0xff ? 0 : st[0x28] + 1;
	if (ir)
		return 0;
	tisp_bcsh_calc_para_refresh(ch, 1);
	tisp_params_transform(ch & 0xff);
	r = tisp_rcfg_get_dma_state(18);
	if (r) {
		tispBcshRefresh = 2;
		return 0;
	}
	t33_bcshk_write_all(ch & 0xff);
	tispBcshRefresh = 1;
	return 0;
}

/* tisp_bcsh_api_set_brightness */
static inline int t33_bcsh_set_brightness(u32 ch, const u8 *val)
{
	u8 *st = bcsh_para[ch];
	u8 *sec = *(u8 **)st;
	int r;

	sec[1199] = 2;
	st[460] = val[0];
	tisp_bcsh_interp_by_ev(ch, (u32)(unsigned long)val, ((u32 *)st)[2], ((u32 *)st)[3], 1);
	tisp_bcsh_calc_para_refresh(ch, 0);
	tisp_params_transform(ch & 0xff);
	r = tisp_rcfg_get_dma_state(18);
	if (r) {
		tispBcshRefresh = 2;
		return 0;
	}
	t33_bcshk_write_all(ch & 0xff);
	tispBcshRefresh = 1;
	return 0;
}

/* tisp_bcsh_api_set_contrast */
static inline int t33_bcsh_set_contrast(u32 ch, const u8 *val)
{
	u8 *st = bcsh_para[ch];
	u8 *sec = *(u8 **)st;
	int r;

	sec[1144] = 2;
	st[461] = val[0];
	tisp_bcsh_interp_by_ev(ch, (u32)(unsigned long)val, ((u32 *)st)[2], ((u32 *)st)[3], 1);
	tisp_bcsh_calc_para_refresh(ch, 0);
	tisp_params_transform(ch & 0xff);
	r = tisp_rcfg_get_dma_state(18);
	if (r) {
		tispBcshRefresh = 2;
		return 0;
	}
	t33_bcshk_write_all(ch & 0xff);
	tispBcshRefresh = 1;
	return 0;
}

#endif /* TX_ISP_T33_BCSHK_H */
