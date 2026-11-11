/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 22: ispcore_interrupt_service_routine (0xb7934), the ISP core interrupt
 * of subdev sd (priv = sd + 280 = core private block, regs = sd + 244).  Steps:
 *  1. lib_tisp_intp_handler fills the status block (word 0 = frame status, 1 = frame-done channel
 *     bits, 2 = error bits, 5 = stream-off request in bit 16).
 *  2. Error bits (0x8000 alone is not printed, line 1122 with register 0x40100): per-pipe counters
 *     at priv + 896 / 912 (bit 5) / 928 (bit 4); outside sensor mode 1 the core and the VIC are
 *     reset (f335f = 1) and 1 is returned.
 *  3. Stream-off request: every channel of the current pipe (bss f334c) whose state +208 is 0 is
 *     stopped by register (pipe index +116 mod 3); after a reset (f335f) the pipe sequence starts
 *     again (f3344 = 1, f3354 = 0, pipe 0), a waiter on f331c (f335e) is woken, the ISP is
 *     triggered (register 0x80010 = 1) and 1 is returned; otherwise f3350 counts the request.
 *  4. lib_tisp_intp_process gives the return value; frame status bits 20 / 24 take the three
 *     time stamps (priv + 952 / 1016 / 984, state byte priv + 945: 1 -> 2 -> 3 -> 4);
 *     every frame-done channel bit runs ispcore_channel_framedone_process; with the frame wait
 *     (priv + 744 == 1) and a negative wait word at index priv + 756 of the table priv + 1048 the
 *     current pipe's word is cleared and its completion (priv + 1064 + 12 * pipe) is signalled once
 *     the low 16 bits reach the limit priv + 752, else the pipe's word counts up.
 *  5. A pipe is done in sensor mode 1 with the frame-done bit, otherwise when the frame-done bit
 *     and all three channels (disabled, or frame-done) were seen (bss f3358[0..3]).  Then: f3354
 *     counts the pipe, the sensor gets event 0x4000002 (and 0x4000003 once when priv + 1164 +
 *     4 * pipe is 1), the pipe's work (d8cd0 / d8cc0 / d8cb0 / d8ca0) is queued and the next pipe
 *     is taken from the sequence table f3314[f3354 % 12].
 *  6. Multi-pipe mode: the reconfiguration interrupt runs; a waiter on f331c (f335d) is woken
 *     and the function returns.  Stop request (priv + 4020 == 1, pipe 0 or mode 1): VIC stop /
 *     reset, interrupts off and the stop event (type 1, sd) is pushed (-22 when the
 *     queue is full).  Mode 1 checks the frame drop of the three channels of pipe 0; otherwise
 *     each channel of the new pipe is armed with its next queued buffer, or the first buffer of
 *     the drop list of channel +116 (moved to the queue), or disabled; then the LDC registers and
 *     the reconfiguration of the pipe are triggered, or (priv + 4284 == 1 at pipe 0) all channels
 *     are stopped.  priv + 4284 == 1 in mode 1 stops channels 0..2 at the end.
 *
 * Vendor behaviour kept: the pipe index is a byte but the counter / wait tables are not bounded;
 * the sensor event argument is the word after the status block (stack, zero unless the handler
 * wrote it) followed by the pipe; the wait word is read at index priv + 756 but cleared at the
 * current pipe; the stop loop at priv + 4284 runs 3 * mode times (pipe = index / 3).
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispisr_oracle).
 */
#ifndef TX_ISP_T32_ISPISR_H
#define TX_ISP_T32_ISPISR_H

#include "tx_isp_t32_ispfd.h"

#define T32_IS_REGS		244
#define T32_IS_PRIV		280
#define T32_IS_ERR_CNT		896
#define T32_IS_ERR_CNT20	912
#define T32_IS_ERR_CNT10	928
#define T32_IS_TS_STATE		945	/* u8 */
#define T32_IS_TS0		952
#define T32_IS_TS1		984
#define T32_IS_TS2		1016
#define T32_IS_WAIT_ON		744
#define T32_IS_WAIT_LIMIT	752
#define T32_IS_WAIT_IDX		756
#define T32_IS_WAIT		1048	/* per-pipe wait words */
#define T32_IS_WAIT_DONE	1064	/* per-pipe completions, 12 B */
#define T32_IS_EV3		1164	/* per-pipe "send event 0x4000003" flags */
#define T32_IS_SENSOR		1704
#define T32_IS_STOP		4020
#define T32_IS_STREAMOFF	4284
#define T32_FC_STATE		208
#define T32_IS_REG_ERR		0x40100
#define T32_IS_REG_TRIG		0x80010
#define T32_IS_REG_PIPE1	0x81010
#define T32_IS_SENSOR_CB	164
#define T32_IS_EV_FRAME		0x04000002U
#define T32_IS_EV_FRAME2	0x04000003U

