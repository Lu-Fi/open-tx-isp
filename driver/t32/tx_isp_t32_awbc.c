// SPDX-License-Identifier: GPL-2.0
/*
 * T32 per-frame vendor AWB in the driver (openimp #234), behind
 * t32_ported_paths bit 14 (T32_PP_AWB).
 *
 * Stream-on (after the AWB statistics init, t32_awb_stats_init: the vendor
 * tisp_awb_malloc_cfg and tisp_awb_set_hardware_param): the AWB context as
 * tisp_awb_init leaves it (t32_awb_init, tx_isp_t32_awbinit.h, oracle
 * t32_awbinit_oracle) on a copy of the IQ bin AWB section, and its
 * tisp_awb_set_gain register writes.
 *
 * Every AWB statistics interrupt (system irq 3, ISP status bit 3) schedules
 * one AWB frame: tisp_awb_interrupt_static (the buffer the hardware filled,
 * unpacked into the run state) and tisp_awb_process, which the vendor runs
 * from event 12: long_par_update, spec_calculate, long_alogrithm, set_gain
 * (tx_isp_t32_awbproc.h, oracles t32_awbpr1..3).  Its register writes go
 * out in order (direct mode).  The colour temperature of the vendor's
 * event 11 goes to the ct fan-out (tisp_ct_update, tx_isp_t32_ctup.c,
 * behind bit 15) after the frame, and stays readable: t32_awbc_ct().
 *
 * The AE inputs (vendor event thread): t32_awbc_ev (AE event 6 ->
 * tisp_long_ev_update -> tisp_awb_ev_update: st ev = EV >> 10) and
 * t32_awbc_tgain (AE event 4 -> tisp_long_tgain_update ->
 * tisp_awb_tgain_update: st tgain and the regional / luma thresholds,
 * tx_isp_t32_awbtg.h, oracle t32_awbtg_oracle), called by the AE work
 * (tx_isp_t32_ae.c) on a change, as the vendor dispatchers do.  Until the
 * first AE frame they keep their init values (ev 400, tgain 1024).
 * Not wired: the RISC-V AWB (f0900 = 0).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "tx_isp_t32.h"
#include "tx_isp_t32_awbtg.h"

int isp_printf(unsigned int level, const char *fmt, ...);

struct t32_awbc_drv_stats t32_awbc_stats;

static struct t32_awb_init ai;
static struct t32_awb_ctd *ctd;
static struct t32_awb_proc proc;
static u8 *par_copy;
static struct t32_awb_lumov lumov;	/* the vendor e16c4 words (word 0 = ai.proc_flag) */
static const u8 *stat_bufs;
static u16 counter;
static u32 last_ct;
static bool ready, ct_valid;
static DEFINE_MUTEX(awbc_lock);

/* ---- the hooks of t32_awb_init ---- */
static void *drv_alloc(void *p, u32 size)
{
	(void)p;
	return size > 0x10000 ? vzalloc(size) : kzalloc(size, GFP_KERNEL);	/* the run state is 117 KiB */
}
static void drv_riscv(void *p, u32 ch) { (void)p; (void)ch; }		/* no RISC-V AWB (f0900 = 0) */
static void drv_hw_param(void *p, u32 ch) { (void)p; (void)ch; }	/* written by t32_awb_stats_init */
static void drv_irq_set(void *p, u32 ch, u32 irq) { (void)p; (void)ch; (void)irq; }	/* t32_isp_irq kicks */
static void drv_event_set(void *p, u32 ch, u32 ev) { (void)p; (void)ch; (void)ev; }	/* the work runs the process */
static void drv_print(void *p, int ok)
{
	(void)p;
	if (!ok)
		isp_printf(2, "tx-isp-t32: no memory for the AWB context\n");
}
static const struct t32_awb_init_ops drv_init_ops = {
	NULL, drv_alloc, drv_riscv, drv_hw_param, drv_irq_set, drv_event_set, drv_print,
};

static u32 drv_rd(void *p, u32 reg) { (void)p; return t32_hw_rd(reg); }

static void awbc_free(void)
{
	kfree(ai.tbl); kfree(ai.ctx); kfree(ai.mode); kfree(ai.a.hw);
	vfree(ai.a.st);
	vfree(ctd);
	kfree(par_copy);
	memset(&ai, 0, sizeof(ai));
	ctd = NULL;
	par_copy = NULL;
	ct_valid = false;
}

