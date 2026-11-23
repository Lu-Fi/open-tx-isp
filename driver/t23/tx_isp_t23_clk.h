/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T23 ISP core clocks (openimp #813): parent clock and rates from the module
 * parameters clk_name / clka_name / isp_clk / isp_clka, as pure helpers
 * shared with tests/t23_clk_host.c.  Written from the decompiled vendor flow
 * of tx-isp-t23.ko (ISVP T23 1.3.0, audit/t23dl-alfonso/v/ko_all.c), no
 * vendor code copied:
 *
 *   ispcore_activate_module @0x7a2d0 (ko_all.c:53276-53293)
 *     ispcore_set_clk_parent(clk_name,  "cgu_isp")
 *     ispcore_set_clk_parent(clka_name, "cgu_vpu")
 *     clks[0] (cgu_isp) = isp_clk, clks[1] (cgu_vpu) = isp_clka, then enable
 *   ispcore_set_clk_parent.isra.0 @0x78e40 (ko_all.c:52213-52265)
 *     the parent name must be one of sclk_name[] = { "mpll", "sclka" },
 *     else "The %s was not found!"; clk_set_parent(clk_get(0, clk),
 *     clk_get(0, parent)), "Failed to modify the parent clock." on error
 *
 * Differences (better than the vendor): an unset clk_name / clka_name skips
 * the parent change (the vendor string compare dereferences the NULL
 * pointer), a rate of 0 is not applied, and the clock references taken for
 * the parent change are put back.
 */
#ifndef TX_ISP_T23_CLK_H
#define TX_ISP_T23_CLK_H

struct t23_clk_ops {
	void *(*get)(const char *name);          /* NULL or error -> not found */
	void (*put)(void *clk);
	int (*set_parent)(void *clk, void *parent);
	int (*set_rate)(void *clk, unsigned long rate);
	void (*log)(const char *msg, const char *arg);
};

/* sclk_name[] of the vendor module, index = position in the table. */
static inline int t23_clk_parent_index(const char *name)
{
	static const char *const sclk[2] = { "mpll", "sclka" };
	int i, k;

	if (!name)
		return -1;
	for (i = 0; i < 2; i++) {
		for (k = 0; sclk[i][k] && sclk[i][k] == name[k]; k++)
			;
		if (sclk[i][k] == name[k])
			return i;
	}
	return -1;
}

/* Returns 0 when the parent was set, 1 when skipped (no name), <0 on error. */
static inline int t23_clk_set_parent(const struct t23_clk_ops *ops,
				     const char *parent, const char *clk_id)
{
	void *clk, *p;
	int ret;

	if (!parent || !parent[0])
		return 1;
	if (t23_clk_parent_index(parent) < 0) {
		ops->log("The %s was not found!\n", parent);
		return -2;
	}
	clk = ops->get(clk_id);
	if (!clk) {
		ops->log("get %s failed!\n", clk_id);
		return -19;
	}
	p = ops->get(parent);
	if (!p) {
		ops->log("get %s failed!\n", parent);
		ops->put(clk);
		return -19;
	}
	ret = ops->set_parent(clk, p);
	if (ret)
		ops->log("Failed to modify the parent clock (%s).\n", clk_id);
	ops->put(p);
	ops->put(clk);
	return ret;
}

/*
 * The vendor activation clock step: parents first, then clks[0] = isp_clk,
 * clks[1] = isp_clka.  clks/n = the core's clock table (isp_core_clk_info:
 * cgu_isp, cgu_vpu, isp).  Returns the number of rates applied.
 */
static inline int t23_core_apply_clk_params(const struct t23_clk_ops *ops,
					    void *const *clks, unsigned int n,
					    const char *clk_name,
					    const char *clka_name,
					    unsigned long isp_clk,
					    unsigned long isp_clka)
{
	unsigned int i;
	int applied = 0;

	t23_clk_set_parent(ops, clk_name, "cgu_isp");
	t23_clk_set_parent(ops, clka_name, "cgu_vpu");
	for (i = 0; i < n && i < 2; i++) {
		unsigned long rate = i == 0 ? isp_clk : isp_clka;

		if (!clks[i] || !rate)
			continue;
		if (ops->set_rate(clks[i], rate) == 0)
			applied++;
		else
			ops->log("Failed to set %s clock rate\n",
				 i == 0 ? "cgu_isp" : "cgu_vpu");
	}
	return applied;
}

#endif /* TX_ISP_T23_CLK_H */
