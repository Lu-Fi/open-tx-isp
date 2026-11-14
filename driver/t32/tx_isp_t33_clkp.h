/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 clock parents: ispcore_set_clk_parent.isra.10 and ivdc_set_clk_parent.isra.8 (the same
 * instructions), called by ispcore_activate_module (clk_name -> "div_isp", clkl_name -> "div_ldc")
 * and ivdc_activate_module (clka_name -> "div_ispa", clkv_name -> "div_vpu") on every activation.
 *
 * (dev, parent, name): parent is looked up in the vendor's sclk_name table, which on the T33 has
 * only "sclka" and "mpll" (the T32 also accepts "vpll"); the device clock `name` is taken with
 * devm_clk_get; an error pointer logs "get %s failed!" (name), an unknown parent logs
 * "The %s was not found!" (parent) and nothing is set; else the parent clock is taken with
 * clk_get(NULL, parent), set with clk_set_parent and put.  The device clock is put in every case
 * (also an error pointer, as the vendor); a failed clk_set_parent logs "Failed to modify the parent
 * clock.".  All logs at isp_printf level 1.
 *
 * Checked against the vendor instructions by tests/t33_binoracle (block clkp, openimp #99).
 */
#ifndef TX_ISP_T33_CLKP_H
#define TX_ISP_T33_CLKP_H

struct t33_clkp_ops {
	void *(*devm_clk_get)(void *dev, const char *name);	/* private_devm_clk_get */
	void (*devm_clk_put)(void *dev, void *clk);		/* private_devm_clk_put */
	void *(*clk_get)(void *dev, const char *name);
	int (*clk_set_parent)(void *clk, void *parent);
	void (*clk_put)(void *clk);
	void (*log)(const char *fmt, const char *arg);		/* isp_printf(1, fmt, arg) */
};

static const char *const t33_sclk_name[2] = { "sclka", "mpll" };	/* sclk_name */

static inline int t33_clkp_streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

/* devp: the isra-split pointer to the device (*devp = struct device *) */
static inline void t33_set_clk_parent(const struct t33_clkp_ops *ops, void **devp, const char *parent,
				      const char *name)
{
	unsigned int k;
	void *c, *p;
	int r = 0;

	for (k = 0; k < 2; k++)
		if (t33_clkp_streq(t33_sclk_name[k], parent))
			break;
	c = ops->devm_clk_get(*devp, name);
	if ((unsigned long)c >= (unsigned long)-4095)
		ops->log("get %s failed!\n", name);
	else if (k == 2)
		ops->log("The %s was not found!\n", parent);
	else {
		p = ops->clk_get(0, t33_sclk_name[k]);
		r = ops->clk_set_parent(c, p);
		ops->clk_put(p);
	}
	ops->devm_clk_put(*devp, c);
	if (r)
		ops->log("Failed to modify the parent clock.\n", 0);
}

#endif /* TX_ISP_T33_CLKP_H */
