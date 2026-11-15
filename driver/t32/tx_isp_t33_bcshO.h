/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) BCSH frame-done handler (openimp #99): tisp_bcsh_irq_frame_down.
 * Counts the frame, and for tispBcshRefresh 1 triggers the list, for 2 (when the list DMA is idle) rewrites
 * the whole list 18 from bcshInfo[ch] (a u16 block, tisp_params_transform's output) word by word.
 */
#ifndef TX_ISP_T33_BCSHO_H
#define TX_ISP_T33_BCSHO_H

#include "tx_isp_t33_bcsh.h"

struct t33_bcsh_fd_ops {
	void (*trig)(u32 ch, u32 mod);			/* tisp_rcfg_module_cfg_trig */
	void (*xform)(u32 ch);				/* tisp_params_transform */
	int (*dma)(u32 mod);				/* tisp_rcfg_get_dma_state */
	void (*wr)(u32 ch, u32 mod, u32 slot, u32 val);	/* tisp_reg_write_t33 */
};

/* tisp_bcsh_irq_frame_down; the vendor globals are passed in; returns 1 */
static inline int t33_bcsh_frame_down(u32 ch, const struct t33_bcsh_fd_ops *ops, u32 *frame_num,
				      u32 init_flag, u8 *refresh, void *const *info_tab, u8 mode)
{
	u32 c = ch & 0xff;
	const u8 *inf;

	frame_num[ch]++;
	if (init_flag != 1)
		return 1;
	if (*refresh == 1) {
		ops->trig(c, T33_MOD_BCSH);
		*refresh = 0;
		return 1;
	}
	if (*refresh != 2)
		return 1;
	ops->xform(c);
	if (ops->dma(T33_MOD_BCSH)) {
		*refresh = 2;
		return 1;
	}
	inf = info_tab[c];
#define H(o)	((u32)t32_tp_le16(inf + (o)))
#define B(o)	((u32)inf[o])
#define W(slot, addr, val)	ops->wr(c, T33_MOD_BCSH, mode ? (slot) : (addr), (val))
#define HL(hi, lo, m, slot, addr)	W(slot, addr, (H(hi) & (m)) << 16 | (H(lo) & (m)))
#define V17	(((B(172) & 1) << 16) | (H(72) & 0x3ff))
#define V21	((H(114) & 0x3ff) << 16 | (B(174) & 0x3f) << 4 | (B(175) & 3))
	HL(58, 44, 0x3ff, 2, 0xf080);
	HL(92, 14, 0x3ff, 6, 0xf090);
	HL(150, 30, 0x3ff, 40, 0xf098);
	HL(100, 46, 0x3ff, 41, 0xf098);
	HL(168, 42, 0x3ff, 42, 0xf098);
	HL(124, 20, 0x3ff, 43, 0xf098);
	HL(18, 126, 0x3ff, 44, 0xf098);
	HL(4, 84, 0x3ff, 45, 0xf098);
	HL(12, 6, 0x3ff, 46, 0xf098);
	HL(34, 156, 0x3ff, 47, 0xf098);
	HL(162, 88, 0x3ff, 48, 0xf098);
	HL(160, 66, 0x3ff, 10, 0xf0d0);
	HL(142, 56, 0x1fff, 11, 0xf0d4);
	HL(140, 70, 0x1fff, 12, 0xf0d8);
	HL(78, 38, 0x3ff, 13, 0xf0dc);
	HL(82, 158, 0x3ff, 14, 0xf0e0);
	W(15, 0xf0e4, V17);
	HL(32, 0, 0x3ff, 16, 0xf0e8);
	HL(80, 144, 0x3ff, 17, 0xf0ec);
	W(18, 0xf0f0, H(8) & 0x3ff);
	W(21, 0xf0fc, V21);
	HL(22, 40, 0x3ff, 23, 0xf040);
	HL(164, 60, 0x3ff, 24, 0xf040);
	HL(64, 54, 0x3ff, 25, 0xf040);
	HL(10, 116, 0x3ff, 26, 0xf040);
	HL(132, 102, 0x3ff, 27, 0xf040);
	HL(28, 96, 0x3ff, 28, 0xf040);
	HL(50, 74, 0x7ff, 30, 0xf058);
	HL(106, 68, 0x7ff, 31, 0xf058);
	HL(148, 26, 0x7ff, 32, 0xf058);
	HL(130, 122, 0x3fff, 34, 0xf064);
	HL(48, 24, 0x3fff, 35, 0xf064);
	HL(98, 76, 0x3fff, 36, 0xf064);
	HL(36, 128, 0x3fff, 37, 0xf064);
	W(38, 0xf064, H(90) & 0x3fff);
	W(1, 0xf07c, (H(86) & 0x3ff) << 16 | (B(173) & 1));
	W(3, 0xf084, (H(104) & 0x3ff) << 16 | (B(170) & 1));
	HL(2, 62, 0x3ff, 4, 0xf088);
	W(5, 0xf08c, (H(166) & 0x3ff) << 16 | (B(177) & 1));
	W(7, 0xf094, (B(171) & 1) << 16);
	HL(134, 146, 0x7fff, 49, 0xf098);
	HL(118, 152, 0x7fff, 50, 0xf098);
	HL(120, 16, 0x7fff, 51, 0xf098);
	HL(136, 154, 0x7fff, 52, 0xf098);
	W(9, 0xf0cc, ((H(52) << 3) & 0xffff) | (H(138) & 0x1fff) << 16 | (B(176) & 1));
	W(15, 0xf0e4, V17);
	W(19, 0xf0f4, H(110) << 16 | H(94));
	W(20, 0xf0f8, H(108) << 16 | H(112));
	W(21, 0xf0fc, V21);
	W(21, 0xf0fc, V21);
#undef H
#undef B
#undef W
#undef HL
#undef V17
#undef V21
	*refresh = 1;
	return 1;
}

#endif /* TX_ISP_T33_BCSHO_H */
