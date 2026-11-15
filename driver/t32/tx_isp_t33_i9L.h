/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 teardown of the DMSC / DPC / YSP blocks (tisp_dmsc_deinit, tisp_dpc_deinit, tisp_ysp_deinit):
 * free the per-channel info block (kfree) and clear its slot, YSP also vfree()s ysp_paramsP[ch],
 * then release the register list of the block; a negative result goes to printk (line number, result).
 * OS calls are hooks.  Checked by tests/t33_binoracle (block i9L).
 */
#ifndef TX_ISP_T33_I9L_H
#define TX_ISP_T33_I9L_H

#include "tx_isp_t33_blk.h"

#define T33_I9L_LIST_DMSC	9
#define T33_I9L_LIST_DPC	2
#define T33_I9L_LIST_YSP	14

struct t33_i9l_ops {
	void (*kfree)(u32 p);			/* private_kfree */
	void (*vfree)(u32 p);			/* private_vfree */
	int (*release)(u32 ch, u32 id);		/* tisp_rcfg_module_release */
	void (*err)(u32 line, int ret);		/* printk */
};

/* tisp_dmsc_deinit */
static inline void t33_dmsc_deinit_ch(const struct t33_i9l_ops *ops, u32 *info, u32 ch)
{
	int r;

	ch &= 0xff;
	if (info[ch]) {
		ops->kfree(info[ch]);
		info[ch] = 0;
	}
	r = ops->release(ch, T33_I9L_LIST_DMSC);
	if (r < 0)
		ops->err(2402, r);
}

/* tisp_dpc_deinit */
static inline void t33_dpc_deinit_ch(const struct t33_i9l_ops *ops, u32 *info, u32 ch)
{
	int r;

	ch &= 0xff;
	if (info[ch]) {
		ops->kfree(info[ch]);
		info[ch] = 0;
	}
	r = ops->release(ch, T33_I9L_LIST_DPC);
	if (r < 0)
		ops->err(726, r);
}

/* tisp_ysp_deinit */
static inline void t33_ysp_deinit_ch(const struct t33_i9l_ops *ops, u32 *info, u32 *paramsp, u32 ch)
{
	int r;

	ch &= 0xff;
	if (info[ch]) {
		ops->kfree(info[ch]);
		info[ch] = 0;
	}
	if (paramsp[ch]) {
		ops->vfree(paramsp[ch]);
		paramsp[ch] = 0;
	}
	r = ops->release(ch, T33_I9L_LIST_YSP);
	if (r < 0)
		ops->err(1804, r);
}

#endif /* TX_ISP_T33_I9L_H */
