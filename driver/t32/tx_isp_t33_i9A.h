/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) denoise block init: tisp_ydns_init, tisp_sdns_init, tisp_cdns_init.
 * Each registers the block's register list, allocates the per-channel state, stores the IQ section
 * pointer / gain tolerance there, runs the block refresh at the init gain and hooks the frame-done irq.
 * The callees (module register, allocator, refresh, irq hook) are passed in through t33_dnsi_ops.
 */
#ifndef TX_ISP_T33_I9A_H
#define TX_ISP_T33_I9A_H

#include "tx_isp_t33_blk.h"

struct t33_dnsi_ops {
	int (*reg_register)(u32 ch, u32 id, void *cb);	/* tisp_rcfg_module_register */
	void *(*alloc)(u32 size, u32 flags);		/* private_kmalloc */
	void (*alloc_fail)(u32 line);			/* isp_printf (never reached by the oracle) */
	void (*reg_fail)(u32 line, int ret);		/* printk of a failed registration */
	void (*refresh)(u32 ch, u32 gain);		/* tisp_<blk>_refresh */
	void (*irq_set)(u32 ch, u32 idx, void *cb);	/* system_irq_func_set */
	void *reg_cb;					/* tisp_rcfg_<blk>_reg_init */
	void *irq_cb;					/* tisp_<blk>_t33_frame_done_interrupt */
};

static inline u32 *t33_dnsi_slot(u32 *slots, u32 ch, void *p)
{
	slots[ch] = (u32)(unsigned long)p;
	return (u32 *)(unsigned long)slots[ch];
}

/* tisp_ydns_init */
static inline int t33_ydns_init_ch(u32 ch, const u8 *par, u32 *slots, const u32 *tparams,
				   const struct t33_dnsi_ops *ops)
{
	u32 *inf;
	void *m;

	ch &= 0xff;
	ops->reg_register(ch, 17, ops->reg_cb);
	m = ops->alloc(108, 208);
	inf = t33_dnsi_slot(slots, ch, m);
	if (!m)
		ops->alloc_fail(222);
	inf[24] = tparams[ch] + 0x88ac;
	inf[26] = 0xffffffff;
	inf[25] = t33_le32(par + 1000);
	ops->refresh(ch, t33_le32(par + 928));
	ops->irq_set(ch, 51, ops->irq_cb);
	return 0;
}

/* tisp_sdns_init */
static inline int t33_sdns_init_ch(u32 ch, const u8 *par, u32 *slots, const u32 *tparams,
				   const struct t33_dnsi_ops *ops)
{
	u32 *inf;
	void *m;
	int r;

	ch &= 0xff;
	r = ops->reg_register(ch, 20, ops->reg_cb);
	if (r < 0)
		ops->reg_fail(338, r);
	m = ops->alloc(160, 208);
	inf = t33_dnsi_slot(slots, ch, m);
	if (!m)
		ops->alloc_fail(343);
	inf[36] = tparams[ch] + 0x9b20;
	inf[37] = 0xffffffff;
	inf[39] = 128;
	inf[38] = t33_le32(par + 1000);
	ops->refresh(ch, t33_le32(par + 928));
	ops->irq_set(ch, 55, ops->irq_cb);
	return 0;
}

/* tisp_cdns_init; *cdns_refresh is tispCdnsRefresh */
static inline int t33_cdns_init_ch(u32 ch, const u8 *par, u32 *slots, const u32 *tparams,
				   u8 *cdns_refresh, const struct t33_dnsi_ops *ops)
{
	u32 *inf;
	void *m;
	int r;

	ch &= 0xff;
	r = ops->reg_register(ch, 21, ops->reg_cb);
	if (r < 0)
		ops->reg_fail(433, r);
	m = ops->alloc(444, 208);
	inf = t33_dnsi_slot(slots, ch, m);
	if (!m)
		ops->alloc_fail(438);
	inf[108] = tparams[ch] + 0x892c;
	inf[110] = t33_le32(par + 1000);
	ops->refresh(ch, t33_le32(par + 928));
	*cdns_refresh = 1;
	ops->irq_set(ch, 56, ops->irq_cb);
	return 0;
}

#endif /* TX_ISP_T33_I9A_H */
