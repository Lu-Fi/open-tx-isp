/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 7: the core's power-management callbacks.
 * Readable C of the vendor functions ispcore_suspend_module (0xb595c),
 * ispcore_resume_early_module (0xb584c) and ispcore_complete_module (0xb572c).
 *
 * The core device (sd) keeps its private block at +280; that block holds 4 channel state words
 * at +300 (a channel is streaming when its word is >= 2), 16 sensor subdev slots at +84, the AOV
 * flag at +4020 and the sleep flag at +4012.  A sensor's ops table (+264) keeps its pm ops at +16:
 * +12 suspend, +16 resume_early, +24 complete, each called with the sensor subdev.
 * suspend: only when all 4 channels stream: AOV sleep (unless +4020), clear +4012, clear the
 * interrupts, suspend every sensor (an error other than -ENOIOCTLCMD stops with -EINVAL).
 * resume_early: resume every sensor (an error is logged with the sensor's word +8, result 0).
 * complete: only when all 4 channels stream; runs every sensor's complete.
 *
 * Vendor behaviour kept: complete returns the value of the isp_printf it tail-calls (void here);
 * suspend returns 0 without suspending when a channel does not stream.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_isppm_oracle).
 */
#ifndef TX_ISP_T32_ISPPM_H
#define TX_ISP_T32_ISPPM_H

#include "tx_isp_t32_ispcore.h"

#define T32_PM_CH_STATE		300	/* 4 words */
#define T32_PM_AOV		4020
#define T32_PM_SLEEP		4012
#define T32_OPS_PM		16
#define T32_PM_SUSPEND		12
#define T32_PM_RESUME_EARLY	16
#define T32_PM_COMPLETE		24
#define T32_SD_NAME_WORD	8
#ifndef T32_EINVAL
#define T32_EINVAL		(-22)
#endif

struct t32_pm_ops {
	int (*call)(u32 fn, u32 a0, u32 a1, u32 a2);
	void (*log)(u32 level, u32 line, u32 a4);	/* isp_printf(level, fmt, func, line, a4) */
	void (*aov_sleep)(u32 arg);			/* lib_tisp_aov_sleep(0) */
	void (*intp_clear)(void);			/* lib_tisp_intp_clear */
};

static inline int t32_pm_bad(u32 p)
{
	return !p || p >= (u32)-4095;	/* NULL or IS_ERR */
}

/* the private block of a valid core device, 0 when sd or it is NULL / an error pointer */
static inline u32 t32_pm_priv(u32 sd)
{
	u32 priv;

	if (t32_pm_bad(sd))
		return 0;
	priv = t32_ic_r32(sd, T32_SD_PRIV);
	return t32_pm_bad(priv) ? 0 : priv;
}

static inline int t32_pm_all_streaming(u32 priv)
{
	unsigned int c;

	for (c = 0; c < 4; c++)
		if ((int)t32_ic_r32(priv, T32_PM_CH_STATE + 4 * c) < 2)
			return 0;
	return 1;
}

/* the pm op at entry of sensor s, 0 when missing */
static inline u32 t32_pm_op(u32 s, u32 entry)
{
	u32 pm = t32_ic_r32(t32_ic_r32(s, T32_SD_OPS), T32_OPS_PM);

	return pm ? t32_ic_r32(pm, entry) : 0;
}

/* ispcore_suspend_module(sd) */
static inline int t32_ispcore_suspend_module(const struct t32_pm_ops *ops, u32 sd)
{
	u32 priv = t32_pm_priv(sd), s, fn;
	unsigned int i;
	int r;

	if (!priv) {
		ops->log(2, 4025, 0);
		return T32_EINVAL;
	}
	if (!t32_pm_all_streaming(priv))
		return 0;
	ops->log(0, 4035, 0);
	if (!t32_ic_r32(priv, T32_PM_AOV))
		ops->aov_sleep(0);
	t32_ic_w32(priv, T32_PM_SLEEP, 0);
	ops->intp_clear();
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		s = t32_ic_r32(priv, T32_SD_SENSORS + 4 * i);
		if (t32_pm_bad(s) || !(fn = t32_pm_op(s, T32_PM_SUSPEND)))
			continue;
		r = ops->call(fn, s, 0, 0);
		if (r && r != T32_ENOIOCTLCMD) {
			ops->log(2, 4052, t32_ic_r32(s, T32_SD_NAME_WORD));
			return T32_EINVAL;
		}
	}
	return 0;
}

/* ispcore_resume_early_module(sd) */
static inline int t32_ispcore_resume_early_module(const struct t32_pm_ops *ops, u32 sd)
{
	u32 priv = t32_pm_priv(sd), s, fn;
	unsigned int i;
	int r;

	if (!priv) {
		ops->log(2, 4162, 0);
		return T32_EINVAL;
	}
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		s = t32_ic_r32(priv, T32_SD_SENSORS + 4 * i);
		if (t32_pm_bad(s) || !(fn = t32_pm_op(s, T32_PM_RESUME_EARLY)))
			continue;
		r = ops->call(fn, s, 0, 0);
		if (r && r != T32_ENOIOCTLCMD) {
			ops->log(2, 4175, t32_ic_r32(s, T32_SD_NAME_WORD));
			return 0;
		}
	}
	return 0;
}

/* ispcore_complete_module(sd) */
static inline void t32_ispcore_complete_module(const struct t32_pm_ops *ops, u32 sd)
{
	u32 priv = t32_pm_priv(sd), s, fn;
	unsigned int i;

	if (!priv) {
		ops->log(2, 4259, 0);
		return;
	}
	ops->log(0, 4262, 0);
	if (!t32_pm_all_streaming(priv)) {
		ops->log(2, 4266, 0);
		return;
	}
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		s = t32_ic_r32(priv, T32_SD_SENSORS + 4 * i);
		if (t32_pm_bad(s) || !(fn = t32_pm_op(s, T32_PM_COMPLETE)))
			continue;
		ops->call(fn, s, 0, 0);
	}
}

#endif /* TX_ISP_T32_ISPPM_H */
