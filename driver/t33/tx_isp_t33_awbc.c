// SPDX-License-Identifier: GPL-2.0
/*
 * T33 (PRJ008) per-frame AWB driver glue: tisp_awb_init's state part at
 * stream-on, and per AWB statistics interrupt tisp_awb_interrupt_static
 * (buffer index from 0x1608c) + tisp_awb_process (event 12) in a work.
 * The algorithm is ../t32/tx_isp_t33_awbalg.h (the T32 port of the AWB
 * algorithm, Matt Davis' recovered C, PR #15, with the T33 statistics
 * front).  Results go to the AWBG list 7 (gains) and the AWB list 29
 * (spec_calculate) through tx_isp_t32_hw.c, which runs the lists at the
 * next frame end like the vendor's tispAwbgRefresh / tispAwbRefresh.
 * T33 only: the T32 build does not compile this file.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/seq_file.h>
#include "tx_isp_t32.h"
#include "tx_isp_t33_awbalg.h"

#define T33_AWB_STAT_ONE	0x7400U	/* one zone statistics buffer (tx_isp_t33_awb.h) */

int isp_printf(unsigned int level, const char *fmt, ...);
u32 t33_hw_rd(u32 reg);
void t33_awb_list_gain(u32 r, u32 b);			/* tx_isp_t32_hw.c: AWBG list 7 slots 3 / 4 */
void t33_awb_list_slot(int slot, u32 v);		/* tx_isp_t32_hw.c: AWB list 29 slot */
void t33_awb_list_refresh(void);			/* tx_isp_t32_hw.c: run list 29 at the next AWB irq */

static struct t32_awb_init ai;
static struct t32_awb_ctd *ctd;
static u8 *par_copy;
static const u8 *stat_bufs;	/* the four 0x7400 byte buffers, uncached */
static u16 counter;
static bool ready;
static DEFINE_MUTEX(awbc_lock);
static struct {
	u32 frames, drops, still, refused, gain_r, gain_b, ct, path, tg_updates, ev_updates;
} awbc_st;
/* the vendor's tgain_old / ev_last (tisp_long_tgain_update / tisp_long_ev_update), not reset per stream */
static u32 tgain_old, ev_last[2], ct_last, ct_new;

/* AWB event 11 (tisp_ct_update) after this frame's AE events, as the vendor's FIFO has it */
static void awbc_ct_fn(struct work_struct *w)
{
	(void)w;
	t33_ct_refresh(ACCESS_ONCE(ct_new));
}
static DECLARE_WORK(awbc_ct_work, awbc_ct_fn);

static void *drv_alloc(void *p, u32 size)
{
	(void)p;
	return size > 0x10000 ? vzalloc(size) : kzalloc(size, GFP_KERNEL);	/* the run state is 117 KiB */
}
static void drv_nop(void *p, u32 ch) { (void)p; (void)ch; }	/* no RISC-V AWB */
/* tisp_awb_set_hardware_param: its list 29 words are in the init list; the state part here */
static void drv_hw_param(void *p, u32 ch)
{
	(void)p;
	(void)ch;
	t33_awb_regional_threshold(&ai.a);
	t33_awb_lum_th_freq(&ai.a);
}
static void drv_nop2(void *p, u32 ch, u32 v) { (void)p; (void)ch; (void)v; }
static void drv_print(void *p, int ok)
{
	(void)p;
	if (!ok)
		isp_printf(2, "tx-isp-t33: no memory for the AWB context\n");
}
static const struct t32_awb_init_ops drv_init_ops = {
	NULL, drv_alloc, drv_nop, drv_hw_param, drv_nop2, drv_nop2, drv_print,
};

static u32 drv_rd(void *p, u32 reg) { (void)p; return t33_hw_rd(reg); }

static void awbc_free(void)
{
	kfree(ai.tbl); kfree(ai.ctx); kfree(ai.mode); kfree(ai.a.hw);
	vfree(ai.a.st);
	vfree(ctd);
	kfree(par_copy);
	memset(&ai, 0, sizeof(ai));
	ctd = NULL;
	par_copy = NULL;
}

