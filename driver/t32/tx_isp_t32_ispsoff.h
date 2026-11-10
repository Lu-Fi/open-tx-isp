/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 13: ispcore_frame_channel_streamoff (0xb5cd4).
 * chan: +0 core device, +7 u8 state (4 = streaming), +32 frame channel fc.  fc: +116 MSCA
 * output index (pipe = index / 3, output = index % 3), +120 u8 work id, +128 state, +168 lock,
 * +208 counter.  Under fc's lock (only when fc streams): MSCA output stream off, the register
 * stream off of the output when the pipe is the current one (u8 t32_data_f334c); then, unless
 * t32_data_d9004 is set, wait up to 502 x 1 ms for tisp_msca_work_state(pipe, work id) == 0;
 * then both states become 3, the output's address fifo is cleared, fc's first 116 B are
 * cleared and the counters of the core (+1180..+1188) and of its mode block (+376: +152, +216)
 * are reset.
 *
 * Vendor behaviour kept: the core private block is dereferenced also when the core pointer is
 * NULL / an error (private block 0: crash).
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispsoff_oracle).
 */
#ifndef TX_ISP_T32_ISPSOFF_H
#define TX_ISP_T32_ISPSOFF_H

#include "tx_isp_t32_isppm.h"

#define T32_CHAN_STATE		7	/* u8 */
#define T32_CHAN_FC		32
#define T32_FC_INDEX		116
#define T32_FC_WORK		120	/* u8 */
#define T32_FC_STATE		128
#define T32_FC_LOCK		168
#define T32_FC_CNT		208
#define T32_FC_STREAMING	4
#define T32_FC_STOPPED		3
#define T32_SOFF_WAIT		502

struct t32_soff_ops {
	u32 (*lock)(u32 l);
	void (*unlock)(u32 l, u32 flags);
	void (*chx_stream_off)(u32 idx);		/* lib_tisp_msca_chx_stream_off */
	void (*chx_reg_stream_off)(u32 out);		/* lib_tisp_msca_chx_reg_stream_off */
	int (*work_state)(u32 pipe, u32 work);		/* tisp_msca_work_state */
	void (*msleep)(u32 ms);
	void (*log)(u32 msg, u32 idx);			/* isp_printf(1, 0 timeout / 1 done, idx) */
	void (*fifo_clear)(u32 idx);			/* tisp_msca_addr_fifo_clear_byindex */
};

struct t32_soff_glob {
	const u8 *cur_pipe;	/* t32_data_f334c */
	const u32 *no_wait;	/* t32_data_d9004 */
};

static inline int t32_ispcore_frame_channel_streamoff(const struct t32_soff_ops *ops, const struct t32_soff_glob *g,
						      u32 chan)
{
	u32 core = t32_ic_r32(chan, 0), priv = 0, fc, mp, fl, n;
	int idx;

	if (core && core < (u32)-4095)
		priv = t32_ic_r32(core, T32_SD_PRIV);
	fc = t32_ic_r32(chan, T32_CHAN_FC);
	mp = t32_ic_r32(priv, 376);	/* the mode block of channel 0 */
	if (*(volatile u8 *)T32_IC_P(chan + T32_CHAN_STATE) != T32_FC_STREAMING)
		return 0;
	fl = ops->lock(fc + T32_FC_LOCK);
	if (t32_ic_r32(fc, T32_FC_STATE) != T32_FC_STREAMING) {
		ops->unlock(fc + T32_FC_LOCK, fl);
		return 0;
	}
	ops->chx_stream_off(*(volatile u8 *)T32_IC_P(fc + T32_FC_INDEX));
	idx = (int)t32_ic_r32(fc, T32_FC_INDEX);
	t32_ic_w32(fc, T32_FC_CNT, 0);
	if (*g->cur_pipe == (u32)(idx / 3))
		ops->chx_reg_stream_off((idx % 3) & 0xff);
	ops->unlock(fc + T32_FC_LOCK, fl);
	if (!*g->no_wait) {
		for (n = T32_SOFF_WAIT; n; n--) {
			idx = (int)t32_ic_r32(fc, T32_FC_INDEX);
			if (!ops->work_state((idx / 3) & 0xff, *(volatile u8 *)T32_IC_P(fc + T32_FC_WORK)))
				break;
			ops->msleep(1);
		}
		if (!n)
			ops->log(0, t32_ic_r32(fc, T32_FC_INDEX));
	}
	ops->log(1, t32_ic_r32(fc, T32_FC_INDEX));
	t32_ic_w32(fc, T32_FC_STATE, T32_FC_STOPPED);
	*(volatile u8 *)T32_IC_P(chan + T32_CHAN_STATE) = T32_FC_STOPPED;
	ops->fifo_clear(*(volatile u8 *)T32_IC_P(fc + T32_FC_INDEX));
	memset(T32_IC_P(fc), 0, T32_FC_INDEX);
	t32_ic_w32(mp, 152, 0);
	t32_ic_w32(mp, 216, 0);
	t32_ic_w32(priv, 1180, 0);
	t32_ic_w32(priv, 1184, 0);
	t32_ic_w32(priv, 1188, 0);
	return 0;
}

#endif /* TX_ISP_T32_ISPSOFF_H */
