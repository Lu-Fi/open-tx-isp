// SPDX-License-Identifier: GPL-2.0
/*
 * T32 defog in the driver (openimp #854): the vendor's tisp_defog_init and its
 * per-frame path, composed from the oracle-proven ports tx_isp_t32_defog.h
 * (t32_defog_oracle), tx_isp_t32_defogs.h (Tiziano_defog_soft,
 * t32_defogs_oracle) and tx_isp_t32_defogcw.h (defog_count_weight35abc,
 * t32_defogcw_oracle).
 *
 *  - Stream-on (tx_isp_t32_hw.c, the place of the vendor's lib_tisp_init call
 *    0x25448 between tisp_lsc_init and tisp_ydns_init is taken by the LSC
 *    block; here after it): the defog section of the IQ bin (section 14, 656
 *    bytes, id 0x0134da0f, the vendor's tuning base + 0x8204) is copied (the
 *    vendor edits it in place: EV word +4, size +592..+595, weight tables) and
 *    tisp_defog_init runs on it with the sensor size: the 8 KiB statistics DMA
 *    buffer (kmalloc, 0xa210..0xa21c, 0xa20c = 3), the grid (0xa040 / 0xa044 /
 *    0xa208), the weight curves (defog_count_weight35abc), para1 / para2 / the
 *    LUT (0x320a0 / 0x320a4), 0xa004 = 0x0134da0f.  Vendor tisp_defog_init
 *    0x5ef80..0x5efa8: system_irq_func_set(ch, 10, tisp_defog_interrupt_static)
 *    and tisp_event_set_cb(ch, 3, tisp_defog_par).
 *  - ISP interrupt (status word 0x30028): bit 10 runs
 *    tisp_defog_interrupt_static in the hard interrupt through the block
 *    dispatch of tx_isp_t32_blkirq.c (statistics block of the bank 0xa230
 *    names, irq count) and pushes event 3.
 *  - Event 3: a work item runs tisp_defog_par (trans-k update,
 *    Tiziano_defog_soft, the LUT write 0xa228 / 0x320a0 / 0x320a4), once per
 *    event as the vendor's event thread does (pending count, at most
 *    T32_DEFOGD_PEND_MAX queued like a bounded event queue).
 *  - AE event 6: tisp_long_ev_update -> tisp_defog_ev_update
 *    (tx_isp_t32_ctup.c, behind T32_PP_EV as the other EV users).
 *  - lib_tisp_refresh_para (bin switch while streaming): tisp_defog_dn_params_refresh
 *    on the new section (tx_isp_t32_tisp.c, t32_pf_dn_refresh).
 *  - Stream-off / next stream-on: bit 10 is dropped, the DMA registers are
 *    pointed back at the driver's static statistics area (tx_isp_t32_hw.c,
 *    openimp #850) before tisp_defog_deinit frees the buffer.
 * Behind t32_ported_paths bit 20 (T32_PP_DEFOG).  Direct register mode
 * (d03a0 = 0): tisp_reg_write is a system register write.
 */
#include "tx_isp_t32_defogcw.h"
#include "tx_isp_t32_defogs.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/seq_file.h>
#include <asm/io.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_iq.h"

int isp_printf(unsigned int level, const char *fmt, ...);
extern u8 t32_data_d03a0[4];

#define T32_DEFOGD_SEC		14	/* IQ bin section of tisp_defog_init (tuning base + 0x8204) */
#define T32_DEFOGD_IRQ		10	/* system_irq_func_set index (vendor 0x5ef84) */
#define T32_DEFOGD_MAX_WRITES	512	/* init: 12 + 68 + 44 + 184 + 1; par: 184 */
#define T32_DEFOGD_PEND_MAX	4

struct t32_defog_drv_stats t32_defog_stats;

static DEFINE_SPINLOCK(defog_lock);	/* on / pending against the interrupt */
static DEFINE_MUTEX(defog_mutex);	/* the state: par, ev_update, dn refresh, attach / detach */
static bool defog_on;
static u32 defog_pend;
static struct t32_defog_chan defog_c;
static u8 *defog_sec;			/* the copy of the defog section (vendor tparamsP + 0x8204) */
static struct t32_front_write defog_w[T32_DEFOGD_MAX_WRITES];

static void *drv_vm(u32 size) { return vmalloc(size); }
static void *drv_km(u32 size, u32 gfp) { (void)gfp; return kmalloc(size, GFP_KERNEL); }
static void drv_vf(void *p) { vfree(p); }
static void drv_kf(void *p) { kfree(p); }
static u32 drv_rd(u32 reg) { return t32_hw_rd(reg); }

