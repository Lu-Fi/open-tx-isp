// SPDX-License-Identifier: GPL-2.0
/*
 * T32 AF and GSM blocks in the driver (openimp #234, #296): the vendor's
 * tisp_af_init / tisp_gsm_init, their interrupt bits and events and the AF /
 * GSM tuning ids, on the composition of tx_isp_t32_afgsm.h (oracle
 * t32_afgsm_oracle, over the block ports t32_af_oracle / t32_gsm_oracle).
 *
 *  - Stream-on (tx_isp_t32_hw.c): tisp_af_init on IQ bin section 24 (the
 *    statistics DMA buffer, kmalloc 16 KiB, 4 slots at 0x18108 .., the
 *    register image 0x18010 ..), later tisp_gsm_init (0x1904c).  Both start
 *    from the vendor's module .data at every attach.
 *  - ISP interrupt (status words 0x30028 / 0x300a8): bit 6
 *    tisp_af_interrupt_static (DMA slot -> statistics arrays, event 15), bit
 *    7 tisp_gsm_interrupt_hist (RAM read, event 16), bit 63
 *    tisp_gsm_irq_frmae_done, in the hard interrupt as in the vendor.
 *  - Events 15 / 16 run in a work item (the vendor's event thread):
 *    tisp_af_calc_process (metrics), tisp_gsm_process (zone weights).
 *  - Per frame: tisp_long_tgain_update hands the gain to tisp_af_tgain_update
 *    (tx_isp_t32_tisp.c).
 *  - Tuning: the statistics config (AF window attribute, GSM zone bounds),
 *    AF weight, AF statistics and AF metrics run on the live blocks
 *    (tx_isp_t32_tunblk.c / tunctl.c); before the attach or with the
 *    block's bit off they answer as before (attribute words / stored weight
 *    / zeros).
 * Behind t32_ported_paths bit 17 (T32_PP_AF) and bit 18 (T32_PP_GSM).
 * Direct register mode (d03a0 = 0).  Channel 0 only (the driver has one).
 */
#include <linux/types.h>
#include <linux/string.h>
#include "tx_isp_t32_afgsm.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <asm/io.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_iq.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define AFG_EVQ		16

struct t32_afg_drv_stats t32_afg_stats;

static DEFINE_SPINLOCK(afg_lock);	/* g against the interrupt, the event work and the tuning ids */
static struct t32_afg *g;		/* vmalloc: the block state (tables, GSM RAM copies) */
static bool af_live, gsm_live;
static u8 evq[AFG_EVQ], evq_r, evq_w;

static void *drv_vm(u32 size) { return vzalloc(size); }
static void *drv_km(u32 size, u32 flags) { (void)flags; return kmalloc(size, GFP_KERNEL); }
static void drv_vf(void *p) { vfree(p); }
static void drv_kf(void *p) { kfree(p); }
static u32 drv_rd(u32 reg) { return t32_hw_rd(reg); }

/* private_dma_cache_sync (0, p, len, DMA_BIDIRECTIONAL): write back and invalidate */
static void drv_sync(u32 dev, void *p, u32 len, u32 dir)
{
	(void)dev;
	(void)dir;
#ifdef CONFIG_MIPS
	dma_cache_wback_inv((unsigned long)p, len);
#else
	(void)p; (void)len;
#endif
}

static void afg_work_fn(struct work_struct *w);
static DECLARE_WORK(afg_work, afg_work_fn);

/* tisp_event_push from the hard interrupt (afg_lock held) */
static void ev_push(u32 type)
{
	u8 n = (u8)((evq_w + 1) % AFG_EVQ);

	if (n == evq_r) {
		t32_afg_stats.ev_full++;
		return;
	}
	evq[evq_w] = (u8)type;
	evq_w = n;
	schedule_work(&afg_work);
}

static void drv_push(u32 ch, const u32 *ev) { (void)ch; ev_push(ev[2]); }
static void drv_gpush(u8 ch, const struct t32_gsm_event *ev) { (void)ch; ev_push(ev->type); }
static void drv_irq_set(u32 ch, u32 irq, void *fn) { (void)ch; (void)irq; (void)fn; }	/* bits: t32_afg_irq */
static void drv_cb_set(u32 ch, u32 ev, void *fn) { (void)ch; (void)ev; (void)fn; }	/* events: afg_work_fn */

