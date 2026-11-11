// SPDX-License-Identifier: GPL-2.0
/*
 * T32 TMO / ADR in the driver (openimp #234): the vendor's tisp_tmo_init and
 * its two block interrupts, composed from the oracle-proven ports
 * (tx_isp_t32_tmoblk.h, oracle t32_tmoblk_oracle).
 *
 *  - Stream-on (tx_isp_t32_hw.c, after the LCE block): the TMO section of the
 *    IQ bin (section 4, 11000 bytes, the vendor's tuning base + 11976) is
 *    copied (the vendor edits it in place) and tisp_tmo_init runs on it with
 *    the sensor size and the first gamma record (adr_gamma_refresh): the
 *    statistics DMA buffer (kmalloc 0x10000, registers 0x7100 ..), the
 *    channel record and its buffers, the default register refreshes, the
 *    ramY / ramV tables, then interrupt bits 9 and 31.
 *  - ISP interrupt (status word 0x30028): bits 9 / 31 run in the hard
 *    interrupt through the ported lib_tisp_intp_process of
 *    tx_isp_t32_blkirq.c, as in the vendor: bit 9 tisp_tmo_interrupt_static
 *    (statistics, tmo2fbf or the ADR soft process), bit 31
 *    tisp_tmo_fbf_interrupt_calc (tmo_soft_process: stat_limit, ramY / ramV).
 *  - Per frame: tisp_tmo_tgain_update from tisp_long_tgain_update
 *    (tx_isp_t32_tisp.c).  tisp_tmo_ev_update (tisp_long_ev_update) is not
 *    called yet: the driver does not run the vendor's long ev update.
 *  - Stream-off / next stream-on: the bits are dropped, tisp_tmo_deinit frees
 *    the buffers.  The y filter table d88d4 keeps its value (module .data).
 * Behind t32_ported_paths bit 13 (T32_PP_TMO).  Direct register mode
 * (d03a0 = 0): tisp_reg_write and tisp_reg_lut_write are system writes.
 */
#include "tx_isp_t32_tmoblk.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <asm/io.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_iq.h"

int isp_printf(unsigned int level, const char *fmt, ...);

struct t32_tmo_drv_stats t32_tmo_stats;

static DEFINE_SPINLOCK(tmo_lock);	/* the module state: interrupt bits against tgain_update / detach */
static bool tmo_on;
static u8 *tmo_ctx;			/* the copy of the TMO section */
static u8 *tmo_gamma;			/* the first gamma record (258 bytes) */

static void *drv_vm(u32 size) { return vmalloc(size); }
static void *drv_km(u32 size, u32 flags) { (void)flags; return kmalloc(size, GFP_KERNEL); }
static void drv_vf(void *p) { vfree(p); }
static void drv_kf(void *p) { kfree(p); }
static u32 drv_rd(u32 reg) { return t32_hw_rd(reg); }
static void drv_sysw(u32 reg, u32 val) { t32_hw_wr(reg, val); }
static void drv_regw(u32 ch, u32 reg, u32 val) { (void)ch; t32_hw_wr(reg, val); }
static void drv_lutw(u32 ch, u32 reg, u32 val, u32 idx) { (void)ch; (void)idx; t32_hw_wr(reg, val); }

/* private_dma_cache_sync (0, p, len, DMA_BIDIRECTIONAL): write back and invalidate */
static void drv_sync(void *p, u32 len)
{
#ifdef CONFIG_MIPS
	dma_cache_wback_inv((unsigned long)p, len);
#else
	(void)p; (void)len;
#endif
}

/* the handlers in the block dispatch (hard interrupt, tx_isp_t32_blkirq.c) */
static u32 drv_irq_tmo(u32 ch)
{
	u32 r;

	spin_lock(&tmo_lock);
	t32_tmo_stats.tmo_irqs++;
	r = tmo_on ? t32_tmob_irq_tmo(ch) : 1;
	spin_unlock(&tmo_lock);
	return r;
}

static u32 drv_irq_fbf(u32 ch)
{
	u32 r;

	spin_lock(&tmo_lock);
	t32_tmo_stats.fbf_irqs++;
	r = tmo_on ? t32_tmob_irq_fbf(ch) : 1;
	spin_unlock(&tmo_lock);
	return r;
}

static t32_intp_fn drv_bits[2];