struct t32_is_glob {
	u8 *cur;		/* f334c current pipe */
	u8 *reset;		/* f335f */
	u8 *rst_wait;		/* f335e */
	u8 *seq_start;		/* f3344 */
	u32 *npipe;		/* f3354 */
	u32 *nstop;		/* f3350 */
	u8 *seen;		/* f3358[4] */
	u8 *done;		/* f335c */
	u8 *sw_wait;		/* f335d */
	u32 *seq;		/* f3314: pointer to the 12 B pipe sequence */
	u32 *drop_mode;		/* d9014 */
	u32 *wq;		/* system_wq */
	u32 wait;		/* &f331c */
	u32 work[4];		/* pipe works d8cd0, d8cc0, d8cb0, d8ca0 */
};

struct t32_is_ops {
	void (*handler)(u32 *st);				/* lib_tisp_intp_handler */
	u32 (*process)(u32 pipe);				/* lib_tisp_intp_process */
	void (*print_err)(u32 err, u32 reg);			/* isp_printf(1, fmt, func, 1122, err, reg) */
	void (*print_line)(u32 line);				/* isp_printf(2, fmt, func, line) */
	void (*core_reset)(u32 priv);				/* lib_tisp_core_reset */
	void (*vic_reset)(void);
	void (*vic_stop)(void);
	void (*intp_disable)(void);
	void (*func_stop_reset)(void);
	void (*info_clear)(void);				/* ivdc_isp_info_clear */
	void (*trig)(void);					/* lib_tisp_trig */
	void (*rcfg_switch)(void);				/* lib_tisp_rcfg_switch_interrupt */
	void (*reg_stream_off)(u32 n);				/* lib_tisp_msca_chx_reg_stream_off */
	void (*stream_off)(u32 n);				/* lib_tisp_msca_chx_stream_off */
	u32 (*get_chx_en)(u32 pipe, u32 ch);			/* lib_tisp_get_msca_chx_en */
	void (*chx_en)(u32 pipe, u32 ch, u32 en);		/* tisp_msca_chx_en */
	void (*fifo_clear)(u32 ch);				/* tisp_msca_addr_fifo_clear_reg_byindex */
	void (*fifo_write)(u32 pipe, u32 ch, u32 a, u32 b);	/* tisp_msca_addr_fifo_write */
	void (*ldc_reflash)(u32 pipe);				/* tisp_ldc_reg_reflash */
	void (*rcfg_trig)(u32 pipe);				/* tisp_rcfg_cfg_trig */
	void (*gettimeofday)(u32 tv);
	void (*complete)(u32 c);
	void (*framedone)(u32 priv, u32 pipe, u32 ch);		/* ispcore_channel_framedone_process */
	u32 (*get_frame_drop)(u32 fc);				/* __ispcore_get_frame_drop */
	void (*check_frame_drop)(u32 fc);			/* __ispcore_single_cam_check_frame_drop */
	int (*event_push)(u32 ch, u32 *ev);			/* isp_core_event_push, 28 B event */
	void (*queue_work)(u32 cpu, u32 wq, u32 work);		/* queue_work_on */
	void (*call)(u32 fn, u32 a0, u32 a1, u32 a2);		/* sensor callback */
	const struct t32_is_glob *g;
};

static inline u32 t32_is_fch(u32 priv, u32 pipe, u32 ch)
{
	return t32_ic_r32(priv, T32_FD_FCH) + (3 * pipe + ch) * T32_FD_FCH_SIZE;
}

/* stop request: VIC off, interrupts off, push the stop event (type 1, subdev); -22 when it cannot be queued */
static inline int t32_is_stop(const struct t32_is_ops *ops, u32 sd, u32 line)
{
	u32 ev[7] = { 0 };

	ops->vic_stop();
	ops->vic_reset();
	ops->intp_disable();
	ev[2] = 1;
	ev[3] = sd;
	if (ops->event_push(0, ev)) {
		ops->print_line(line);
		return -22;
	}
	return 0;
}

/* first buffer of a list head, 0 when the list is empty */
static inline u32 t32_is_first(u32 head)
{
	u32 e = t32_ic_r32(head, 0);

	return e == head ? 0 : e;
}