static void awbc_work_fn(struct work_struct *w)
{
	struct t33_awb_out *o;
	const u8 *buf;
	unsigned int i;
	int s;

	(void)w;
	o = kmalloc(sizeof(*o), GFP_KERNEL);
	if (!o)
		return;
	mutex_lock(&awbc_lock);
	if (!ready)
		goto out;
	/* tisp_awb_interrupt_static: the buffer the engine filled */
	buf = stat_bufs + t33_awb_buf_idx(t33_hw_rd(T33_AWB_IDX_REG)) * T33_AWB_STAT_ONE;
	t33_awb_load(ai.a.st, buf, ai.a.par->zones_h, ai.a.par->zones_w);
	if (++ai.a.st->frame_cnt == 0xffffffffU)
		ai.a.st->frame_cnt = 0;
	if (t33_awb_process(&ai.a, ctd, drv_rd, NULL, buf, &counter, &awbc_st.drops, o)) {
		awbc_st.refused++;
		goto out;
	}
	for (i = 0; i < o->nspec; i++) {
		s = t33_awb_slot(o->spec[i].reg);
		if (s >= 0)
			t33_awb_list_slot(s, o->spec[i].val);
	}
	if (o->gain_set)
		t33_awb_list_gain(o->gain_r, o->gain_b);
	awbc_st.frames++;
	awbc_st.still += o->rc == 1;
	awbc_st.path = o->path;
	awbc_st.gain_r = o->gain_r;
	awbc_st.gain_b = o->gain_b;
	awbc_st.ct = o->ct;
	if (o->ct != ct_last) {
		ct_last = o->ct;
		ACCESS_ONCE(ct_new) = o->ct;
		t33_evq_queue(&awbc_ct_work);
	}
out:
	mutex_unlock(&awbc_lock);
	kfree(o);
}
static DECLARE_WORK(awbc_work, awbc_work_fn);

/* the AWB statistics interrupt */
void t33_awbc_kick(void)
{
	if (ACCESS_ONCE(ready))
		t33_evq_queue(&awbc_work);
}

/*
 * AE events from the AE work (same ordered queue, after this frame's AWB
 * work as the vendor's event FIFO has them):
 *   6 (long EV, v0 / v1 = low / high word): tisp_long_ev_update ->
 *     tisp_awb_ev_update on an EV change;
 *   4 (long total gain, log2 Q16; v1 = force): tisp_long_tgain_update ->
 *     tisp_awb_tgain_update on a change: thresholds, list 29 words and
 *     tispAwbRefresh (list 29 at the next AWB interrupt).
 */
void t33_awbc_ae_event(u32 type, u32 v0, u32 v1)
{
	u8 slot[T33_AWB_TG_SLOTS];
	u32 val[T33_AWB_TG_SLOTS];
	unsigned int k;

	mutex_lock(&awbc_lock);
	if (!ready)
		goto out;
	if (type == 6 && (v0 != ev_last[0] || v1 != ev_last[1])) {
		t33_awb_ev_update(&ai.a, v0);
		ev_last[0] = v0;
		ev_last[1] = v1;
		awbc_st.ev_updates++;
	} else if (type == 4 && (v0 != tgain_old || v1)) {
		t33_awb_tgain_update(&ai.a, v0);
		t33_awb_tg_slots(&ai.a, slot, val);
		for (k = 0; k < T33_AWB_TG_SLOTS; k++)
			t33_awb_list_slot(slot[k], val[k]);
		t33_awb_list_slot(1, 1);	/* system_reg_set_awb_trig(1) */
		t33_awb_list_refresh();
		tgain_old = v0;
		awbc_st.tg_updates++;
	}
out:
	mutex_unlock(&awbc_lock);
}

void t33_awbc_detach(void)
{
	ACCESS_ONCE(ready) = false;
	cancel_work_sync(&awbc_work);
	cancel_work_sync(&awbc_ct_work);
	mutex_lock(&awbc_lock);
	awbc_free();
	mutex_unlock(&awbc_lock);
}