/* private_dma_cache_sync of the statistics block the engine wrote */
static void drv_sync(const u8 *buf, u32 len)
{
#ifdef CONFIG_MIPS
	dma_cache_wback_inv((unsigned long)buf, len);
#else
	(void)buf; (void)len;
#endif
}

static const struct t32_defog_cw_ops cw_ops = { drv_vm, drv_vf };

/* defog_count_weight35abc (the vendor scratch t32_data_e20e0 / e211c = the channel's vbuf / vsize) */
static void drv_cw(u32 ch, u32 h, u32 w, u32 gain, u8 *p80, u8 *p128, u8 *p430, u8 *p462, u8 *p494,
		   u8 *p526, u8 *p558)
{
	u8 *const out[5] = { p430, p462, p494, p526, p558 };

	(void)ch;
	if (t32_defog_count_weight35abc(0, h, w, gain, (u16 *)p80, (u16 *)p128, out, &defog_c.vbuf,
					&defog_c.vsize, &cw_ops))
		t32_defog_stats.cw_fail++;
}

static void drv_soft(struct t32_defog_par *par) { t32_defog_soft(par); }

static void defog_work_fn(struct work_struct *w);
static DECLARE_WORK(defog_work, defog_work_fn);

/* event 3 of tisp_defog_interrupt_static (hard interrupt, defog_lock held) */
static void drv_push(u32 ch, const u32 *ev)
{
	(void)ch; (void)ev;
	if (defog_pend < T32_DEFOGD_PEND_MAX)
		defog_pend++;
	else
		t32_defog_stats.ev_full++;
	schedule_work(&defog_work);
}

static void drv_irq_set(u32 ch, u32 id) { (void)ch; (void)id; }	/* bit 10: t32_blk_defog_set below */
static void drv_ev_cb(u32 ch, u32 ev) { (void)ch; (void)ev; }	/* event 3: defog_work */

static const struct t32_defog_ops drv_ops = {
	.count_weight = drv_cw,
	.soft = drv_soft,
	.reg_read = drv_rd,
	.dma_sync = drv_sync,
	.event_push = drv_push,
	.vmalloc = drv_vm,
	.kmalloc = drv_km,
	.vfree = drv_vf,
	.kfree = drv_kf,
	.irq_set = drv_irq_set,
	.event_set_cb = drv_ev_cb,
};

static void defog_flush(unsigned int n)
{
	unsigned int i;

	if (n > T32_DEFOGD_MAX_WRITES) {
		isp_printf(2, "tx-isp-t32: defog write list overflow (%u)\n", n);
		return;
	}
	for (i = 0; i < n; i++)
		t32_hw_wr(defog_w[i].reg, defog_w[i].val);
	t32_defog_stats.writes += n;
}

/* bit 10 in the block dispatch: tisp_defog_interrupt_static */
static u32 drv_irq(u32 ch)
{
	u32 r = 1;

	spin_lock(&defog_lock);
	t32_defog_stats.irqs++;
	if (defog_on && t32_pp_on(T32_PP_DEFOG))
		r = (u32)t32_defog_interrupt_static(&defog_c, ch);
	spin_unlock(&defog_lock);
	return r;
}

/* event 3: tisp_defog_par, once per queued event */
static void defog_work_fn(struct work_struct *w)
{
	unsigned long f;

	(void)w;
	mutex_lock(&defog_mutex);
	for (;;) {
		unsigned int n = 0;

		spin_lock_irqsave(&defog_lock, f);
		if (!defog_on || !defog_pend) {
			defog_pend = 0;
			spin_unlock_irqrestore(&defog_lock, f);
			break;
		}
		defog_pend--;
		spin_unlock_irqrestore(&defog_lock, f);
		if (!t32_pp_on(T32_PP_DEFOG))
			continue;
		t32_defog_par(&defog_c, 0, defog_w, &n);
		defog_flush(n);
		t32_defog_stats.pars++;
	}
	mutex_unlock(&defog_mutex);
}

void t32_defog_detach(void)
{
	unsigned long f;
	bool was;

	t32_blk_defog_set(NULL);
	spin_lock_irqsave(&defog_lock, f);
	was = defog_on;
	defog_on = false;
	defog_pend = 0;
	spin_unlock_irqrestore(&defog_lock, f);
	cancel_work_sync(&defog_work);
	mutex_lock(&defog_mutex);
	if (was || defog_c.ctx || defog_c.vbuf) {
		t32_defog_park();
		t32_defog_deinit(&defog_c);
	}
	kfree(defog_sec);
	defog_sec = NULL;
	mutex_unlock(&defog_mutex);
}

