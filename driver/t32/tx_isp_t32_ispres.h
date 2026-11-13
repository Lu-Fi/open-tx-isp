/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 18: ispcore_resume_module (0xb6534), the core's resume.
 * Only when all four channels stream (state >= 2): log, reset the AOV bookkeeping (+4024 /
 * +4032 = 0, list head +4036 empty), clear the MSCA address FIFOs unless AOV (+4020) runs, wake
 * every pipe (count +1196) and, with two or more pipes, refill the FIFO of each of the first
 * three frame channels of pipe 0 with its first queued buffer (list +220) or else with the first
 * dropped one (list +212, moved back to the queue); a channel with neither is logged.  Then for
 * every pipe: restore its autozoom (60 B attribute at +4044, when one of its three enable words
 * is 1) and restart every frame channel that has a buffer (+208); start the stream once
 * (+4012, bss f3354 = 0) and resume every sensor (pm op +20; an error other than -ENOIOCTLCMD
 * is logged with the sensor's word +8 and gives -EINVAL).
 *
 * Vendor behaviour kept: an invalid core is logged and gives -EINVAL, a channel that does not
 * stream gives 0 without resuming; the first two logs pass an unset stack word (0 here); the
 * restart index is (3 * pipe + channel) & 0xff while the frame channel is 852 * pipe + 284 *
 * channel from +1132.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispres_oracle).
 */
#ifndef TX_ISP_T32_ISPRES_H
#define TX_ISP_T32_ISPRES_H

#include "tx_isp_t32_ispfd.h"

#define T32_RES_NPIPES		1196
#define T32_RES_AOV_A		4024
#define T32_RES_AOV_B		4032
#define T32_RES_AOV_LIST	4036
#define T32_RES_ZOOM		4044	/* 60 B per pipe */
#define T32_RES_ZOOM_SIZE	60
#define T32_FC_HASBUF		208
#define T32_PM_RESUME		20

struct t32_res_ops {
	int (*call)(u32 fn, u32 a0, u32 a1, u32 a2);
	void (*log)(u32 level, u32 line, u32 a4);	/* isp_printf(level, fmt, func, line, a4) */
	void (*fifo_clear_all)(void);			/* tisp_msca_addr_fifo_clear_all */
	void (*aov_wake_up)(u32 pipe);			/* lib_tisp_aov_wake_up */
	void (*chx_en)(u32 pipe, u32 ch, u32 on);	/* tisp_msca_chx_en */
	void (*fifo_write)(u32 pipe, u32 ch, u32 a, u32 b);	/* tisp_msca_addr_fifo_write */
	void (*autozoom)(u32 pipe, u32 attr);		/* tisp_s_autozoom_control */
	void (*channel_start)(u32 n);			/* tisp_msca_channel_start */
	void (*stream_on)(void);			/* lib_tisp_stream_on */
	u32 *stream_flag;				/* bss f3354 */
};

static inline void t32_res_refill(const struct t32_res_ops *ops, u32 priv)
{
	u32 k;

	for (k = 0; k < 3; k++) {
		u32 fc = t32_ic_r32(priv, T32_FD_FCH) + k * T32_FD_FCH_SIZE;
		u32 d = t32_ic_r32(fc, T32_FC_DROPQ), q = t32_ic_r32(fc, T32_FC_QUEUE);

		if (d == fc + T32_FC_DROPQ)
			d = 0;
		if (q != fc + T32_FC_QUEUE && q) {
			ops->chx_en(0, k, 1);
			ops->fifo_write(0, k, t32_ic_r32(q, 8), t32_ic_r32(q, 12));
		} else if (d) {
			ops->chx_en(0, k, 1);
			ops->fifo_write(0, k, t32_ic_r32(d, 8), t32_ic_r32(d, 12));
			t32_fd_move_tail(d, t32_ic_r32(priv, T32_FD_FCH) + k * T32_FD_FCH_SIZE + T32_FC_QUEUE);
		} else {
			ops->log(1, 4104, k);
		}
	}
}

static inline int t32_ispcore_resume_module(const struct t32_res_ops *ops, u32 sd)
{
	u32 priv = t32_pm_priv(sd), p, k, z, s, fn;
	int r;

	if (!priv) {
		ops->log(2, 4073, 0);
		return T32_EINVAL;
	}
	for (k = 0; k < 4; k++)
		if ((int)t32_ic_r32(priv, T32_PM_CH_STATE + 4 * k) < 2)
			return 0;
	ops->log(0, 4082, 0);
	t32_ic_w32(priv, T32_RES_AOV_A, 0);
	t32_ic_w32(priv, T32_RES_AOV_LIST, priv + T32_RES_AOV_LIST);
	t32_ic_w32(priv, T32_RES_AOV_LIST + 4, priv + T32_RES_AOV_LIST);
	t32_ic_w32(priv, T32_RES_AOV_B, 0);
	if (!t32_ic_r32(priv, T32_PM_AOV))
		ops->fifo_clear_all();
	for (p = 0; p < t32_ic_r32(priv, T32_RES_NPIPES); p++) {
		ops->aov_wake_up(p & 0xff);
		if (t32_ic_r32(priv, T32_RES_NPIPES) >= 2 && p == 0)
			t32_res_refill(ops, priv);
	}
	for (p = 0; p < t32_ic_r32(priv, T32_RES_NPIPES); p++) {
		z = priv + T32_RES_ZOOM + p * T32_RES_ZOOM_SIZE;
		if (t32_ic_r32(z, 0) == 1 || t32_ic_r32(z, 4) == 1 || t32_ic_r32(z, 8) == 1)
			ops->autozoom(p, z);
		for (k = 0; k < 3; k++)
			if (t32_ic_r32(t32_ic_r32(priv, T32_FD_FCH) + (3 * p + k) * T32_FD_FCH_SIZE, T32_FC_HASBUF))
				ops->channel_start((k + 3 * p) & 0xff);
	}
	if (!t32_ic_r32(priv, T32_PM_SLEEP)) {
		*ops->stream_flag = 0;
		ops->stream_on();
		t32_ic_w32(priv, T32_PM_SLEEP, 1);
	}
	for (k = 0; k < T32_SD_SENSORS_N; k++) {
		s = t32_ic_r32(priv, T32_SD_SENSORS + 4 * k);
		if (t32_pm_bad(s) || !(fn = t32_pm_op(s, T32_PM_RESUME)))
			continue;
		r = ops->call(fn, s, 0, 0);
		if (r && r != T32_ENOIOCTLCMD) {
			ops->log(2, 4143, t32_ic_r32(s, T32_SD_NAME_WORD));
			return T32_EINVAL;
		}
	}
	return 0;
}

#endif /* TX_ISP_T32_ISPRES_H */
