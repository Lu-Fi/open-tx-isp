/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 16: ispcore_activate_module (0xb9448), the core's power-up.
 * Only when channel 0 is idle (private +300 == 1): query the three ISP clock rates, reparent
 * "mux_isp" / "mux_isps" / "mux_ldc" to the configured parents (ispcore_set_clk_parent.isra.13 on
 * sd + 4), set each of the first three subdev clocks (+256) to its rate unless it already runs at
 * it, enable all subdev clocks (count +260), move every frame channel (count +1136, 284 B each at
 * +1132, word +128) from 1 to 2, send the tuning device event 0x4000000, run every sensor's pm op
 * +0 (activate; an error other than -ENOIOCTLCMD is logged with the sensor's word +8 and ends
 * the walk) and set all four channel states to 2.
 *
 * Vendor behaviour kept: a frame channel not in state 1 is logged and gives -1 with the earlier
 * channels already moved and the clocks left enabled; a sensor error still ends with state 2 and
 * result 0; a non-idle channel 0 gives 0 without touching anything.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispact_oracle).
 */
#ifndef TX_ISP_T32_ISPACT_H
#define TX_ISP_T32_ISPACT_H

#include "tx_isp_t32_isppm.h"

#define T32_ACT_NFCH		1136
#define T32_ACT_FCH		1132	/* 284 B frame channels */
#define T32_ACT_FCH_SIZE	284
#define T32_ACT_FCH_STATE	128
#define T32_ACT_TUNING		1704
#define T32_ACT_TUN_EVENT	164
#define T32_ACT_TUN_UP		0x04000000U
#define T32_PM_ACTIVATE		0

struct t32_act_ops {
	int (*call)(u32 fn, u32 a0, u32 a1, u32 a2);
	void (*log)(u32 level, u32 line, u32 a4);	/* isp_printf(level, fmt, func, line, a4) */
	u32 (*isp_rate)(u32 k);				/* get_isp_clk / get_isp_clks / get_isp_clkl */
	u32 (*parent_name)(u32 k);			/* get_clk_name / get_clks_name / get_clkl_name */
	void (*set_parent)(u32 devp, u32 parent, const char *mux);	/* ispcore_set_clk_parent.isra.13 */
	u32 (*get_rate)(u32 clk);
	void (*set_rate)(u32 clk, u32 rate);
	void (*enable)(u32 clk);			/* private_clk_prepare_enable */
};

static const char *const t32_act_mux[3] = { "mux_isp", "mux_isps", "mux_ldc" };	/* ccfcc, ccfd4, ccfe0 */

static inline int t32_ispcore_activate_module(const struct t32_act_ops *ops, u32 sd)
{
	u32 priv, clks, rate[3], i, s, fn, tun;
	int r;

	if (t32_pm_bad(sd))
		return T32_EINVAL;
	priv = t32_ic_r32(sd, T32_SD_PRIV);
	if (t32_pm_bad(priv))
		return T32_EINVAL;
	if (t32_ic_r32(priv, T32_PM_CH_STATE) != 1)
		return 0;
	clks = t32_ic_r32(sd, 256);
	for (i = 0; i < 3; i++)
		rate[i] = ops->isp_rate(i);
	for (i = 0; i < 3; i++)
		ops->set_parent(sd + 4, ops->parent_name(i), t32_act_mux[i]);
	for (i = 0; i < 3; i++)
		if (ops->get_rate(t32_ic_r32(clks, 4 * i)) != rate[i])
			ops->set_rate(t32_ic_r32(clks, 4 * i), rate[i]);
	for (i = 0; i < t32_ic_r32(sd, 260); i++)
		ops->enable(t32_ic_r32(clks, 4 * i));
	for (i = 0; i < t32_ic_r32(priv, T32_ACT_NFCH); i++) {
		u32 fc = t32_ic_r32(priv, T32_ACT_FCH) + i * T32_ACT_FCH_SIZE;

		if (t32_ic_r32(fc, T32_ACT_FCH_STATE) != 1) {
			ops->log(2, 3910, i);
			return -1;
		}
		t32_ic_w32(fc, T32_ACT_FCH_STATE, 2);
	}
	tun = t32_ic_r32(priv, T32_ACT_TUNING);
	ops->call(t32_ic_r32(tun, T32_ACT_TUN_EVENT), tun, T32_ACT_TUN_UP, 0);
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		s = t32_ic_r32(priv, T32_SD_SENSORS + 4 * i);
		if (t32_pm_bad(s) || !(fn = t32_pm_op(s, T32_PM_ACTIVATE)))
			continue;
		r = ops->call(fn, s, 0, 0);
		if (r && r != T32_ENOIOCTLCMD) {
			ops->log(2, 3929, t32_ic_r32(s, T32_SD_NAME_WORD));
			break;
		}
	}
	for (i = 0; i < 4; i++)
		t32_ic_w32(priv, T32_PM_CH_STATE + 4 * i, 2);
	return 0;
}

#endif /* TX_ISP_T32_ISPACT_H */
