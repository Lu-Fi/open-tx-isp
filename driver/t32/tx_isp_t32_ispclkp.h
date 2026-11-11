/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 12: ispcore_set_clk_parent.isra.13 (0xb7410).
 * (dev, parent, clk): parent must be one of "sclka", "mpll", "vpll" (t32_data_ce220); get the
 * device clock clk, get the parent clock by name, clk_set_parent, put the parent, put the
 * device clock; errors are logged (isp_printf level 1).
 *
 * Vendor behaviour kept: the device clock is put also when devm_clk_get returned an error
 * pointer, and also when the parent name is unknown.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispclkp_oracle).
 */
#ifndef TX_ISP_T32_ISPCLKP_H
#define TX_ISP_T32_ISPCLKP_H

#include "tx_isp_t32_ispcore.h"

struct t32_clkp_ops {
	u32 (*devm_clk_get)(u32 dev, u32 name);
	void (*devm_clk_put)(u32 dev, u32 clk);
	u32 (*clk_get)(u32 dev, const char *name);
	int (*clk_set_parent)(u32 clk, u32 parent);
	void (*clk_put)(u32 clk);
	void (*log)(u32 msg, u32 arg);	/* isp_printf(1, msg 0 get / 1 parent / 2 set_parent, arg) */
};

static const char *const t32_clk_parents[3] = { "sclka", "mpll", "vpll" };	/* t32_data_ce220 */

/* devp: the isra-split pointer to the device */
static inline void t32_ispcore_set_clk_parent(const struct t32_clkp_ops *ops, u32 devp, u32 parent, u32 name)
{
	u32 k, c, p;
	int r = 0;

	for (k = 0; k < 3; k++)
		if (!strcmp(t32_clk_parents[k], (const char *)T32_IC_P(parent)))
			break;
	c = ops->devm_clk_get(t32_ic_r32(devp, 0), name);
	if (c >= (u32)-4095) {
		ops->log(0, name);
	} else if (k == 3) {
		ops->log(1, parent);
	} else {
		p = ops->clk_get(0, t32_clk_parents[k]);
		r = ops->clk_set_parent(c, p);
		ops->clk_put(p);
	}
	ops->devm_clk_put(t32_ic_r32(devp, 0), c);
	if (r)
		ops->log(2, 0);
}

#endif /* TX_ISP_T32_ISPCLKP_H */
