/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 DPC block: frame-done interrupt handler (vendor tisp_dpc_t33_frame_done_interrupt).
 *
 * After a refresh request (state 2) the handler writes the whole parameter set of the channel's
 * info struct into the DPC registers (module 2).  Every register has two addresses: the direct one
 * (cfg mode 0) and a list slot (cfg mode != 0).  The callees are passed in as a small table.
 */
#ifndef TX_ISP_T33_FD_DPC_H
#define TX_ISP_T33_FD_DPC_H

#include "tx_isp_t33_blk.h"

struct t33_dpc_hw {
	int (*reg_write)(u32 ch, u32 module, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	int (*dma_state)(u32 id);				/* tisp_rcfg_get_dma_state */
	int (*cfg_trig)(u32 ch, u32 module);			/* tisp_rcfg_module_cfg_trig */
};

#define T33_DPC_MODULE	2

/* masked word of the info struct at a byte offset */
static inline u32 t33_dpc_f(const u8 *info, unsigned int off, u32 mask)
{
	return t33_le32(info + off) & mask;
}

static inline void t33_dpc_wr(const struct t33_dpc_hw *hw, u32 ch, u32 mode, u32 r0, u32 r1, u32 v)
{
	hw->reg_write(ch, T33_DPC_MODULE, mode ? r1 : r0, v);
}

/* two 12-bit fields: low word at lo, high word at lo + 4 */
static inline u32 t33_dpc_pair12(const u8 *info, unsigned int lo)
{
	return t33_dpc_f(info, lo + 4, 0xfff) << 16 | t33_dpc_f(info, lo, 0xfff);
}

static inline u32 t33_dpc_v18(const u8 *info)
{
	return t33_dpc_f(info, 564, 0x1ff) << 16 | t33_dpc_f(info, 560, 1);
}

static inline u32 t33_dpc_v19(const u8 *info)
{
	return t33_dpc_f(info, 576, 3) << 8 | t33_dpc_f(info, 572, 0xfff) << 16 | t33_dpc_f(info, 568, 1);
}

static inline u32 t33_dpc_v34(const u8 *info)
{
	return t33_dpc_f(info, 548, 0x1ff) << 16 | t33_dpc_f(info, 544, 3);
}

/* tisp_dpc_t33_frame_done_interrupt; returns 1 */
static inline int t33_dpc_frame_done(const struct t33_dpc_hw *hw, u32 arg, u32 init, u8 *refresh,
				     u32 mode, const u8 *info)
{
	u32 ch = arg & 0xff, v;

	if (init != 1)
		return 1;
	if (*refresh == 1) {
		hw->cfg_trig(ch, 2);
		*refresh = 0;
		return 1;
	}
	if (*refresh != 2)
		return 1;
	mode &= 0xff;

	v = t33_dpc_f(info, 324, 1) | t33_dpc_f(info, 328, 1) << 1 | t33_dpc_f(info, 332, 1) << 2 |
	    t33_dpc_f(info, 336, 1) << 3 | t33_dpc_f(info, 356, 1) << 4 | t33_dpc_f(info, 360, 1) << 5 |
	    t33_dpc_f(info, 364, 1) << 6 | t33_dpc_f(info, 380, 1) << 8 | t33_dpc_f(info, 384, 1) << 9 |
	    t33_dpc_f(info, 396, 1) << 12 | t33_dpc_f(info, 340, 1) << 16 | t33_dpc_f(info, 344, 1) << 17 |
	    t33_dpc_f(info, 348, 1) << 18 | t33_dpc_f(info, 352, 1) << 19 | t33_dpc_f(info, 368, 1) << 20 |
	    t33_dpc_f(info, 372, 1) << 21 | t33_dpc_f(info, 376, 1) << 22 | t33_dpc_f(info, 388, 1) << 24 |
	    t33_dpc_f(info, 392, 1) << 25 | t33_dpc_f(info, 400, 1) << 28;
	t33_dpc_wr(hw, ch, mode, 0x204c, 6, v);
	t33_dpc_wr(hw, ch, mode, 0x205c, 10, t33_dpc_pair12(info, 428));
	v = (t33_dpc_f(info, 440, ~0U) << 8 & 0xffff) | info[436];
	t33_dpc_wr(hw, ch, mode, 0x2060, 11, v);
	/* the vendor writes the next pairs to the same direct address in mode 0 */
	t33_dpc_wr(hw, ch, mode, 0x2064, 36, t33_dpc_pair12(info, 444));
	t33_dpc_wr(hw, ch, mode, 0x2064, 37, t33_dpc_pair12(info, 452));
	t33_dpc_wr(hw, ch, mode, 0x206c, 39, t33_dpc_pair12(info, 460));
	t33_dpc_wr(hw, ch, mode, 0x206c, 40, t33_dpc_pair12(info, 468));
	t33_dpc_wr(hw, ch, mode, 0x2074, 13, t33_dpc_pair12(info, 476));
	t33_dpc_wr(hw, ch, mode, 0x2078, 14, t33_dpc_pair12(info, 488));
	v = (t33_dpc_f(info, 500, ~0U) << 8 & 0xffff) | info[496];
	t33_dpc_wr(hw, ch, mode, 0x207c, 15, v);
	t33_dpc_wr(hw, ch, mode, 0x2080, 42, t33_dpc_pair12(info, 504));
	t33_dpc_wr(hw, ch, mode, 0x2080, 43, t33_dpc_pair12(info, 512));
	t33_dpc_wr(hw, ch, mode, 0x2088, 45, t33_dpc_pair12(info, 520));
	t33_dpc_wr(hw, ch, mode, 0x2088, 46, t33_dpc_pair12(info, 528));
	t33_dpc_wr(hw, ch, mode, 0x2090, 17, t33_dpc_pair12(info, 536));
	t33_dpc_wr(hw, ch, mode, 0x2094, 18, t33_dpc_v18(info));
	t33_dpc_wr(hw, ch, mode, 0x2098, 19, t33_dpc_v19(info));
	t33_dpc_wr(hw, ch, mode, 0x20a0, 21, t33_dpc_pair12(info, 588));
	t33_dpc_wr(hw, ch, mode, 0x2170, 34, t33_dpc_v34(info));

	v = t33_dpc_f(info, 296, 1) << 8 | t33_dpc_f(info, 300, 1) << 12 | t33_dpc_f(info, 288, 1) |
	    t33_dpc_f(info, 552, 0x7ff) << 16;
	t33_dpc_wr(hw, ch, mode, 0x2040, 3, v);
	v = t33_dpc_f(info, 304, 0x1f) << 8 | t33_dpc_f(info, 424, 0x3f) << 16 | t33_dpc_f(info, 292, 0x1f) |
	    t33_dpc_f(info, 484, 0x3f) << 24;
	t33_dpc_wr(hw, ch, mode, 0x2044, 4, v);
	v = t33_dpc_f(info, 312, 1) << 8 | t33_dpc_f(info, 316, 1) << 16 | t33_dpc_f(info, 308, 1) |
	    t33_dpc_f(info, 320, 3) << 24;
	t33_dpc_wr(hw, ch, mode, 0x2048, 5, v);
	v = t33_dpc_f(info, 408, 3) << 4 | t33_dpc_f(info, 412, 3) << 8 | t33_dpc_f(info, 404, 3);
	t33_dpc_wr(hw, ch, mode, 0x2054, 8, v);
	v = t33_dpc_f(info, 420, 3) << 4 | t33_dpc_f(info, 416, 3);
	t33_dpc_wr(hw, ch, mode, 0x2058, 9, v);
	t33_dpc_wr(hw, ch, mode, 0x2094, 18, t33_dpc_v18(info));
	t33_dpc_wr(hw, ch, mode, 0x2098, 19, t33_dpc_v19(info));
	v = t33_dpc_f(info, 584, 3) << 8 | t33_dpc_f(info, 596, 0x3f) << 16 | t33_dpc_f(info, 580, 3);
	t33_dpc_wr(hw, ch, mode, 0x209c, 20, v);
	v = info[600] | t33_le32(info + 612) << 24 | (t33_le32(info + 604) << 8 & 0xffff) | (u32)info[608] << 16;
	t33_dpc_wr(hw, ch, mode, 0x20a4, 22, v);
	v = t33_dpc_f(info, 620, 0x3f) << 8 | t33_dpc_f(info, 624, 0x3f) << 16 | t33_dpc_f(info, 616, 0x3f) |
	    t33_dpc_f(info, 628, 0x3f) << 24;
	t33_dpc_wr(hw, ch, mode, 0x20b4, 24, v);
	t33_dpc_wr(hw, ch, mode, 0x20c4, 26, t33_dpc_f(info, 284, 7));
	v = t33_dpc_f(info, 636, 0x1ff) << 4 | t33_dpc_f(info, 640, 0x1ff) << 16 | t33_dpc_f(info, 632, 1);
	t33_dpc_wr(hw, ch, mode, 0x20c8, 27, v);
	v = t33_dpc_f(info, 648, 0x1ff) << 12 | t33_dpc_f(info, 644, 0x1ff);
	t33_dpc_wr(hw, ch, mode, 0x20cc, 28, v);
	v = t33_dpc_f(info, 656, 0x1ff) << 12 | t33_dpc_f(info, 652, 0x1ff);
	t33_dpc_wr(hw, ch, mode, 0x20e0, 30, v);
	v = t33_dpc_f(info, 664, 0x7ff) << 12 | t33_dpc_f(info, 668, 0x1f) << 24 | t33_dpc_f(info, 660, 0xfff);
	t33_dpc_wr(hw, ch, mode, 0x20e4, 31, v);
	v = t33_dpc_f(info, 676, 0xfff) << 8 | t33_dpc_f(info, 672, 0x1f);
	t33_dpc_wr(hw, ch, mode, 0x20e8, 32, v);
	t33_dpc_wr(hw, ch, mode, 0x2170, 34, t33_dpc_v34(info));
	t33_dpc_wr(hw, ch, mode, 0x2010, 1, 1);

	*refresh = hw->dma_state(2) ? 2 : 1;
	return 1;
}

#endif /* TX_ISP_T33_FD_DPC_H */