bool t32_defog_attached(void)
{
	return READ_ONCE(defog_on);
}

/* stream-on: tisp_defog_init (ch 0) on the IQ bin's defog section */
int t32_defog_attach(const u8 *bin, u32 bytes, u32 width, u32 height)
{
	u32 off, len = 0, size[2] = { width, height };
	unsigned int n = 0;
	unsigned long f;
	int r;

	t32_defog_detach();
	if (!t32_pp_on(T32_PP_DEFOG))
		return -ENODEV;
	off = t32_iq_section(bin, bytes, T32_DEFOGD_SEC, &len);
	if (!off || len < T32_DEFOG_SEC_BYTES || t32_front_le32(bin + off) != T32_DEFOG_ID)
		return -ENOENT;
	defog_sec = kmemdup(bin + off, T32_DEFOG_SEC_BYTES, GFP_KERNEL);
	if (!defog_sec) {
		t32_defog_detach();
		return -ENOMEM;
	}
	mutex_lock(&defog_mutex);
	memset(&defog_c, 0, sizeof(defog_c));
	defog_c.ops = &drv_ops;
	defog_c.shadow = t32_data_d03a0[0] == 1;
	/* the port addresses the section as tuning base + 0x8204 (vendor x->sec) */
	r = t32_defog_init(&defog_c, 0, size, defog_sec - T32_DEFOG_SEC_OFF, defog_w, &n);
	if (r) {
		mutex_unlock(&defog_mutex);
		isp_printf(1, "tx-isp-t32: defog init without its buffers\n");
		t32_defog_detach();
		return -ENOMEM;
	}
	defog_flush(n);
	spin_lock_irqsave(&defog_lock, f);
	defog_on = true;
	defog_pend = 0;
	spin_unlock_irqrestore(&defog_lock, f);
	mutex_unlock(&defog_mutex);
	t32_blk_defog_set(drv_irq);
	t32_defog_stats.attaches++;
	t32_defog_stats.init_writes = n;
	return 0;
}

/* tisp_long_ev_update -> tisp_defog_ev_update (ch, ev lo, ev hi, x, y) */
void t32_defog_ev(u32 lo, u32 hi, u32 x, u32 y)
{
	mutex_lock(&defog_mutex);
	if (READ_ONCE(defog_on) && t32_pp_on(T32_PP_DEFOG)) {
		t32_defog_ev_update(&defog_c, 0, lo, hi, x, y);
		t32_defog_stats.evs++;
	}
	mutex_unlock(&defog_mutex);
}

/* lib_tisp_refresh_para: the new bin's section into the copy, then tisp_defog_dn_params_refresh (ch 0, value) */
int t32_defog_dn_refresh(const u8 *bin, u32 bytes, u32 value)
{
	u32 off, len = 0;
	unsigned int n = 0;
	int done = 0;

	off = t32_iq_section(bin, bytes, T32_DEFOGD_SEC, &len);
	if (!off || len < T32_DEFOG_SEC_BYTES || t32_front_le32(bin + off) != T32_DEFOG_ID)
		return 0;
	mutex_lock(&defog_mutex);
	if (READ_ONCE(defog_on) && defog_sec && defog_c.ctx) {
		memcpy(defog_sec, bin + off, T32_DEFOG_SEC_BYTES);
		t32_defog_dn_params_refresh(&defog_c, 0, value, defog_w, &n);
		defog_flush(n);
		t32_defog_stats.dns++;
		done = 1;
	}
	mutex_unlock(&defog_mutex);
	return done;
}

void t32_defog_show(struct seq_file *m)
{
	const struct t32_defog_ctx *x;

	mutex_lock(&defog_mutex);
	x = defog_c.ctx;
	seq_printf(m, "defog: %s, attaches %u, init writes %u, irqs %u (bank irqs %u), pars %u (queue full %u), "
		   "ev updates %u, dn refreshes %u, writes %u, weight fails %u, irq_cnt %u ev %#x\n",
		   READ_ONCE(defog_on) ? "on" : "off", t32_defog_stats.attaches, t32_defog_stats.init_writes,
		   t32_defog_stats.irqs, defog_c.irq_total, t32_defog_stats.pars, t32_defog_stats.ev_full,
		   t32_defog_stats.evs, t32_defog_stats.dns, t32_defog_stats.writes, t32_defog_stats.cw_fail,
		   x ? x->irq_cnt : 0, x ? x->ev : 0);
	mutex_unlock(&defog_mutex);
}
