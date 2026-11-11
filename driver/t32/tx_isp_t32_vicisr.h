/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 20: isp_vic_interrupt_service_routine (0x3f60), the VIC interrupt.
 * lib_tisp_vic_intp_handle fills a status block (words 1 / 2: VIC status of channel 0 / 1,
 * words 3 / 4: MDMA status); for each of the two VIC channels ch (VIC private block = vic, the
 * subdev's +280):
 *  - frame start (0x10): the channel's buffer slot idx = ch + 2 when the VIC register
 *    0xb338020c + (ch << 12) has bits 0..2 set; in single-frame mode (+824 == 1, slot flag
 *    +844 + idx == 1) toggle the slot in the start mask (+848 + 4 * (idx & 1)) and queue the
 *    frame-start work (ispcore_update_fs_work(idx, 11, 0)), and when the mask is the one of
 *    +864 also the works of every channel left in it; queue the slot's fs work and switch the
 *    DMA to the slot (isp_vic_set_dma_enable(vic, idx)); print the first 11 frame starts per channel with the OST time (d9008 set); count
 *    down the slot's start budget (+2388 + 4 * idx while +2404 is set) and, when all channels
 *    (+1096) ran out, end the budget and notify the core (+152: event of +2408, then for one
 *    channel the VIC control 17424 / 17472 by the sensor's bus +340 -> +148, else event
 *    0x1000006);
 *  - frame done (0x1): count it (+904 + 16 * slot), call the frame-done hook (bss f3838, d9008
 *    set) with the channel's count, and in single-frame mode toggle the done mask (+856), stop
 *    the slot when it is the one of +864, use up the slot flag and, when nothing is pending
 *    (+820 set, +856 / +860 clear), complete +832 (+828 set) or end the mode (+820 = 0, event
 *    0x1000011);
 *  - errors (0xffffff00): reset and restart the VIC with one channel, else event 0x1000006; the
 *    error bits are counted in the 52 B counter block bss d934c + 52 * ch;
 *  - MDMA (word 3 + ch: bits 0 / 1 = channel 0 / 1 done, bit 3 = error): count (+968 / +972),
 *    vic_mdma_irq_function(vic, n) and, while a capture runs (+2412, mode +2436), count frames
 *    (+2424) and complete +2448 when +2428 is reached.
 * Result: 1 (IRQ_HANDLED), also for an invalid subdev.
 *
 * Vendor behaviour kept: the frame-done hook gets the count of the channel's slot 0/1 even
 * when the frame was in slot 2/3; the status bit 0x400 also reads subdev +244 -> +936 without
 * using it (not done here: a load without effect).
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_vicisr_oracle).
 */
#ifndef TX_ISP_T32_VICISR_H
#define TX_ISP_T32_VICISR_H

#include "tx_isp_t32_isppm.h"
#ifndef __KERNEL__
#include <stdio.h>
#endif

#define T32_VIC_STAT_REG(ch)	(0xb338020cU + ((u32)(ch) << 12))	/* KSEG1 */
#define T32_OST_COUNT		0xb0002078U
#define T32_VI_NCH		1096
#define T32_VI_SINGLE		824
#define T32_VI_SLOTFLAG		844	/* u8 per slot */
#define T32_VI_FSMASK		848
#define T32_VI_FDMASK		856
#define T32_VI_FDPEND		860
#define T32_VI_LASTMASK		864
#define T32_VI_PENDING		820
#define T32_VI_WAITING		828
#define T32_VI_DONE		832
#define T32_VI_FDCOUNT		904	/* 16 B per slot */
#define T32_VI_MDMA_CNT		968
#define T32_VI_SENSOR		340
#define T32_VI_BUDGET		2388
#define T32_VI_BUDGET_ON	2404
#define T32_VI_BUDGET_EV	2408
#define T32_VI_CAP_ON		2412
#define T32_VI_CAP_X		2420
#define T32_VI_CAP_N		2424
#define T32_VI_CAP_END		2428
#define T32_VI_CAP_MODE		2436
#define T32_VI_CAP_DONE		2448
#define T32_SD_NOTIFY		152
#define T32_VIC_EV_ERR		0x01000006U
#define T32_VIC_EV_SINGLE_END	0x01000011U

