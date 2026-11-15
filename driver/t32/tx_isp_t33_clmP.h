/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 CSC attribute glue of the vendor module (binary oracle block "clmP"): the CSC refresh of the CLM and
 * BCSH blocks and the get / set attribute entry points.  The block functions they call are ported elsewhere
 * (tx_isp_t33_clm.h, tx_isp_t33_bcsh.h) and reached through the ops table.
 */
#ifndef TX_ISP_T33_CLMP_H
#define TX_ISP_T33_CLMP_H

#include "tx_isp_t33_blk.h"

struct t33_clmp_ops {
	int (*csc_get)(u32 ch, u8 *dst);
	int (*clm_calc)(u32 ch);
	int (*clm_write)(u32 ch);
	int (*bcsh_ev)(u32 ch, u32 a1, u32 lo, u32 hi, u32 force);
	int (*bcsh_ct)(u32 ch, u32 a1, u32 ct, u32 z, u32 force);
	int (*bcsh_calc)(u32 ch, u32 mode);
	int (*api_get)(u32 ch, u32 a1);
	int (*api_set)(u32 ch, u32 a1);
};

/* tisp_clm_refresh_by_csc */
static inline int t33_clm_refresh_by_csc(const struct t33_clmp_ops *ops, u8 *const *clm_info, u32 ch)
{
	u8 *p = clm_info[ch];

	ops->csc_get(ch & 0xff, p + 4);
	ops->clm_calc(ch);
	ops->clm_write(ch);
	return 0;
}

/* tisp_bcsh_refresh_by_csc */
static inline int t33_bcsh_refresh_by_csc(const struct t33_clmp_ops *ops, u8 *const *bcsh_para, u32 ch)
{
	u8 *p = bcsh_para[ch];

	ops->csc_get(ch & 0xff, p + 504);
	ops->bcsh_ev(ch, (u32)(unsigned long)(p + 504), t33_le32(p + 8), t33_le32(p + 12), 1);
	ops->bcsh_ct(ch, (u32)(unsigned long)(p + 504), t33_le32(p + 32), 0, 1);
	ops->bcsh_calc(ch, 0);
	return 0;
}

/* tisp_get_csc_attr */
static inline int t33_get_csc_attr(const struct t33_clmp_ops *ops, u32 ch, u32 a1)
{
	return ops->api_get(ch & 0xff, a1);
}

/* tisp_set_csc_attr */
static inline int t33_set_csc_attr(const struct t33_clmp_ops *ops, u8 *const *bcsh_para, u8 *const *clm_info,
				   u32 ch, u32 a1)
{
	ops->api_set(ch & 0xff, a1);
	t33_bcsh_refresh_by_csc(ops, bcsh_para, ch);
	t33_clm_refresh_by_csc(ops, clm_info, ch);
	return 0;
}

#endif /* TX_ISP_T33_CLMP_H */
