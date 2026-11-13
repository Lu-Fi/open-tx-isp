/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 15: ispcore_video_s_stream (0xb9880).
 * (sd, req = { enable, channel }).  The channel must be at least prepared (core private +300
 * word >= 3, checked under the lock +288).  enable: interrupts on (when the core device of
 * t32_data_f3310 has a private block), and when the last channel (+1196 - 1) of a prepared set
 * turns on, all channels become streaming (4) and the ISP stream starts once (+4012; with
 * t32_data_d9008 only from channel 0); the per-pipeline table pointer t32_data_f3314 is chosen
 * by the channel count (3, 4, 2, else).  disable: when the channel streams, every streaming
 * frame channel (12 x 284 B at +1132, +128 state, +132 channel) is stopped and the channel
 * falls back to 3.  Then every sensor's video s_stream (ops +4, entry +0) runs with req; for
 * disable the interrupts go off afterwards.  Result: the last sensor result, -ENOIOCTLCMD as 0.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispss_oracle).
 */
#ifndef TX_ISP_T32_ISPSS_H
#define TX_ISP_T32_ISPSS_H

#include "tx_isp_t32_ispslake.h"

#define T32_PRIV_NCH		1196
#define T32_PRIV_LOCK		288
#define T32_FCH_CHAN		132
#define T32_NFCH_FIXED		12
#define T32_OPS_VIDEO		4

struct t32_ss_ops {
	int (*call)(u32 fn, u32 a0, u32 a1, u32 a2);
	u32 (*lock)(u32 l);
	void (*unlock)(u32 l, u32 flags);
	void (*log)(u32 level, u32 line);
	void (*intp_enable)(void);
	void (*intp_disable)(void);
	void (*enable_irq)(u32 sd, u32 ch);	/* tx_isp_enable_irq */
	void (*disable_irq)(u32 sd, u32 ch);	/* tx_isp_disable_irq */
	void (*stream_on)(void);		/* lib_tisp_stream_on */
	void (*fch_streamoff)(u32 chan);	/* ispcore_frame_channel_streamoff */
};

struct t32_ss_glob {
	u32 *core;		/* t32_data_f3310 */
	const u32 *one_start;	/* t32_data_d9008: start only from channel 0 */
	u32 *f3354;		/* cleared before the stream start */
	u32 *pipe_tab;		/* t32_data_f3314 */
	u32 tab3, tab4, tab2, tab_other;	/* d8e78, d8e6c, d8e84, f3360 */
};

static inline int t32_ispcore_video_s_stream(const struct t32_ss_ops *ops, const struct t32_ss_glob *g, u32 sd, u32 req)
{
	u32 priv = t32_ic_r32(sd, T32_SD_PRIV), fl, ch = t32_ic_r32(req, 4), en, n, i, s, vops, fn;
	int r = 0, st;

	fl = ops->lock(priv + T32_PRIV_LOCK);
	if ((int)t32_ic_r32(priv, T32_PM_CH_STATE + 4 * ch) < 3) {
		ops->log(2, 2945);
		ops->unlock(priv + T32_PRIV_LOCK, fl);
		return -1;
	}
	ops->unlock(priv + T32_PRIV_LOCK, fl);
	en = t32_ic_r32(req, 0);
	if (en) {
		if (t32_ic_r32(*g->core, T32_SD_PRIV)) {
			ops->intp_enable();
			ops->enable_irq(sd, ch);
		}
		ch = t32_ic_r32(req, 4);
		n = t32_ic_r32(priv, T32_PRIV_NCH);
		if (t32_ic_r32(priv, T32_PM_CH_STATE + 4 * ch) == 3 && ch == n - 1) {
			for (st = 0; st < (int)n; st++)
				t32_ic_w32(priv, T32_PM_CH_STATE + 4 * st, 4);
			if ((!*g->one_start || !ch) && !t32_ic_r32(priv, T32_PM_SLEEP)) {
				*g->f3354 = 0;
				ops->stream_on();
				t32_ic_w32(priv, T32_PM_SLEEP, 1);
			}
		}
		n = t32_ic_r32(priv, T32_PRIV_NCH);
		*g->pipe_tab = n == 3 ? g->tab3 : n == 4 ? g->tab4 : n == 2 ? g->tab2 : g->tab_other;
	} else if (t32_ic_r32(priv, T32_PM_CH_STATE + 4 * ch) == 4) {
		for (i = 0; i < T32_NFCH_FIXED; i++) {
			u32 f = t32_ic_r32(priv, T32_PRIV_FCH) + i * T32_FCH_SIZE;

			if (t32_ic_r32(f, T32_FCH_STATE) == 4)
				ops->fch_streamoff(t32_ic_r32(f, T32_FCH_CHAN));
		}
		t32_ic_w32(priv, T32_PM_CH_STATE + 4 * t32_ic_r32(req, 4), 3);
		t32_ic_w32(priv, T32_PM_SLEEP, 0);
	}
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		s = t32_ic_r32(sd, T32_SD_SENSORS + 4 * i);
		if (!s)
			continue;
		vops = t32_ic_r32(t32_ic_r32(s, T32_SD_OPS), T32_OPS_VIDEO);
		fn = vops ? t32_ic_r32(vops, 0) : 0;
		if (!fn) {
			r = T32_ENOIOCTLCMD;
			continue;
		}
		r = ops->call(fn, s, req, 0);
		if (r && r != T32_ENOIOCTLCMD)
			break;
	}
	if (!t32_ic_r32(req, 0) && t32_ic_r32(*g->core, T32_SD_PRIV)) {
		ops->intp_disable();
		ops->disable_irq(sd, t32_ic_r32(req, 4));
	}
	return r == T32_ENOIOCTLCMD ? 0 : r;
}

#endif /* TX_ISP_T32_ISPSS_H */