static const struct t32_af_ops drv_ops = {
	drv_vm, drv_km, drv_vf, drv_kf, drv_rd, drv_sync, drv_push, drv_irq_set, drv_cb_set,
};

static void flush(const struct t32_front_write *w, int n)
{
	int i;

	for (i = 0; i < n; i++)
		t32_hw_wr(w[i].reg, w[i].val);
}

/* the vendor's event thread for events 15 / 16 */
static void afg_work_fn(struct work_struct *w)
{
	unsigned long f;

	(void)w;
	spin_lock_irqsave(&afg_lock, f);
	while (evq_r != evq_w) {
		u32 type = evq[evq_r];

		evq_r = (u8)((evq_r + 1) % AFG_EVQ);
		if (g && ((type == T32_AFG_EV_AF && af_live) || (type == T32_AFG_EV_GSM && gsm_live))) {
			t32_afg_event(g, 0, type);
			if (type == T32_AFG_EV_AF)
				t32_afg_stats.af_events++;
			else
				t32_afg_stats.gsm_events++;
		}
	}
	spin_unlock_irqrestore(&afg_lock, f);
}

/* status words 0x30028 (bits 6 / 7) and 0x300a8 (bit 31 = system irq 63), hard interrupt */
void t32_afgsm_irq(u32 sta, u32 sta3)
{
	struct t32_front_write w[8];
	unsigned long f;
	u32 s0 = 0, s3 = 0;

	if (!(sta & ((1U << T32_AFG_IRQ_AF) | (1U << T32_AFG_IRQ_GSM))) && !(sta3 & (1U << 31)))
		return;
	spin_lock_irqsave(&afg_lock, f);
	if (af_live && t32_pp_on(T32_PP_AF))
		s0 |= sta & (1U << T32_AFG_IRQ_AF);
	if (gsm_live && t32_pp_on(T32_PP_GSM)) {
		s0 |= sta & (1U << T32_AFG_IRQ_GSM);
		s3 = sta3 & (1U << 31);
	}
	if (s0 & (1U << T32_AFG_IRQ_AF))
		t32_afg_stats.af_irqs++;
	if (s0 & (1U << T32_AFG_IRQ_GSM))
		t32_afg_stats.gsm_irqs++;
	if (s0 | s3)
		flush(w, t32_afg_irq(g, s0, s3, 0, drv_rd, drv_gpush, w));
	spin_unlock_irqrestore(&afg_lock, f);
}

/* stop both blocks and free their buffers (tisp_af_deinit); process context */
void t32_afgsm_detach(void)
{
	unsigned long f;
	bool was;

	spin_lock_irqsave(&afg_lock, f);
	was = af_live;
	af_live = gsm_live = false;
	evq_r = evq_w = 0;
	spin_unlock_irqrestore(&afg_lock, f);
	cancel_work_sync(&afg_work);
	if (g && was)
		t32_af_deinit(&g->af, 0);
}

/* stream-on: tisp_af_init(0, {width, height}) on the IQ bin's AF section */
void t32_afgsm_attach_af(const u8 *bin, u32 bytes, u32 width, u32 height)
{
	struct t32_front_write *w;
	const u8 *sec;
	unsigned long f;
	int n;

	t32_afgsm_detach();
	if (!t32_pp_on(T32_PP_AF) && !t32_pp_on(T32_PP_GSM))
		return;
	if (!g) {
		g = vzalloc(sizeof(*g));
		if (!g)
			return;
	}
	g->af.ops = &drv_ops;
	t32_afg_data(g);	/* both blocks from the module .data, as at the vendor's load */
	sec = t32_afg_af_section(bin, bytes);
	if (!sec || !t32_pp_on(T32_PP_AF))
		return;
	w = kmalloc_array(T32_AF_MAX_WRITES, sizeof(*w), GFP_KERNEL);
	if (!w)
		return;
	n = t32_afg_af_init(g, 0, sec, width, height, w);
	if (!g->af.ctx[0] || !g->af.ctx[0]->par || !g->af.ctx[0]->regs || !g->af.ctx[0]->blk ||
	    !g->af.dma[0] || !g->af.dma[0]->buf) {
		isp_printf(2, "tx-isp-t32: AF init: no memory\n");
		kfree(w);
		return;	/* not live; the next attach frees what was allocated */
	}
	flush(w, n);
	kfree(w);
	spin_lock_irqsave(&afg_lock, f);
	af_live = true;
	spin_unlock_irqrestore(&afg_lock, f);
	t32_afg_stats.af_attaches++;
}

