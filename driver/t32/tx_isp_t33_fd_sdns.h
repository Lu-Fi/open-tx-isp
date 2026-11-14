/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 SDNS frame-done handler (vendor tisp_sdns_t33_frame_done_interrupt).
 * The callees (register write, dma state, cfg trigger) are passed in, so the
 * handler stays independent of the runtime that implements them.
 */
#ifndef TX_ISP_T33_FD_SDNS_H
#define TX_ISP_T33_FD_SDNS_H

#include "tx_isp_t33_blk.h"

#define T33_SDNS_ID	20

struct t33_sdns_fd_ops {
	int (*reg_write)(u32 ch, u32 mod, u32 reg, u32 val);
	int (*dma_state)(u32 mod);
	int (*cfg_trig)(u32 ch, u32 mod);
};

static inline u32 t33_sdns_fd_reg(u32 cfg, u32 slot, u32 addr)
{
	return cfg ? slot : addr;
}

static inline u32 t33_sdns_fd_pack(u32 b0, u32 w1, u32 w2, u32 b3)
{
	return b0 | w1 << 24 | ((w2 << 8) & 0xffff) | b3 << 16;
}

/*
 * init: stIspInitPar words; refresh: tispSdnsRefresh; cfg: sdnsCfgMode;
 * info: the per-channel sdnsInfo[ch] pointer.  Returns 1 like the vendor.
 */
static inline int t33_sdns_frame_done(u32 a0, const u32 *init, u8 *refresh, u8 cfg,
				      const u8 *const *infos, const struct t33_sdns_fd_ops *ops)
{
	u32 ch = a0 & 0xff;
	const u8 *inf;

	if (init[226] != 1)
		return 1;
	if (*refresh == 1) {
		ops->cfg_trig(ch, T33_SDNS_ID);
		*refresh = 0;
		return 1;
	}
	if (*refresh != 2)
		return 1;
	if (ops->dma_state(T33_SDNS_ID))
		return 1;
	inf = infos[ch];
	ops->reg_write(ch, T33_SDNS_ID, t33_sdns_fd_reg(cfg, 5, 0x13028),
		       t33_sdns_fd_pack(inf[76], t33_le32(inf + 88), t33_le32(inf + 80), inf[84]));
	ops->reg_write(ch, T33_SDNS_ID, t33_sdns_fd_reg(cfg, 6, 0x1302c),
		       t33_sdns_fd_pack(inf[92], t33_le32(inf + 104), t33_le32(inf + 96), inf[100]));
	ops->reg_write(ch, T33_SDNS_ID, t33_sdns_fd_reg(cfg, 7, 0x13030),
		       ((t33_le32(inf + 112) << 8) & 0xffff) | (u32)inf[116] << 16 |
		       (t33_le32(inf + 108) & 0x3f) | (t33_le32(inf + 120) & 0x3f) << 24);
	ops->reg_write(ch, T33_SDNS_ID, t33_sdns_fd_reg(cfg, 8, 0x13034), t33_le32(inf + 124) & 0x1f);
	ops->reg_write(ch, T33_SDNS_ID, t33_sdns_fd_reg(cfg, 9, 0x13038),
		       t33_sdns_fd_pack(inf[128], t33_le32(inf + 140), t33_le32(inf + 132), inf[136]));
	inf = infos[ch];
	ops->reg_write(ch, T33_SDNS_ID, t33_sdns_fd_reg(cfg, 3, 0x13020), t33_le32(inf + 68) & 3);
	ops->reg_write(ch, T33_SDNS_ID, t33_sdns_fd_reg(cfg, 4, 0x13024), t33_le32(inf + 72) & 3);
	ops->reg_write(ch, T33_SDNS_ID, t33_sdns_fd_reg(cfg, 7, 0x13030),
		       ((t33_le32(inf + 112) << 8) & 0xffff) | (u32)inf[116] << 16 |
		       (t33_le32(inf + 108) & 0x3f) | (t33_le32(inf + 120) & 0x3f) << 24);
	ops->reg_write(ch, T33_SDNS_ID, t33_sdns_fd_reg(cfg, 1, 0x13010), 1);
	*refresh = 1;
	return 1;
}

#endif /* TX_ISP_T33_FD_SDNS_H */