/* system_irq_func_set: our locked wrappers of the two handlers, set into the block table at the end of the init */
static void drv_irq_set(u32 ch, u32 bit, t32_intp_fn fn)
{
	(void)ch;
	if (bit == T32_TMOB_IRQ_TMO && fn == t32_tmob_irq_tmo)
		drv_bits[0] = drv_irq_tmo;
	else if (bit == T32_TMOB_IRQ_FBF && fn == t32_tmob_irq_fbf)
		drv_bits[1] = drv_irq_fbf;
}

static const struct t32_tmob_ops drv_ops = {
	drv_vm, drv_km, drv_vf, drv_kf, drv_rd, drv_sysw, drv_regw, drv_lutw, drv_sync, drv_irq_set,
};

/* stream-off / before the next init: drop the bits, tisp_tmo_deinit; process context */
void t32_tmo_detach(void)
{
	unsigned long f;
	bool was;

	t32_blk_tmo_set(NULL, NULL);
	spin_lock_irqsave(&tmo_lock, f);
	was = tmo_on;
	tmo_on = false;
	spin_unlock_irqrestore(&tmo_lock, f);
	if (was)
		t32_tmob_deinit(0);	/* vfree / kfree: outside the lock, the handlers are gone */
	kfree(tmo_ctx);
	kfree(tmo_gamma);
	tmo_ctx = tmo_gamma = NULL;
}

/* stream-on: tisp_tmo_init (ch 0) on the IQ bin's TMO section */
void t32_tmo_attach(const u8 *bin, u32 bytes, u32 width, u32 height)
{
	u32 off, len = 0, goff, glen = 0, info[10];
	unsigned long f;

	t32_tmo_detach();
	if (!t32_pp_on(T32_PP_TMO))
		return;
	off = t32_iq_section(bin, bytes, T32_TMOB_SEC, &len);
	if (!off || len < T32_TMOB_SEC_BYTES)
		return;
	goff = t32_iq_section(bin, bytes, 13, &glen);
	tmo_ctx = kmemdup(bin + off, T32_TMOB_SEC_BYTES, GFP_KERNEL);
	tmo_gamma = kzalloc(T32_TMOB_GAMMA_BYTES, GFP_KERNEL);
	if (!tmo_ctx || !tmo_gamma) {
		t32_tmo_detach();
		return;
	}
	if (goff && goff + glen + T32_TMOB_GAMMA_BYTES <= bytes)
		memcpy(tmo_gamma, bin + goff + glen, T32_TMOB_GAMMA_BYTES);	/* dc274[0] + 32: the gamma payload */
	memset(info, 0, sizeof(info));
	info[0] = width;
	info[1] = height;		/* byte 36 (WDR) 0: linear */
	if (!t32_tmob.ops) {
		t32_tmob_load(&t32_tmob);	/* the .data value of d88d4, once (module lifetime) */
		t32_tmob.ops = &drv_ops;
		t32_tmob.d03a0 = 0;
	}
	drv_bits[0] = drv_bits[1] = NULL;
	t32_tmob_init(0, info, tmo_ctx, tmo_gamma);
	if (!t32_tmob.e35e0[0] || !t32_tmob.e35b0[0] || !((u32 *)(uintptr_t)t32_tmob.e35d0[0])[1]) {
		isp_printf(1, "tx-isp-t32: TMO init without its buffers\n");
		tmo_on = true;		/* deinit frees what was allocated */
		t32_tmo_detach();
		return;
	}
	spin_lock_irqsave(&tmo_lock, f);
	tmo_on = true;
	spin_unlock_irqrestore(&tmo_lock, f);
	t32_blk_tmo_set(drv_bits[0], drv_bits[1]);
	t32_tmo_stats.attaches++;
}

/* tisp_long_tgain_update -> tisp_tmo_tgain_update (ch, gain): low / high word */
void t32_tmo_tgain(u32 ch, u64 gain)
{
	unsigned long f;

	if (ch)
		return;
	spin_lock_irqsave(&tmo_lock, f);
	if (tmo_on && t32_pp_on(T32_PP_TMO)) {
		t32_tmob_tgain_update(0, (u32)gain, (u32)(gain >> 32));
		t32_tmo_stats.tgains++;
	}
	spin_unlock_irqrestore(&tmo_lock, f);
}
