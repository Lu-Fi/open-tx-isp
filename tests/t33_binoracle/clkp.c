/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "clkp": the clock-parent helper (driver/t32/tx_isp_t33_clkp.h) with the vendor
 * calling convention and callees, against ispcore_set_clk_parent / ivdc_set_clk_parent (spec: clkp.conf).
 */
#include <stdint.h>
#include "tx_isp_t33_clkp.h"

extern void *private_devm_clk_get(void *dev, const char *name);
extern void private_devm_clk_put(void *dev, void *clk);
extern void *clk_get(void *dev, const char *name);
extern int clk_set_parent(void *clk, void *parent);
extern void clk_put(void *clk);
extern int isp_printf(int level, const char *fmt, ...);

static void a_log(const char *fmt, const char *arg)
{
	if (arg)
		isp_printf(1, fmt, arg);
	else
		isp_printf(1, fmt);
}

static const struct t33_clkp_ops bo_ops = {
	.devm_clk_get = private_devm_clk_get, .devm_clk_put = private_devm_clk_put, .clk_get = clk_get,
	.clk_set_parent = clk_set_parent, .clk_put = clk_put, .log = a_log,
};

void bo_clkp(void **devp, const char *parent, const char *name) { t33_set_clk_parent(&bo_ops, devp, parent, name); }
