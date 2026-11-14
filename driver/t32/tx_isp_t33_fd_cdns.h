/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 CDNS block frame-done handler (vendor tisp_cdns_t33_frame_done_interrupt).
 * Refresh state 1: trigger the module list; state 2: when the DMA is idle write
 * the per-channel parameter block (cdnsInfo[ch]) to the module registers.
 */
#ifndef TX_ISP_T33_FD_CDNS_H
#define TX_ISP_T33_FD_CDNS_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_CDNS	21

struct t33_cdns_ops {
	int (*reg_write)(u32 id, u32 mod, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	int (*dma_state)(u32 mod);				/* tisp_rcfg_get_dma_state */
	int (*cfg_trig)(u32 id, u32 mod);			/* tisp_rcfg_module_cfg_trig */
};

static inline u32 t33_cdns_w(const u8 *i, u32 o)
{
	return t33_le32(i + o);
}

/* register address (cfg mode 0) or list slot (cfg mode != 0) */
static inline u32 t33_cdns_reg(u32 mode, u32 slot, u32 off)
{
	return mode ? slot : 0x12000 + off;
}

/* three-field pack: byte at ob, word at ob + 4 (shifted 8, 16 bit), byte at ob + 8 << 16 */
static inline u32 t33_cdns_pack3(const u8 *i, u32 ob)
{
	return ((t33_cdns_w(i, ob + 4) << 8) & 0xffff) | (u32)i[ob + 8] << 16 | i[ob];
}

static inline u32 t33_cdns_v18(const u8 *i)
{
	return (u32)i[372] << 4 | (t33_cdns_w(i, 368) & 1);
}

static inline int t33_cdns_frame_done(const struct t33_cdns_ops *ops, u32 ch, u32 init_flag, u8 *refresh,
				      u32 mode, u8 *const *info)
{
	const u8 *i;
	u32 id = ch & 0xff, k;

	if (init_flag != 1)
		return 1;
	if (*refresh == 1) {
		ops->cfg_trig(id, T33_MOD_CDNS);
		*refresh = 0;
		return 1;
	}
	if (*refresh != 2)
		return 1;
	if (ops->dma_state(T33_MOD_CDNS))
		return 1;
	i = info[id];
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 3, 0x40), t33_cdns_w(i, 216) & 7);
	for (k = 0; k < 8; k++)
		ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 5 + k, 0x50 + 4 * k),
			       t33_cdns_pack3(i, 220 + 12 * k));
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 13, 0x70),
		       i[316] | t33_cdns_w(i, 328) << 24 | (t33_cdns_w(i, 320) & 0x3f) << 8 |
		       (t33_cdns_w(i, 324) & 0x3f) << 16);
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 14, 0x74),
		       (t33_cdns_w(i, 336) & 0x3f) << 8 | (t33_cdns_w(i, 340) & 0x3f) << 16 |
		       (t33_cdns_w(i, 332) & 7));
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 15, 0x78), t33_cdns_w(i, 344) & 3);
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 16, 0x7c),
		       i[348] | (t33_cdns_w(i, 352) & 0xf) << 16);
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 17, 0x80),
		       (t33_cdns_w(i, 360) & 0x3f) << 8 | (t33_cdns_w(i, 364) & 0x3f) << 16 |
		       (t33_cdns_w(i, 356) & 7));
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 18, 0x84), t33_cdns_v18(i));
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 19, 0x88),
		       (t33_cdns_w(i, 404) & 0x1f) << 8 | (t33_cdns_w(i, 408) & 0x3f) << 16 |
		       (t33_cdns_w(i, 400) & 0x3f) | (t33_cdns_w(i, 412) & 0x3f) << 24);
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 20, 0x8c),
		       ((t33_cdns_w(i, 420) << 8) & 0xffff) | (u32)i[428] << 16 |
		       (t33_cdns_w(i, 416) & 0x1f) | (t33_cdns_w(i, 424) & 1) << 24);
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 21, 0x90),
		       (t33_cdns_w(i, 388) & 0x3f) << 8 | (t33_cdns_w(i, 392) & 0x3f) << 16 |
		       (t33_cdns_w(i, 384) & 0x3f) | (t33_cdns_w(i, 396) & 0x3f) << 24);
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 22, 0x94),
		       (t33_cdns_w(i, 380) & 0x3f) << 8 | (t33_cdns_w(i, 376) & 0x3f));
	i = info[id];
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 18, 0x84), t33_cdns_v18(i));
	ops->reg_write(id, T33_MOD_CDNS, t33_cdns_reg(mode, 1, 0x10), 1);
	*refresh = 1;
	return 1;
}

#endif /* TX_ISP_T33_FD_CDNS_H */
