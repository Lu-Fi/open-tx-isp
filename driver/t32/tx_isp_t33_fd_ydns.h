/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 YDNS frame-done handler (vendor tisp_ydns_t33_frame_done_interrupt).
 *
 * The vendor refresh state byte: 1 = the register list is to be triggered,
 * 2 = the new values are to be written once the DMA of the module is idle.
 * The callees (register write, rcfg trigger, DMA state) are passed in, they
 * are provided elsewhere (runtime tx_isp_t32_hw.c).
 */
#ifndef TX_ISP_T33_FD_YDNS_H
#define TX_ISP_T33_FD_YDNS_H

#include "tx_isp_t33_blk.h"

#define T33_YDNS_FD_MOD	17

struct t33_ydns_fd_ops {
	void (*reg_write)(u32 ch, u32 mod, u32 reg, u32 val);
	int (*dma_state)(u32 mod);
	void (*trig)(u32 ch, u32 mod);
};

/* packs four bytes/words of the info struct: b0 | w3 << 24 | (w2 << 8) & 0xffff | b1 << 16 */
static inline u32 t33_ydns_fd_pack(const u8 *inf, unsigned int o)
{
	return inf[o] | (t33_le32(inf + o + 12) << 24) | ((t33_le32(inf + o + 4) << 8) & 0xffff) |
	       ((u32)inf[o + 8] << 16);
}

/* vendor tisp_ydns_t33_frame_done_interrupt; always returns 1 */
static inline int t33_ydns_frame_done(u32 ch, u32 init_par904, u8 *state, u32 cfg_mode, const u8 *inf,
				      const struct t33_ydns_fd_ops *ops)
{
	u32 v, k;

	ch &= 0xff;
	if (init_par904 != 1)
		return 1;
	if (*state == 1) {
		ops->trig(ch, T33_YDNS_FD_MOD);
		*state = 0;
		return 1;
	}
	if (*state != 2)
		return 1;
	if (ops->dma_state(T33_YDNS_FD_MOD)) {
		*state = 2;
		return 1;
	}
	v = ((t33_le32(inf + 56) & 3) << 8) | ((t33_le32(inf + 60) & 3) << 12) | inf[52];
	for (k = 0; k < 3; k++)
		ops->reg_write(ch, T33_YDNS_FD_MOD, cfg_mode ? 4 : 0xe054, v);
	ops->reg_write(ch, T33_YDNS_FD_MOD, cfg_mode ? 5 : 0xe058,
		       inf[64] | (t33_le32(inf + 76) << 24) | ((t33_le32(inf + 68) << 8) & 0xffff) |
		       ((u32)inf[72] << 16));
	ops->reg_write(ch, T33_YDNS_FD_MOD, cfg_mode ? 6 : 0xe05c, t33_ydns_fd_pack(inf, 80));
	ops->reg_write(ch, T33_YDNS_FD_MOD, cfg_mode ? 3 : 0xe050, t33_le32(inf + 48) & 1);
	ops->reg_write(ch, T33_YDNS_FD_MOD, cfg_mode ? 1 : 0xe010, 1);
	*state = 1;
	return 1;
}

#endif /* TX_ISP_T33_FD_YDNS_H */
