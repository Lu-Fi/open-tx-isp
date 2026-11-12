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

static struct t32_awb_init ai;
static struct t32_awb_ctd *ctd;
static u8 *par_copy;
static const u8 *stat_bufs;	/* the four 0x7400 byte buffers, uncached */
static u16 counter;
static bool ready;
static DEFINE_MUTEX(awbc_lock);
static struct {
	u32 frames, drops, still, refused, gain_r, gain_b, ct, path;
} awbc_st;

static void *drv_alloc(void *p, u32 size)
{
	(void)p;
	return size > 0x10000 ? vzalloc(size) : kzalloc(size, GFP_KERNEL);	/* the run state is 117 KiB */
}
static void drv_nop(void *p, u32 ch) { (void)p; (void)ch; }	/* no RISC-V AWB; hw param in list 29 at init */
static void drv_nop2(void *p, u32 ch, u32 v) { (void)p; (void)ch; (void)v; }
static void drv_print(void *p, int ok)
{
	(void)p;
	if (!ok)
		isp_printf(2, "tx-isp-t33: no memory for the AWB context\n");
}
static const struct t32_awb_init_ops drv_init_ops = {
	NULL, drv_alloc, drv_nop, drv_nop, drv_nop2, drv_nop2, drv_print,
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
out:
	mutex_unlock(&awbc_lock);
	kfree(o);
}
static DECLARE_WORK(awbc_work, awbc_work_fn);

/* the AWB statistics interrupt */
void t33_awbc_kick(void)
{
	if (ACCESS_ONCE(ready))
		schedule_work(&awbc_work);
}

void t33_awbc_detach(void)
{
	ACCESS_ONCE(ready) = false;
	cancel_work_sync(&awbc_work);
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
	seq_printf(m, "t33 awb: %s frames %u still %u drops %u refused %u path %d gain r 0x%x b 0x%x ct %u\n",
		   ready ? "on" : "off", awbc_st.frames, awbc_st.still, awbc_st.drops, awbc_st.refused,
		   awbc_st.path, awbc_st.gain_r, awbc_st.gain_b, awbc_st.ct);
	mutex_unlock(&awbc_lock);
}