/*
 * stream-on, after the AWB lists: sec = the IQ bin AWB section (T32 layout,
 * 6740 B used), bufs = the four statistics buffers (uncached).  The
 * start gains of tisp_awb_init's set_gain are already in list 7.
 */
int t33_awbc_attach(const u8 *sec, u32 len, u32 width, u32 height, const u8 *bufs)
{
	struct t32_stats_write w[T32_AWB_MAX_WRITES];
	int n, ret = 0;

	t33_awbc_detach();
	t33_evq_get();
	mutex_lock(&awbc_lock);
	memset(&awbc_st, 0, sizeof(awbc_st));
	counter = 0;
	if (len < sizeof(struct t32_awb_par) || !bufs) {
		ret = -EINVAL;
		goto out;
	}
	par_copy = kmemdup(sec, sizeof(struct t32_awb_par), GFP_KERNEL);
	ctd = vzalloc(sizeof(*ctd));
	if (!par_copy || !ctd) {
		ret = -ENOMEM;
		goto out;
	}
	n = t32_awb_init(&ai, 0, par_copy, width, height, 0, &drv_init_ops, w);
	if (n < 0) {
		ret = n;
		goto out;
	}
	ai.a.st->hist[0] = ai.a.hw->inv_r;
	ai.a.st->hist[1] = ai.a.hw->inv_b;
	stat_bufs = bufs;
out:
	if (ret)
		awbc_free();
	ACCESS_ONCE(ready) = !ret;
	mutex_unlock(&awbc_lock);
	return ret;
}

void t33_awbc_show(struct seq_file *m)
{
	mutex_lock(&awbc_lock);
	seq_printf(m, "t33 awb: %s frames %u still %u drops %u refused %u path %d gain r 0x%x b 0x%x ct %u tgain %u/0x%x ev %u/%u\n",
		   ready ? "on" : "off", awbc_st.frames, awbc_st.still, awbc_st.drops, awbc_st.refused,
		   awbc_st.path, awbc_st.gain_r, awbc_st.gain_b, awbc_st.ct, awbc_st.tg_updates,
		   ready ? ai.a.st->tgain : 0, awbc_st.ev_updates, ready ? ai.a.st->ev : 0);
	mutex_unlock(&awbc_lock);
}

/*
 * The tuning controls (../t32/tx_isp_t32_tunblk.c) on the live T33 AWB
 * blocks, as in ../t32/tx_isp_t32_awbc.c (same t32_awb_init state).
 */
bool t32_awbc_api_get(struct t32_awb_ctx *c)
{
	mutex_lock(&awbc_lock);
	if (!ready) {
		mutex_unlock(&awbc_lock);
		return false;
	}
	c->tune = (u8 *)ai.a.par;
	c->sec = ai.a.st->raw;
	c->mode = (u32 *)ai.mode;
	c->base = (u8 *)ai.a.par - T32_AWBI_PAR_OFF;
	return true;
}

void t32_awbc_api_put(void)
{
	mutex_unlock(&awbc_lock);
}

/*
 * tisp_awb_set_start's tail call tisp_awb_set_gain (lock held): the T33
 * takes the gain words (0x6040 / 0x6044 of the T32 write list) into the
 * AWBG list 7 and keeps the inverses in the history pair (t33_awb_process).
 */
void t32_awbc_api_set_gain(void)
{
	struct t32_stats_write w[T32_AWB_MAX_WRITES];
	unsigned int i, n = t32_awb_set_gain(&ai.a, w);
	u32 r = 0, b = 0;
	bool set = false;

	ai.a.st->hist[0] = ai.a.hw->inv_r;
	ai.a.st->hist[1] = ai.a.hw->inv_b;
	for (i = 0; i < n; i++) {
		if (w[i].reg == 0x6040) {
			r = w[i].val;
			set = true;
		} else if (w[i].reg == 0x6044) {
			b = w[i].val & 0xffff;
		}
	}
	if (set)
		t33_awb_list_gain(r, b);
}
