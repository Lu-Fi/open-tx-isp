/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IVDC clock setup from the vendor module (Matt Davis' recovered C,
 * PR #15): ivdc_set_clk_parent.isra.8 (0xeb80) and ivdc_activate_module
 * (0xee4c).
 *
 * set_clk_parent looks the parent name up in the vendor table
 * {"sclka", "mpll", "vpll"} (t32_data_ce220), gets the mux clock of the
 * device, re-parents it and drops both references.  activate_module (subdev
 * layout in tx_isp_t32_ivdc.h; device word +0x4) moves a slaked module (1)
 * to 2 under the spin lock, sets the ISP / VPU mux parents, brings clocks 0
 * and 1 to the rates of get_isp_clka / get_isp_clkv, enables every clock and
 * sets d98b4.  The clock framework calls are hooks of struct t32_ivdc_clk_os.
 *
 * Vendor quirks kept: set_clk_parent also hands an error clock to
 * devm_clk_put, and returns devm_clk_put's return value (a void function in
 * the kernel: a leftover register) unless the re-parenting failed, then the
 * result of its isp_printf; activate returns 0 for any state other than 1.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_ivclk_oracle).
 */
#ifndef TX_ISP_T32_IVDC_CLK_H
#define TX_ISP_T32_IVDC_CLK_H

#include "tx_isp_t32_ivdc.h"

#define T32_IVDC_SD_DEV		0x004U
#define T32_IVDC_ST_SLAKED	1U
#define T32_IVDC_ST_ACTIVE	2U

struct t32_ivdc_clk_os {
	u32 (*devm_clk_get)(u32 dev, const char *name);		/* private_devm_clk_get */
	u32 (*devm_clk_put)(u32 dev, u32 clk);			/* private_devm_clk_put */
	u32 (*clk_get)(u32 dev, const char *name);
	int (*clk_set_parent)(u32 clk, u32 parent);
	void (*clk_put)(u32 clk);
	u32 (*isp_clka)(void);					/* get_isp_clka */
	u32 (*isp_clkv)(void);					/* get_isp_clkv */
	const char *(*clka_name)(void);				/* get_clka_name */
	const char *(*clkv_name)(void);				/* get_clkv_name */
	u32 (*clk_get_rate)(u32 clk);
	void (*clk_set_rate)(u32 clk, u32 rate);
	void (*clk_prepare_enable)(u32 clk);
};

static const char *const t32_ivdc_clk_parents[3] = { "sclka", "mpll", "vpll" };	/* t32_data_ce220 */

static inline u32 t32_ivdc_set_clk_parent(const struct t32_ivdc_clk_os *co, const u8 *devp,
					  const char *parent, const char *mux)
{
	u32 i, clk, dev = t32_g_rd32(devp, 0), r;
	int err = 0;

	for (i = 0; i < 3; i++)
		if (!strcmp(parent, t32_ivdc_clk_parents[i]))
			break;
	clk = co->devm_clk_get(dev, mux);
	if (clk >= (u32)-4095) {
		isp_printf(1, "get %s failed!\n", mux);
	} else if (i == 3) {
		isp_printf(1, "The %s was not found!\n", parent);
	} else {
		u32 p = co->clk_get(0, t32_ivdc_clk_parents[i]);

		err = co->clk_set_parent(clk, p);
		co->clk_put(p);
	}
	r = co->devm_clk_put(t32_g_rd32(devp, 0), clk);
	if (err)
		return (u32)isp_printf(1, "Failed to modify the parent clock.\n");
	return r;
}

static inline int t32_ivdc_activate_module(struct t32_ivdc *iv, const struct t32_ivdc_clk_os *co, u8 *sd)
{
	u8 *priv = t32_ivdc_priv(sd);
	u32 flags = 0, clka, clkv, i;
	const char *name;

	if (!t32_ivdc_valid(priv))
		return -22;
	if (t32_g_rd32(priv, T32_IVDC_PRIV_STATE) != T32_IVDC_ST_SLAKED)
		return 0;
	iv->os->lock(priv + T32_IVDC_PRIV_LOCK, &flags);
	t32_g_wr32(priv, T32_IVDC_PRIV_STATE, T32_IVDC_ST_ACTIVE);
	iv->os->unlock(priv + T32_IVDC_PRIV_LOCK, flags);
	clka = co->isp_clka();
	clkv = co->isp_clkv();
	name = co->clka_name();
	t32_ivdc_set_clk_parent(co, sd + T32_IVDC_SD_DEV, name, "mux_ispa");
	name = co->clkv_name();
	t32_ivdc_set_clk_parent(co, sd + T32_IVDC_SD_DEV, name, "mux_vpu");
	{
		u8 *clks = T32_PTR(t32_g_rd32(sd, T32_IVDC_SD_CLKS));

		if (co->clk_get_rate(t32_g_rd32(clks, 0)) != clka)
			co->clk_set_rate(t32_g_rd32(clks, 0), clka);
		if (co->clk_get_rate(t32_g_rd32(clks, 4)) != clkv)
			co->clk_set_rate(t32_g_rd32(clks, 4), clkv);
		for (i = 0; i < t32_g_rd32(sd, T32_IVDC_SD_CLK_NUM); i++)
			co->clk_prepare_enable(t32_g_rd32(clks, 4 * i));
	}
	iv->d98b4 = 1;
	return 0;
}

#endif