/* step 6 outside mode 1: arm channel ch of the new pipe */
static inline void t32_is_arm(const struct t32_is_ops *ops, u32 priv, u32 ch)
{
	const struct t32_is_glob *g = ops->g;
	u32 fc = t32_is_fch(priv, *g->cur, ch), q, d, b;

	if (!t32_ic_r32(fc, T32_FC_STATE) || ops->get_frame_drop(fc)) {
		ops->chx_en(*g->cur, ch, 0);
		return;
	}
	q = t32_is_first(fc + T32_FC_QUEUE);
	d = t32_ic_r32(priv, T32_FD_FCH) + T32_FD_FCH_SIZE * t32_ic_r32(fc, T32_FC_FRAMES);
	b = t32_is_first(d + T32_FC_DROPQ);
	if (!q && !b) {
		ops->chx_en(*g->cur, ch, 0);
		return;
	}
	ops->fifo_clear(ch);
	ops->chx_en(*g->cur, ch, 1);
	if (q) {
		ops->fifo_write(*g->cur, ch, t32_ic_r32(q, 8), t32_ic_r32(q, 12));
		return;
	}
	ops->fifo_write(*g->cur, ch, t32_ic_r32(b, 8), t32_ic_r32(b, 12));
	t32_fd_move_tail(b, fc + T32_FC_QUEUE);	/* a drop-list buffer goes back to the queue */
}