struct t32_vicisr_ops {
	void (*intp_handle)(u32 *st);			/* lib_tisp_vic_intp_handle(status block) */
	void (*fs_work)(u32 slot, u32 cmd, u32 arg);	/* ispcore_update_fs_work */
	void (*queue_fs)(u32 slot);			/* queue_work_on(1, system_wq, fs work of slot 0..3: cee24 cee14 cee04 cedf4) */
	void (*dma_enable)(u32 vic, u32 mode);		/* isp_vic_set_dma_enable */
	void (*print_start)(const char *msg, u32 ticks);	/* isp_printf(1, fmt, func, 541, msg, ticks) */
	int (*call)(u32 fn, u32 a0, u32 a1, u32 a2);
	void (*vic_ctrl)(u32 n, u32 v);			/* lib_tisp_vic_ctrl */
	void (*stop_byindex)(void);			/* lib_tisp_vic_stop_byindex */
	void (*complete)(u32 c);			/* private_complete */
	void (*vic_reset)(void);			/* lib_tisp_vic_reset */
	void (*stream_on)(void);			/* lib_tisp_vic_stream_on */
	void (*mdma)(u32 vic, u32 n);			/* vic_mdma_irq_function */
	u32 *print_on;					/* bss d9008 */
	u32 *start_cnt;					/* bss d9320, 2 words */
	u8 *budget_mask;				/* bss d931c */
	u32 *fd_hook;					/* bss f3838 */
	u32 *err_cnt;					/* bss d934c, 13 words per channel */
};

static inline void t32_vic_err_count(u32 *c, u32 st)
{
	static const u8 slot[32] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, 0, 12, 12, 12, 1, 12,	/* bits 0..8 unused */
				     12, 12, 2, 3, 4, 5, 6, 7, 8, 12, 12, 12, 9, 12, 12, 12 };
	u32 b;

	for (b = 9; b < 32; b++)
		if (st & (1U << b))
			c[slot[b]]++;
}

static inline u32 t32_vic_slot(u32 ch, u32 mask)
{
	return ch + ((t32_ic_r32(T32_VIC_STAT_REG(ch), 0) & mask) ? 2 : 0);
}

static inline void t32_vic_frame_start(const struct t32_vicisr_ops *ops, u32 sd, u32 vic, u32 ch)
{
	u32 reg = t32_ic_r32(T32_VIC_STAT_REG(ch), 0) & 7, idx = ch + (reg ? 2 : 0);
	u32 v = t32_ic_r32(sd, T32_SD_PRIV), m, k, n, cb, cnt, t;
	char buf[64];

	if (t32_ic_r32(v, T32_VI_SINGLE) == 1 && *T32_IC_P(v + T32_VI_SLOTFLAG + idx) == 1) {
		m = v + 4 * (idx & 1);
		t32_ic_w32(m, T32_VI_FSMASK, t32_ic_r32(m, T32_VI_FSMASK) ^ (1U << idx));
		ops->fs_work(idx, 11, 0);
		if (t32_ic_r32(m, T32_VI_FSMASK) && t32_ic_r32(m, T32_VI_LASTMASK) == idx)
			for (k = 0; k < t32_ic_r32(v, T32_VI_NCH); k++)
				if (t32_ic_r32(m, T32_VI_FSMASK) & (1U << k))
					ops->fs_work(k, 11, 0);
	}
	ops->queue_fs(idx);
	ops->dma_enable(vic, idx);
	if (*ops->print_on) {
		cnt = ops->start_cnt[ch]++;
		if ((int)cnt < 11) {
			memset(buf, 0, sizeof(buf));
			sprintf(buf, "VIC:%d  Frame %d Start", ch, cnt);
			t = t32_ic_r32(T32_OST_COUNT, 0);
			ops->print_start(buf, (t << 10) / 24000);
		}
	}
	v = t32_ic_r32(sd, T32_SD_PRIV);
	if (!t32_ic_r32(v, T32_VI_BUDGET_ON))
		return;
	m = v + 4 * idx;
	t32_ic_w32(m, T32_VI_BUDGET, t32_ic_r32(m, T32_VI_BUDGET) - 1);
	if ((int)t32_ic_r32(m, T32_VI_BUDGET) <= 0)
		*ops->budget_mask |= 1U << idx;
	n = t32_ic_r32(v, T32_VI_NCH);
	if (*ops->budget_mask != (1U << (n & 31)) - 1)
		return;
	t32_ic_w32(v, T32_VI_BUDGET_ON, 0);
	*ops->budget_mask = 0;
	cb = t32_ic_r32(sd, T32_SD_NOTIFY);
	if (n < 2) {
		if (cb)
			ops->call(cb, sd, t32_ic_r32(v, T32_VI_BUDGET_EV), 0);
		k = t32_ic_r32(t32_ic_r32(v, T32_VI_SENSOR), 148) == 2 ? 17424 : 17472;
		ops->vic_ctrl(0, k);
		ops->vic_ctrl(1, k);
		return;
	}
	if (!cb)
		return;
	ops->call(cb, sd, t32_ic_r32(v, T32_VI_BUDGET_EV), 0);
	cb = t32_ic_r32(sd, T32_SD_NOTIFY);
	if (cb)
		ops->call(cb, sd, T32_VIC_EV_ERR, 0);
}

