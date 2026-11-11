// SPDX-License-Identifier: GPL-2.0
/*
 * T32 block interrupts in the driver (openimp #234): the vendor's LCE
 * per-frame path, which runs from ISP core interrupt bits and an event, not
 * from the AE process (tx_isp_t32_blkirq.h, oracle t32_blkirq_oracle).
 *
 *  - Stream-on: tx_isp_t32_hw.c allocates the three LCE DMA buffers of
 *    tisp_lce_malloc_cfg (statistics banks 0x26400, curve 0x5a00, histogram
 *    0x9000) and programs them (t32_lce_init); tx_isp_t32_tisp.c builds the
 *    LCE state (t32_lce_state_init) and hands it here (t32_blk_lce_attach):
 *    buffer set, band table, interrupt bits 8 / 30 and event 14 as the
 *    vendor's tisp_lce_init registers them.
 *  - ISP interrupt (status word 0x30028): the registered bits run in the hard
 *    interrupt through the ported lib_tisp_intp_process, as in the vendor:
 *    bit 8 tisp_lce_interrupt_static (zone sums, statistics bank, curve
 *    buffer), bit 30 tisp_lsf_interrupt_static (histogram, event 14).
 *  - Event 14 goes through the ported tisp event queue (tx_isp_t32_event.h)
 *    to a work item that runs tisp_lce_par (the vendor's event thread):
 *    Tisp_lce_soft_t41_simd / _simd_v4 and tisp_lce_write_curve; the next
 *    bit 8 hands the new curve to the hardware.
 * Behind t32_ported_paths bit 12 (T32_PP_LCE).  Direct register mode.
 * The TMO bits 9 / 31 (tx_isp_t32_tmo.c, behind bit 13) share the table and
 * the dispatch (t32_blk_tmo_set), so all block bits run in the vendor's order.
 */
#include "tx_isp_t32_blkirq.h"	/* first: blkrest.h has a member named wake_up */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <asm/io.h>

#include "tx_isp_t32.h"

int isp_printf(unsigned int level, const char *fmt, ...);
extern u8 t32_data_d03a0[4];

struct t32_blk_drv_stats t32_blk_stats;

static DEFINE_SPINLOCK(blk_lock);	/* dispatch against attach / detach */
static DEFINE_SPINLOCK(blk_par_lock);	/* the lock of tisp_lce_par */
static DEFINE_SPINLOCK(blk_ev_lock);	/* the event queue lock (vendor t32_small_bss) */
static bool blk_lce_on, blk_tmo_on;
static t32_intp_fn blk_irq[T32_INTP_BITS];	/* vendor t32_data_e3b50 (system_irq_func_set) */
static struct t32_intp_glob blk_g;
static struct t32_event_q *blk_q;
static struct completion blk_done[T32_EV_CHANS];
static void *blk_buf[3];			/* curve, statistics, histogram (cached kernel addresses) */

static u32 drv_rd(u32 reg) { return t32_hw_rd(reg); }
static void drv_wr(u32 ch, u32 reg, u32 val) { (void)ch; t32_hw_wr(reg, val); }

/* private_dma_cache_sync (0, p, len, DMA_BIDIRECTIONAL): write back and invalidate */
static void drv_sync(void *p, u32 len)
{
#ifdef CONFIG_MIPS
	dma_cache_wback_inv((unsigned long)p, len);
#else
	(void)p; (void)len;
#endif
}

static u32 drv_lock(void)
{
	unsigned long f;

	spin_lock_irqsave(&blk_par_lock, f);
	return (u32)f;
}

static void drv_unlock(u32 f) { spin_unlock_irqrestore(&blk_par_lock, (unsigned long)f); }

static void blk_ev_work_fn(struct work_struct *w);
static DECLARE_WORK(blk_ev_work, blk_ev_work_fn);

static void drv_push(u32 ch, const struct t32_event *ev)
{
	if (t32_event_push(blk_q, ch, ev))
		t32_blk_stats.ev_full++;
	else
		schedule_work(&blk_ev_work);
}

static const struct t32_blk_ops drv_ops = { drv_rd, drv_wr, drv_sync, drv_lock, drv_unlock, drv_push };

/* ---- the event queue's OS calls ---- */
static void ev_lock_init(void) { }
static void ev_lock(unsigned long *f) { spin_lock_irqsave(&blk_ev_lock, *f); }
static void ev_unlock(unsigned long f) { spin_unlock_irqrestore(&blk_ev_lock, f); }
static void ev_comp_init(u32 ch) { init_completion(&blk_done[ch]); }
static void ev_complete(u32 ch) { complete(&blk_done[ch]); }
static long ev_wait(u32 ch, u32 t) { return (long)wait_for_completion_timeout(&blk_done[ch], t); }
static void ev_restart(void) { isp_printf(2, "tx-isp-t32: block event wait interrupted\n"); }