/* stream-on, after the AF attach: tisp_gsm_init({width, height}) */
void t32_afgsm_attach_gsm(u32 width, u32 height)
{
	struct t32_front_write w[2];
	unsigned long f;

	if (!g || !t32_pp_on(T32_PP_GSM))
		return;
	flush(w, t32_afg_gsm_init(g, width, height, w));
	spin_lock_irqsave(&afg_lock, f);
	gsm_live = true;
	spin_unlock_irqrestore(&afg_lock, f);
	t32_afg_stats.gsm_attaches++;
}

void t32_afgsm_exit(void)
{
	t32_afgsm_detach();
	vfree(g);
	g = NULL;
}

/* tisp_long_tgain_update -> tisp_af_tgain_update (low gain word) */
void t32_afgsm_tgain(u32 ch, u32 gain)
{
	unsigned long f;

	spin_lock_irqsave(&afg_lock, f);
	if (af_live && !ch && t32_pp_on(T32_PP_AF))
		t32_af_tgain_update(&g->af, ch, gain);
	spin_unlock_irqrestore(&afg_lock, f);
}

/* ---- tuning ids: return false (caller's fallback) while the block is not live ---- */

#define AFG_API(cond, body)						\
	unsigned long f;						\
	bool ok;							\
									\
	spin_lock_irqsave(&afg_lock, f);				\
	ok = !ch && (cond);						\
	if (ok) {							\
		body;							\
	}								\
	spin_unlock_irqrestore(&afg_lock, f);				\
	return ok

#define AF_ON	(af_live && t32_pp_on(T32_PP_AF))
#define GSM_ON	(gsm_live && t32_pp_on(T32_PP_GSM))

/* tisp_af_api_get_attr (24 B, tisp_g_statis_config_attr) */
bool t32_afgsm_af_get_attr(u32 ch, u8 *out)
{
	AFG_API(AF_ON, t32_af_api_get_attr(&g->af, 0, out));
}

/* tisp_af_api_set_attr (tisp_s_statis_config_attr): par_init + register image */
bool t32_afgsm_af_set_attr(u32 ch, const u8 *in)
{
	struct t32_front_write w[T32_AF_MAX_WRITES];

	AFG_API(AF_ON, flush(w, t32_af_api_set_attr(&g->af, 0, in, w)));
}

bool t32_afgsm_af_set_weight(u32 ch, const u8 *buf)
{
	AFG_API(AF_ON, t32_af_api_set_weight(&g->af, 0, buf));
}

bool t32_afgsm_af_get_weight(u32 ch, u8 *buf)
{
	AFG_API(AF_ON, t32_af_api_get_weight(&g->af, 0, buf));
}

/* tisp_af_api_get_statis_info: 5400 B */
bool t32_afgsm_af_get_statis(u32 ch, u8 *buf)
{
	AFG_API(AF_ON, t32_af_api_get_statis_info(&g->af, 0, buf));
}

/* tisp_g_af_metric_attr: tisp_af_api_get_metric(ch, buf, buf + 4, buf + 8) */
bool t32_afgsm_af_get_metric(u32 ch, u8 *buf)
{
	u32 m[2];

	AFG_API(AF_ON, t32_af_api_get_metric(&g->af, 0, &m[0], &m[1], buf + 8); memcpy(buf, m, 8));
}

/* tisp_gsm_api_set (the 1052 B scratch of tisp_s_statis_config_attr: zone bounds at 1044) */
bool t32_afgsm_gsm_api_set(u32 ch, const u8 *scratch)
{
	AFG_API(GSM_ON, t32_gsm_api_set(&g->gsm, (const struct t32_gsm_api *)(const void *)scratch));
}
