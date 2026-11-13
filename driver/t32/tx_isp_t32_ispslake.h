/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 14: ispcore_slake_module (0xbab80), the core's power-down.
 * For every channel not yet idle (core private +300 word != 1): log, deinit the core when the
 * channel was running (>= 3: ispcore_core_ops_init(sd, &0)), mark all frame channels (count
 * +1136, 284 B each at +1132, word +128) as 1, send the tuning device event 0x4000001 (tuning
 * block +1704, its event callback +164) and set the channel idle.  Then stop the two firmware
 * threads (+1688, +1696; a failed stop is logged and keeps the slot), disable tuning, run every
 * sensor's pm op +4 (an error other than -ENOIOCTLCMD is logged and ends the walk), query the
 * three ISP clocks and disable / unprepare the subdev clocks (+256, count +260, newest first).
 *
 * Vendor behaviour kept: the first log's fifth argument is an unset stack word (0 here); the
 * clock walk takes the count as signed.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispslake_oracle).
 */
#ifndef TX_ISP_T32_ISPSLAKE_H
#define TX_ISP_T32_ISPSLAKE_H

#include "tx_isp_t32_isppm.h"

#define T32_PRIV_NFCH		1136
#define T32_PRIV_FCH		1132	/* 284 B frame channels */
#define T32_FCH_SIZE		284
#define T32_FCH_STATE		128
#define T32_PRIV_TUNING		1704
#define T32_TUN_EVENT		164
#define T32_PRIV_THREADS	1688	/* 2 kthreads */
#define T32_PM_SLAKE		4

struct t32_slake_ops {
	int (*call)(u32 fn, u32 a0, u32 a1, u32 a2);
	void (*log)(u32 level, u32 line, u32 a4);
	void (*core_ops_init)(u32 sd, u32 *on);	/* ispcore_core_ops_init(sd, &0) */
	int (*kthread_stop)(u32 t);
	void (*disable_tuning)(void);
	void (*get_clks)(void);			/* get_isp_clk, get_isp_clks, get_isp_clkl */
	void (*clk_off)(u32 clk);		/* private_clk_disable_unprepare */
};

static inline int t32_ispcore_slake_module(const struct t32_slake_ops *ops, u32 sd)
{
	u32 priv = t32_pm_priv(sd), c, i, s, fn, clks, zero = 0;
	int n, r;

	if (!priv)
		return T32_EINVAL;
	for (c = 0; c < 4; c++) {
		u32 st = t32_ic_r32(priv, T32_PM_CH_STATE + 4 * c), tun;

		if (st == 1)
			continue;
		ops->log(0, 3961, 0);
		if ((int)st >= 3)
			ops->core_ops_init(sd, &zero);
		for (i = 0; i < t32_ic_r32(priv, T32_PRIV_NFCH); i++)
			t32_ic_w32(t32_ic_r32(priv, T32_PRIV_FCH) + i * T32_FCH_SIZE, T32_FCH_STATE, 1);
		tun = t32_ic_r32(priv, T32_PRIV_TUNING);
		ops->call(t32_ic_r32(tun, T32_TUN_EVENT), tun, 0x04000001, 0);
		t32_ic_w32(priv, T32_PM_CH_STATE + 4 * c, 1);
	}
	for (i = 0; i < 2; i++) {
		u32 t = t32_ic_r32(priv, T32_PRIV_THREADS + 4 * i);

		if (!t)
			continue;
		if (!ops->kthread_stop(t))
			t32_ic_w32(priv, T32_PRIV_THREADS + 4 * i, 0);
		else
			ops->log(2, 3979, 0);
	}
	ops->disable_tuning();
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		s = t32_ic_r32(priv, T32_SD_SENSORS + 4 * i);
		if (t32_pm_bad(s) || !(fn = t32_pm_op(s, T32_PM_SLAKE)))
			continue;
		r = ops->call(fn, s, 0, 0);
		if (r && r != T32_ENOIOCTLCMD) {
			ops->log(2, 3995, t32_ic_r32(s, T32_SD_NAME_WORD));
			break;
		}
	}
	clks = t32_ic_r32(sd, 256);
	ops->get_clks();
	for (n = (int)(t32_ic_r32(sd, 260) - 1); n >= 0; n--)
		ops->clk_off(t32_ic_r32(clks, 4 * n));
	return 0;
}

#endif /* TX_ISP_T32_ISPSLAKE_H */