static inline int t32_ispcore_interrupt_service_routine(const struct t32_is_ops *ops, u32 sd)
{
	const struct t32_is_glob *g = ops->g;
	u32 priv = t32_ic_r32(sd, T32_IS_PRIV), st[8] = { 0 }, err, done, i, ret, sen, cnt;
	u8 *ts = T32_IC_P(priv + T32_IS_TS_STATE);
	int r;

	ops->handler(st);
	err = st[2];
	if (err) {
		if (err != 0x8000)
			ops->print_err(err, t32_ic_r32(t32_ic_r32(sd, T32_IS_REGS), T32_IS_REG_ERR));
		cnt = priv + 4 * *g->cur;
		t32_ic_w32(cnt, T32_IS_ERR_CNT, t32_ic_r32(cnt, T32_IS_ERR_CNT) + 1);
		if (err & 0x20)
			t32_ic_w32(cnt, T32_IS_ERR_CNT20, t32_ic_r32(cnt, T32_IS_ERR_CNT20) + 1);
		if (err & 0x10)
			t32_ic_w32(cnt, T32_IS_ERR_CNT10, t32_ic_r32(cnt, T32_IS_ERR_CNT10) + 1);
		if (t32_ic_r32(priv, T32_FD_MODE) != 1) {
			*g->reset = 1;
			ops->core_reset(priv);
			ops->vic_reset();
			return 1;
		}
	}
	if ((st[5] >> 16) & 1) {
		for (i = 0; i < 3; i++) {
			u32 fc = t32_is_fch(priv, *g->cur, i);

			if (!t32_ic_r32(fc, T32_FC_STATE))
				ops->reg_stream_off((u8)((int)t32_ic_r32(fc, T32_FC_FRAMES) % 3));
		}
		if (*g->reset == 1) {
			*g->reset = 0;
			*g->seq_start = 1;
			*g->npipe = 0;
			*g->cur = 0;
			if (*g->rst_wait == 1) {
				ops->complete(g->wait);
				*g->rst_wait = 0;
			}
			ops->trig();
			t32_ic_w32(t32_ic_r32(sd, T32_IS_REGS), T32_IS_REG_TRIG, 1);
			return 1;
		}
		(*g->nstop)++;
	}
	ret = ops->process(*g->cur);

	/* step 4: time stamps, frame-done channels, frame wait */
	if (st[0] & (1U << 20)) {
		if (*ts == 1) {
			ops->gettimeofday(priv + T32_IS_TS0);
			*ts = 2;
		}
		if (*ts == 3) {
			ops->gettimeofday(priv + T32_IS_TS2);
			*ts = 4;
		}
	}
	if ((st[0] & (1U << 24)) && *ts == 2) {
		ops->gettimeofday(priv + T32_IS_TS1);
		*ts = 3;
	}
	for (i = 0; i < 3; i++)
		if (st[1] & (1U << i))
			ops->framedone(t32_ic_r32(sd, T32_IS_PRIV), *g->cur, i);
	done = st[0] & 1;
	if (done && t32_ic_r32(priv, T32_IS_WAIT_ON) == 1) {
		u32 w = t32_ic_r32(priv, T32_IS_WAIT + 4 * t32_ic_r32(priv, T32_IS_WAIT_IDX));

		if ((int)w < 0) {
			cnt = priv + 4 * *g->cur;
			if ((w & 0xffff) >= t32_ic_r32(priv, T32_IS_WAIT_LIMIT)) {
				t32_ic_w32(cnt, T32_IS_WAIT, 0);
				ops->complete(priv + T32_IS_WAIT_DONE + 12 * *g->cur);
			} else {
				t32_ic_w32(cnt, T32_IS_WAIT, t32_ic_r32(cnt, T32_IS_WAIT) + 1);
			}
		}
	}

	/* step 5: pipe done? */
	if (t32_ic_r32(priv, T32_FD_MODE) == 1) {
		if (done)
			*g->done = 1;
	} else {
		if (done)
			g->seen[0] = 1;
		for (i = 0; i < 3; i++)
			if (ops->get_chx_en(*g->cur, i) != 1 || (st[1] & (1U << i)))
				g->seen[1 + i] = 1;
		if (g->seen[0] == 1 && g->seen[1] == 1 && g->seen[2] == 1 && g->seen[3] == 1) {
			*g->done = 1;
			memset(g->seen, 0, 4);
		}
	}
	if (*g->done != 1)
		return ret;
	(*g->npipe)++;
	*g->done = 0;
	st[7] = *g->cur;
	sen = t32_ic_r32(priv, T32_IS_SENSOR);
	if (sen)
		ops->call(t32_ic_r32(sen, T32_IS_SENSOR_CB), sen, T32_IS_EV_FRAME, T32_IC_A(&st[6]));
	if (t32_ic_r32(priv, T32_IS_EV3 + 4 * *g->cur) == 1) {
		sen = t32_ic_r32(priv, T32_IS_SENSOR);
		if (sen)
			ops->call(t32_ic_r32(sen, T32_IS_SENSOR_CB), sen, T32_IS_EV_FRAME2, T32_IC_A(&st[6]));
		t32_ic_w32(priv, T32_IS_EV3 + 4 * *g->cur, 0);
	}
	if (*g->cur < 4)
		ops->queue_work(1, *g->wq, g->work[*g->cur]);
	*g->cur = *T32_IC_P(*g->seq + *g->npipe % 12);

	/* step 6: next pipe */
	if (t32_ic_r32(priv, T32_FD_MODE) != 1) {
		ops->rcfg_switch();
		if (*g->sw_wait == 1) {
			*g->sw_wait = 0;
			ops->complete(g->wait);
			return ret;
		}
	}
	if (t32_ic_r32(priv, T32_FD_MODE) == 1) {
		*g->cur = 0;
		if (t32_ic_r32(priv, T32_IS_STOP) == 1) {
			r = t32_is_stop(ops, sd, 1330);
			if (r)
				return r;
		}
		for (i = 0; i < 3; i++)
			ops->check_frame_drop(t32_is_fch(priv, *g->cur, i));
	} else if (t32_ic_r32(priv, T32_IS_STOP) == 1 && *g->cur == 0) {
		if (*g->drop_mode)
			ops->info_clear();
		ops->func_stop_reset();
		r = t32_is_stop(ops, sd, 1357);
		if (r)
			return r;
	} else {
		for (i = 0; i < 3; i++)
			t32_is_arm(ops, priv, i);
		if (*g->seq_start == 1) {
			if (*g->cur == 1)
				t32_ic_w32(t32_ic_r32(sd, T32_IS_REGS), T32_IS_REG_PIPE1, 1);
			if (*g->cur == t32_ic_r32(priv, T32_FD_MODE) - 1)
				*g->seq_start = 0;
		}
		if (t32_ic_r32(priv, T32_IS_STREAMOFF) == 1 && *g->cur == 0) {
			for (i = 0; i < 3 * t32_ic_r32(priv, T32_FD_MODE); i++) {
				ops->stream_off(i & 0xff);
				ops->chx_en((i / 3) & 0xff, i % 3, 0);
			}
		} else {
			ops->ldc_reflash(*g->cur);
			ops->rcfg_trig(*g->cur);
		}
	}
	if (t32_ic_r32(priv, T32_IS_STREAMOFF) == 1 && t32_ic_r32(priv, T32_FD_MODE) == 1)
		for (i = 0; i < 3; i++)
			ops->stream_off(i);
	return ret;
}

#endif
