/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 8: the subdev clock list.
 * Readable C of the vendor functions isp_subdev_init_clks (0x161cc) and
 * isp_subdev_release_clks (0x163d8).
 *
 * A subdev keeps its device at +4, the clock array at +256 and the clock count at +260.  init
 * gets every clock of the table { name, rate } (8 B per entry) with devm_clk_get and sets its
 * rate unless the rate is 0xffff; on an error it puts the clocks got so far (newest first),
 * frees the array and returns the error.  release puts all clocks (newest first) and frees the
 * array.
 *
 * Vendor behaviour kept: a set_rate error does not put the clock whose rate failed; on an error
 * the subdev's +256 keeps its old value.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispclk_oracle).
 */
#ifndef TX_ISP_T32_ISPCLK_H
#define TX_ISP_T32_ISPCLK_H

#include "tx_isp_t32_ispcore.h"

#define T32_SD_DEV		4
#define T32_SD_CLKS		256
#define T32_SD_NCLKS		260
#define T32_CLK_KEEP_RATE	0xffffu
#define T32_CLK_GFP		0x024000c0U

struct t32_clk_ops {
	void *(*kmalloc)(u32 size, u32 gfp);
	void (*kfree)(u32 p);
	u32 (*clk_get)(u32 dev, u32 name);		/* private_devm_clk_get */
	void (*clk_put)(u32 dev, u32 clk);		/* private_devm_clk_put */
	int (*clk_set_rate)(u32 clk, u32 rate);		/* private_clk_set_rate */
	void (*log)(u32 line, u32 a4, u32 a5, u32 a6);	/* isp_printf(2, fmt, func, line, ...) */
};

/* put clks[n - 1] .. clks[0] and free the array */
static inline void t32_clk_unwind(const struct t32_clk_ops *ops, u32 dev, u32 clks, u32 n)
{
	while (n)
		ops->clk_put(dev, t32_ic_r32(clks, 4 * --n));
	ops->kfree(clks);
}

/* isp_subdev_init_clks(sd, table) */
static inline int t32_isp_subdev_init_clks(const struct t32_clk_ops *ops, u32 sd, u32 tab)
{
	u32 n = t32_ic_r32(sd, T32_SD_NCLKS), clks, i, c, name, rate;
	int r;

	if (!n) {
		t32_ic_w32(sd, T32_SD_CLKS, 0);
		return 0;
	}
	clks = T32_IC_A(ops->kmalloc(n << 2, T32_CLK_GFP));
	if (!clks) {
		ops->log(2933, 0, 0, 0);
		return -12;
	}
	memset(T32_IC_P(clks), 0, n << 2);
	for (i = 0; i < n; i++) {
		name = t32_ic_r32(tab, 8 * i);
		rate = t32_ic_r32(tab, 8 * i + 4);
		c = ops->clk_get(t32_ic_r32(sd, T32_SD_DEV), name);
		t32_ic_w32(clks, 4 * i, c);
		if (c >= (u32)-4095) {
			ops->log(2941, 0, name, c);
			t32_clk_unwind(ops, t32_ic_r32(sd, T32_SD_DEV), clks, i);
			return (int)c;
		}
		if (rate == T32_CLK_KEEP_RATE)
			continue;
		r = ops->clk_set_rate(c, rate);
		if (r) {
			ops->log(2948, name, rate, 0);
			t32_clk_unwind(ops, t32_ic_r32(sd, T32_SD_DEV), clks, i);
			return r;
		}
	}
	t32_ic_w32(sd, T32_SD_CLKS, clks);
	return 0;
}

/* isp_subdev_release_clks(sd) */
static inline int t32_isp_subdev_release_clks(const struct t32_clk_ops *ops, u32 sd)
{
	u32 clks = t32_ic_r32(sd, T32_SD_CLKS), n = t32_ic_r32(sd, T32_SD_NCLKS);

	if (!clks)
		return 0;
	t32_clk_unwind(ops, t32_ic_r32(sd, T32_SD_DEV), clks, (int)n > 0 ? n : 0);	/* signed count */
	t32_ic_w32(sd, T32_SD_CLKS, 0);
	return 0;
}

#endif /* TX_ISP_T32_ISPCLK_H */