static inline void t32_vic_frame_done(const struct t32_vicisr_ops *ops, u32 sd, u32 vic, u32 ch)
{
	u32 idx = t32_vic_slot(ch, 0x700), v, m, a, b, cb;

	t32_ic_w32(vic + 16 * idx, T32_VI_FDCOUNT, t32_ic_r32(vic + 16 * idx, T32_VI_FDCOUNT) + 1);
	if (*ops->print_on && *ops->fd_hook)
		ops->call(*ops->fd_hook, t32_ic_r32(vic + 16 * ch, T32_VI_FDCOUNT), 0, 0);
	v = t32_ic_r32(sd, T32_SD_PRIV);
	if (t32_ic_r32(v, T32_VI_SINGLE) != 1)
		return;
	a = v + idx;
	b = *T32_IC_P(a + T32_VI_SLOTFLAG);
	if (b == 1) {
		m = v + 4 * (idx & 1);
		t32_ic_w32(m, T32_VI_FDMASK, t32_ic_r32(m, T32_VI_FDMASK) ^ (1U << idx));
		if (t32_ic_r32(m, T32_VI_LASTMASK) == idx) {
			t32_ic_w32(m, T32_VI_FDMASK, 0);
			ops->stop_byindex();
		}
		b = *T32_IC_P(a + T32_VI_SLOTFLAG);
	}
	if (b)
		*T32_IC_P(a + T32_VI_SLOTFLAG) = b - 1;
	if (!t32_ic_r32(v, T32_VI_PENDING) || t32_ic_r32(v, T32_VI_FDMASK) || t32_ic_r32(v, T32_VI_FDPEND))
		return;
	if (t32_ic_r32(v, T32_VI_WAITING)) {
		t32_ic_w32(v, T32_VI_WAITING, 0);
		ops->complete(v + T32_VI_DONE);
		return;
	}
	t32_ic_w32(v, T32_VI_PENDING, 0);
	cb = t32_ic_r32(sd, T32_SD_NOTIFY);
	if (cb)
		ops->call(cb, sd, T32_VIC_EV_SINGLE_END, 0);
}

static inline void t32_vic_mdma(const struct t32_vicisr_ops *ops, u32 vic, u32 ch, u32 n)
{
	u32 mode;

	t32_ic_w32(vic + 16 * ch, T32_VI_MDMA_CNT + 4 * n, t32_ic_r32(vic + 16 * ch, T32_VI_MDMA_CNT + 4 * n) + 1);
	ops->mdma(vic, n);
	if (!t32_ic_r32(vic, T32_VI_CAP_ON))
		return;
	mode = t32_ic_r32(vic, T32_VI_CAP_MODE);
	if (n == 0 ? mode != 0 : mode - 1 >= 2)
		return;
	t32_ic_w32(vic, T32_VI_CAP_N, t32_ic_r32(vic, T32_VI_CAP_N) + 1);
	if (t32_ic_r32(vic, T32_VI_CAP_N) != t32_ic_r32(vic, T32_VI_CAP_END))
		return;
	ops->complete(vic + T32_VI_CAP_DONE);
	t32_ic_w32(vic, T32_VI_CAP_X, 0);
	t32_ic_w32(vic, T32_VI_CAP_ON, 0);
}

static inline int t32_isp_vic_interrupt_service_routine(const struct t32_vicisr_ops *ops, u32 sd)
{
	u32 st[8] = { 0 }, vic, ch, s, s2, cb;

	vic = t32_pm_priv(sd);
	if (!vic)
		return 1;
	ops->intp_handle(st);
	for (ch = 0; ch < 2; ch++) {
		s = st[1 + ch];
		s2 = st[3 + ch];
		if (s & 0x10)
			t32_vic_frame_start(ops, sd, vic, ch);
		if (s & 0x1)
			t32_vic_frame_done(ops, sd, vic, ch);
		if (s & 0xffffff00U) {
			if (t32_ic_r32(vic, T32_VI_NCH) == 1) {
				ops->vic_reset();
				ops->stream_on();
			} else if ((cb = t32_ic_r32(sd, T32_SD_NOTIFY))) {
				ops->call(cb, sd, T32_VIC_EV_ERR, 0);
			}
		}
		t32_vic_err_count(ops->err_cnt + 13 * ch, s);
		if (s2 & 1)
			t32_vic_mdma(ops, vic, ch, 0);
		if (s2 & 2)
			t32_vic_mdma(ops, vic, ch, 1);
		if (s2 & 8)
			ops->err_cnt[13 * ch + 10]++;
	}
	return 1;
}

#endif /* TX_ISP_T32_VICISR_H */