static const struct t32_event_ops ev_ops = {
	ev_lock_init, ev_lock, ev_unlock, ev_comp_init, ev_complete, ev_wait, ev_restart,
};

/* the vendor's event thread for the block events: drain channel 0 */
static void blk_ev_work_fn(struct work_struct *w)
{
	(void)w;
	while (READ_ONCE(blk_lce_on) && !t32_event_work_empty(blk_q, 0)) {
		t32_event_process(blk_q, 0);
		t32_blk_stats.events++;
	}
}

/* status word 0x30028 of the ISP interrupt: the registered block bits, in the hard interrupt */
void t32_blk_irq(u32 sta)
{
	unsigned long f;
	u32 on = 0;

	if (!(sta & T32_BLK_IRQ_MASK))
		return;
	spin_lock_irqsave(&blk_lock, f);
	if (blk_lce_on && t32_pp_on(T32_PP_LCE)) {
		on |= T32_BLK_IRQ_LCE_BITS;
		if (sta & (1U << T32_BLK_IRQ_LCE))
			t32_blk_stats.lce_irqs++;
		if (sta & (1U << T32_BLK_IRQ_LSF))
			t32_blk_stats.lsf_irqs++;
	}
	if (blk_tmo_on && t32_pp_on(T32_PP_TMO))
		on |= T32_BLK_IRQ_TMO_BITS;
	if (sta & on)
		t32_blk_dispatch(&blk_g, blk_irq, sta & on, 0);
	spin_unlock_irqrestore(&blk_lock, f);
}

/* tx_isp_t32_hw.c: the three LCE DMA buffers (kept until module exit) */
void t32_blk_lce_bufs(void *curve, void *stat, void *hist)
{
	blk_buf[0] = curve;
	blk_buf[1] = stat;
	blk_buf[2] = hist;
}

/* stop the LCE block paths (state about to be freed); process context */
void t32_blk_lce_detach(void)
{
	unsigned long f;

	spin_lock_irqsave(&blk_lock, f);
	blk_lce_on = false;
	blk_irq[T32_BLK_IRQ_LCE] = NULL;
	blk_irq[T32_BLK_IRQ_LSF] = NULL;
	spin_unlock_irqrestore(&blk_lock, f);
	cancel_work_sync(&blk_ev_work);
	if (blk_q)
		t32_event_init(blk_q, 0);	/* drop events of the old state */
}

/* the LCE part of tisp_lce_init on the state st (vmalloc'd by tx_isp_t32_tisp.c) */
void t32_blk_lce_attach(u8 *st, u32 width, u32 height)
{
	u32 va[3], pa[3];
	unsigned long f;
	int i;

	t32_blk_lce_detach();
	if (!blk_buf[0] || !blk_buf[1] || !blk_buf[2])
		return;
	if (!blk_q) {
		blk_q = kzalloc(sizeof(*blk_q), GFP_KERNEL);
		if (!blk_q)
			return;
		blk_q->ops = &ev_ops;
		t32_event_init(blk_q, 0);
	}
	for (i = 0; i < 3; i++) {
		va[i] = (u32)(uintptr_t)blk_buf[i];
		pa[i] = (u32)virt_to_phys(blk_buf[i]);
	}
	spin_lock_irqsave(&blk_lock, f);
	t32_blk.ops = &drv_ops;
	t32_blk.shadow = t32_data_d03a0;
	t32_blk_lce_init(0, st, width, height, va, pa, blk_irq, blk_q->handler);
	blk_lce_on = true;
	spin_unlock_irqrestore(&blk_lock, f);
	t32_blk_stats.attaches++;
}

/* tx_isp_t32_tmo.c: the TMO handlers of bits 9 / 31 (system_irq_func_set of tisp_tmo_init), NULL = off */
void t32_blk_tmo_set(u32 (*tmo)(u32 ch), u32 (*fbf)(u32 ch))
{
	unsigned long f;

	spin_lock_irqsave(&blk_lock, f);
	blk_irq[T32_BLK_IRQ_TMO] = tmo;
	blk_irq[T32_BLK_IRQ_FBF] = fbf;
	blk_tmo_on = tmo || fbf;
	spin_unlock_irqrestore(&blk_lock, f);
}

void t32_blk_exit(void)
{
	t32_blk_lce_detach();
	kfree(blk_q);
	blk_q = NULL;
}