static void awbc_work_fn(struct work_struct *w)
{
	unsigned int i;

	(void)w;
	mutex_lock(&awbc_lock);
	if (!ready || !t32_pp_on(T32_PP_AWB)) {
		mutex_unlock(&awbc_lock);
		return;
	}
	if (t32_awb_frame(&ai, ctd, drv_rd, NULL, stat_bufs, &counter, &proc)) {
		t32_awbc_stats.refused++;	/* the vendor would read past one of its tables */
		mutex_unlock(&awbc_lock);
		return;
	}
	for (i = 0; i < proc.nw; i++)
		t32_hw_wr(proc.w[i].reg, proc.w[i].val);
	last_ct = proc.ev.ct;
	ct_valid = true;
	t32_awbc_stats.frames++;
	t32_awbc_stats.writes += proc.nw;
	t32_awbc_stats.ct = proc.ev.ct;
	t32_awbc_stats.gain_r = ai.a.hw->gain_r;
	t32_awbc_stats.gain_b = ai.a.hw->gain_b;
	mutex_unlock(&awbc_lock);
	t32_ctup_run(proc.ev.ct);	/* event 11: tisp_ct_update */
}
static DECLARE_WORK(awbc_work, awbc_work_fn);

/* the AWB statistics interrupt */
void t32_awbc_kick(void)
{
	if (READ_ONCE(ready))
		schedule_work(&awbc_work);
}

/* stream-on, after t32_awb_stats_init: sec = the IQ bin AWB section (6740 B), bufs = the four statistics buffers */
int t32_awbc_attach(const u8 *sec, u32 width, u32 height, const u8 *bufs)
{
	struct t32_stats_write w[T32_AWB_MAX_WRITES];
	int n, i, ret = 0;

	t32_awbc_detach();
	mutex_lock(&awbc_lock);
	memset(&t32_awbc_stats, 0, sizeof(t32_awbc_stats));
	counter = 0;
	memset(&lumov, 0, sizeof(lumov));
	par_copy = kmemdup(sec, sizeof(struct t32_awb_par), GFP_KERNEL);
	ctd = vzalloc(sizeof(*ctd));
	if (!par_copy || !ctd || !bufs) {
		ret = -ENOMEM;
		goto out;
	}
	n = t32_awb_init(&ai, 0, par_copy, width, height, 0, &drv_init_ops, w);
	if (n < 0) {
		ret = n;
		goto out;
	}
	for (i = 0; i < n; i++)
		t32_hw_wr(w[i].reg, w[i].val);
	stat_bufs = bufs;
out:
	if (ret)
		awbc_free();
	WRITE_ONCE(ready, !ret);
	mutex_unlock(&awbc_lock);
	return ret;
}

void t32_awbc_detach(void)
{
	WRITE_ONCE(ready, false);
	cancel_work_sync(&awbc_work);
	mutex_lock(&awbc_lock);
	awbc_free();
	mutex_unlock(&awbc_lock);
}

/* the colour temperature of the last AWB frame (vendor event 11); false before the first one */
bool t32_awbc_ct(u32 *ct)
{
	bool v;

	mutex_lock(&awbc_lock);
	v = ready && ct_valid;
	if (v)
		*ct = last_ct;
	mutex_unlock(&awbc_lock);
	return v;
}

/* AE event 6 (tisp_long_ev_update -> tisp_awb_ev_update): the low EV word */
void t32_awbc_ev(u32 ev)
{
	mutex_lock(&awbc_lock);
	if (ready && t32_pp_on(T32_PP_AWB)) {
		t32_awb_ev_update(&ai.a, ev);
		t32_awbc_stats.ev_updates++;
	}
	mutex_unlock(&awbc_lock);
}

/* AE event 4 (tisp_long_tgain_update -> tisp_awb_tgain_update): the low gain word, log2 Q16 */
void t32_awbc_tgain(u32 gain)
{
	struct t32_stats_write w[T32_AWBTG_MAX_WRITES];
	unsigned int n, i;

	mutex_lock(&awbc_lock);
	if (ready && t32_pp_on(T32_PP_AWB)) {
		lumov.w[0] = ai.proc_flag;
		n = t32_awb_tgain_apply(&ai.a, &lumov, gain, w);
		for (i = 0; i < n; i++)
			t32_hw_wr(w[i].reg, w[i].val);
		t32_awbc_stats.tgain_updates++;
		t32_awbc_stats.writes += n;
	}
	mutex_unlock(&awbc_lock);
}
