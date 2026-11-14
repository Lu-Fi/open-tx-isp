// SPDX-License-Identifier: GPL-2.0
/*
 * T32 ISP hardware access (S2/S3): VIC + MIPI CSI start/stop from the sensor
 * attributes, the stream-on register list, the interrupt block, MSCA
 * channel programming (three channels) and the frame-done path.
 *
 * Register values and their order follow the vendor stack's QEMU trace
 * (S0 runs r10/r11, audit/T32_S0_1025.md): VIC input configuration at
 * S_INPUT, IRQ enables + VIC run + CSI/D-PHY at STREAMON, MSCA channel setup
 * and buffer pushes at FRAME_STREAMON/QBUF, done-FIFO drain in the ISP
 * interrupt.  The VIC/CSI words are derived from the sensor attributes
 * (tx_isp_t32_vic.h, checked against the cv8001 trace).  No tisp / no image
 * pipeline programming yet (S4).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_regs.h"
#include "tx_isp_t32_soc.h"
#include "tx_isp_t32_msca.h"
#include "tx_isp_t32_msca_curve.h"
#include "tx_isp_t32_vic.h"
#include "tx_isp_t32_rcfg.h"
#include "tx_isp_t32_rcfg33.h"
#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_dmsc.h"
#include "tx_isp_t33_dpc.h"
#include "tx_isp_t33_ysp.h"
#include "tx_isp_t33_rdns.h"
#include "tx_isp_t33_ccm.h"
#include "tx_isp_t33_lsc.h"
#include "tx_isp_t33_gamma.h"
#include "tx_isp_t33_defog.h"
#include "tx_isp_t33_lce.h"
#include "tx_isp_t33_lsf.h"
#ifdef TX_ISP_T33
static void t33_lcepf_lock_irq(void);
static void t33_lcepf_unlock_irq(void);
static void t33_lcepf_wbinv(unsigned long pages, u32 len);
static void t33_lcepf_free(void);
extern u32 t33_lcepf_irq8s, t33_lcepf_irq30s, t33_lcepf_curves, t33_lcepf_lce_runs, t33_lcepf_lsf_runs, t33_lcepf_evs;
#define T33_LCEPF_LOCK(c)	t33_lcepf_lock_irq()
#define T33_LCEPF_UNLOCK(c)	t33_lcepf_unlock_irq()
#include "tx_isp_t33_lceev.h"	/* tx_isp_t33_lcepf.h + the EV update (openimp #500) */
static void t33_lcepf_ev(u32 lo, u32 hi, u32 bv);
static u32 t33_top_byp;		/* the top list's bypass word 0x40 (tisp_top_get_bypass) */
#endif
#include "tx_isp_t33_af.h"
#include "tx_isp_t33_bcsh.h"
#include "tx_isp_t33_tmo.h"
#include "tx_isp_t33_clm.h"
#include "tx_isp_t33_msca.h"
#include "tx_isp_t33_msca_curve.h"
#include "tx_isp_t33_mdns.h"
#include "tx_isp_t33_ae.h"
#include "tx_isp_t33_awb.h"
#include "tx_isp_t33_stats.h"
#include <linux/seq_file.h>
#include "tx_isp_t32_lsc.h"
#include "tx_isp_t32_stats.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_front.h"
#include "tx_isp_t32_gamma.h"
#include "tx_isp_t32_cdns.h"
#include "tx_isp_t32_dmsc.h"
#include "tx_isp_t32_ysp.h"
#include "tx_isp_t32_ydns.h"
#include "tx_isp_t32_sdns.h"
#include "tx_isp_t32_clm.h"
#include "tx_isp_t32_mdns.h"
#include "tx_isp_t32_dpc.h"
#include "tx_isp_t32_gib.h"
#include "tx_isp_t32_ccm.h"
#include "tx_isp_t32_bcsh.h"
#include "tx_isp_t32_lce.h"
#include "tx_isp_t32_rdns.h"
#include "tx_isp_t32_rdnstab.h"
#ifdef TX_ISP_T33
#include "tx_isp_t33_ldc.h"
#endif

static void t32_bringup_ae_kick(void);
#ifdef TX_ISP_T33
static struct work_struct t33_stats_w;
#endif

/* Linux IRQ numbers: INTC source + XBURST_SOC_IRQ_BASE (8) on PRJ007 */
static int isp_irq = 8 + T32_IRQ_ISP_M0;
module_param(isp_irq, int, 0444);
MODULE_PARM_DESC(isp_irq, "Linux IRQ of the ISP core (INTC 29)");
static int vic_irq = 8 + T32_IRQ_VIC;
module_param(vic_irq, int, 0444);
MODULE_PARM_DESC(vic_irq, "Linux IRQ of the VIC (INTC 30)");

static void __iomem *isp_base;
static void __iomem *csi_base;
static void __iomem *dphy_base;
static int irqs_requested;

struct t32_hw_stats t32_hw_stats;

static inline u32 isp_rd(u32 off)
{
	return readl(isp_base + off);
}

static inline void isp_wr(u32 off, u32 v)
{
	writel(v, isp_base + off);
}

u32 t32_hw_rd(u32 off)
{
	return isp_rd(off);
}

void t32_hw_wr(u32 off, u32 v)
{
	isp_wr(off, v);
}

static int t32_wait_clear(u32 off, u32 mask, int us)
{
	while (us-- > 0) {
		if (!(isp_rd(off) & mask))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

/* ------------------------------------------------------------------ */
/* interrupts                                                         */

#ifdef TX_ISP_T33
static bool t33_top_sw;		/* list 33 rebuilt by a bin switch (tispTopRefresh, #504) */
static u32 t33_mod_dirty;	/* lists 0..31 to run again at the next frame end (tisp*Refresh) */
static bool t33_awb_refresh;
static bool t33_first_fe;	/* the TMO / MDNS start words are still 1 */
static void t33_first_frame_words(void);
static u32 t33_list_word(const u32 *l, u32 reg);
/* lists 2..4, 6..9, 12..15, 17..22 */
#define T33_INIT_REFRESH	0x007ef3dcU
#define T33_TMO_START_REG	0x70f0U
#define T33_MDNS_START_REG	0xd018U	/* tispAwbRefresh == 1: run list 29 at the next AWB interrupt */
extern u32 t33_rcfg_runs;
#define T33_INT_LCE	(1U << 8)
#define T33_INT_LSF	(1U << 30)
static void t33_lcepf_isr8(void);
static void t33_lcepf_isr30(void);
#endif

static irqreturn_t t32_isp_irq(int irq, void *dev)
{
	u32 sta, done, msca, s3, s4, s5 = 0;

	sta = isp_rd(T32_INT_STA);
	done = isp_rd(T32_CHDONE_STA);
	msca = isp_rd(T32_MSCADONE_STA);
	s3 = isp_rd(T32_INT3_STA);
	s4 = isp_rd(T32_INT4_STA);
	if (T32_SOC_LIST_IRQ)
		s5 = isp_rd(T32_INT5_STA);
	if (!(sta | done | msca | s3 | s4 | s5))
		return IRQ_NONE;
	isp_wr(T32_INT_CLR, sta);
	isp_wr(T32_CHDONE_CLR, done);
	isp_wr(T32_MSCADONE_CLR, msca);
	isp_wr(T32_INT3_CLR, s3);
	if (T32_SOC_LIST_IRQ)
		isp_wr(T32_INT5_CLR, s5);
	isp_wr(T32_INT4_CLR, s4);
	isp_wr(T32_RDMA_IRQ_CLR, 0);
	t32_hw_stats.isp_irqs++;
	if (sta & 1) {
		t32_hw_stats.frame_ends++;
#ifdef TX_ISP_T33
		/*
		 * First frame end after stream-on: tisp_tmo_t33_frame_done_interrupt
		 * (TMO start word 0x70f0 = 0, tispTmoRefresh) and
		 * tisp_mdns_t33_frame_done_interrupt (mdnsInit: 0xd018 = 0).
		 */
		if (t33_first_fe && !isp_rd(T33_RCFG_TRIG)) {
			t33_first_fe = false;
			t33_first_frame_words();
			if (t32_ldc_top_refresh() && !isp_rd(T33_RCFG_TRIG_HI))
				isp_wr(T33_RCFG_TRIG_HI, 1U << (T33_MOD_TOP - 32));	/* tispTopRefresh */
		}
		if (t33_top_sw && !isp_rd(T33_RCFG_TRIG_HI)) {	/* tispTopRefresh after a bin switch (#504) */
			t33_top_sw = false;
			isp_wr(T33_RCFG_TRIG_HI, 1U << (T33_MOD_TOP - 32));
			t33_rcfg_runs++;
		}
		/* the vendor's frame-done list refresh: modules marked since, when no list runs */
		if (t33_mod_dirty && !isp_rd(T33_RCFG_TRIG)) {
			isp_wr(T33_RCFG_TRIG, t33_mod_dirty);
			t33_mod_dirty = 0;
			t33_rcfg_runs++;
		}
		t32_tunctl_frame_done(0);	/* isp_frame_done_wakeup (tuning id 0x08000091) */
#else
		t32_tunctl_frame_done(0);	/* isp_frame_done_wakeup (tuning id 0x08000091) */
#endif
	}
	if (sta & T32_INT_AWB) {
		t32_hw_stats.awb_irqs++;
#ifdef TX_ISP_T33
		t33_awbc_kick();
		/* tisp_awb_interrupt_static: tispAwbRefresh == 1 -> list 29 (no DMA state check) */
		if (t33_awb_refresh) {
			t33_awb_refresh = false;
			isp_wr(T33_RCFG_TRIG, 1U << T33_MOD_AWB);
			t33_rcfg_runs++;
		}
#else
		t32_awbc_kick();	/* the vendor AWB frame (system irq 3: tisp_awb_interrupt_static) */
#endif
	}
	if (sta & T32_INT_AE) {
		t32_hw_stats.ae_irqs++;
#ifdef TX_ISP_T33
		t33_aec_irq4(isp_rd(T33_AE_IDX_REG));	/* tisp_ae_interrupt_static: AE frame */
#else
		t32_aec_kick();		/* the vendor AE frame (system irq 4: tisp_ae_interrupt_static) */
#endif
	}
	if (sta & T32_INT_AE_HIST) {
		t32_hw_stats.ae_hist_irqs++;
#ifdef TX_ISP_T33
		t33_aec_irq5(isp_rd(T33_AE_IDX_REG));	/* tisp_ae_interrupt_hist: zone buffer index */
#endif
		t32_bringup_ae_kick();
		t32_pf_kick();
	}
#ifdef TX_ISP_T33
	if (sta & (T32_INT_AWB | T32_INT_AE | T32_INT_AE_HIST))
		schedule_work(&t33_stats_w);
	if (sta & T33_INT_LCE)		/* system irq 8: tisp_lce_interrupt_static */
		t33_lcepf_isr8();
	if (sta & T33_INT_LSF)		/* system irq 30: tisp_lsf_interrupt_static */
		t33_lcepf_isr30();
#else
	t32_blk_irq(sta);		/* block bits 8 / 30 (LCE): lib_tisp_intp_process, tx_isp_t32_blkirq.c */
	t32_afgsm_irq(sta, msca);	/* AF bit 6, GSM bits 7 / 63 (tx_isp_t32_afgsm.c) */
#endif
	if (done)
		t32_fs_done_irq(done);
#ifdef TX_ISP_T33
	if ((msca & 1) || (s3 & 0x10))	/* bits 32 / 68: RAW snapshot WDMA (openimp #491) */
		t32_snap_isp_irq(msca, s3);
#endif
	if (msca & 2)			/* MSCA frame done: tisp_msca_irq_frame_done (ISP OSD, #336) */
		t32_isposd_frame_done();
	if (msca & T32_MSCADONE_LSC)	/* interrupt bit 36: tisp_lsc_irq_frame_done (tx_isp_t32_lscct.c) */
		t32_lsc_drv_frame_done();
	return IRQ_HANDLED;
}

static irqreturn_t t32_vic_irq(int irq, void *dev)
{
	u32 s0 = readl(isp_base + T32_VIC_BASE(0) + T32_VIC_INT_STA);
	u32 s1 = readl(isp_base + T32_VIC_BASE(0) + T32_VIC_INT_STA2);
	u32 t0 = readl(isp_base + T32_VIC_BASE(1) + T32_VIC_INT_STA);
	u32 t1 = readl(isp_base + T32_VIC_BASE(1) + T32_VIC_INT_STA2);

	if (!(s0 | s1 | t0 | t1))
		return IRQ_NONE;
	/* both VICs share the line; the vendor clears both each time */
	writel(s0, isp_base + T32_VIC_BASE(0) + T32_VIC_INT_CLR);
	writel(s1, isp_base + T32_VIC_BASE(0) + T32_VIC_INT_CLR2);
	writel(t0, isp_base + T32_VIC_BASE(1) + T32_VIC_INT_CLR);
	writel(t1, isp_base + T32_VIC_BASE(1) + T32_VIC_INT_CLR2);
	t32_hw_stats.vic_irqs++;
#ifndef TX_ISP_T33
	/* RAW snapshot through the VIC's MDMA (vendor lib_tisp_vic_intp_handle, openimp #491) */
	t32_snap_vic_irq(0, s0, s1);
	t32_snap_vic_irq(1, t0, t1);
#endif
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/* VIC and CSI                                                        */

static void t32_vic_wr(int n, u32 off, u32 v)
{
	writel(v, isp_base + T32_VIC_BASE(n) + off);
}

/* ------------------------------------------------------------------ */
/* register lists (RCFG, tx_isp_t32_rcfg.h)                          */

#define T32_RCFG_LIST_WORDS	4096	/* the vendor's stream-on list: 3479 words */
static unsigned long rcfg_pages;	/* table (256 B) + list */
static u32 rcfg_pa;
u32 t32_rcfg_runs, t32_rcfg_timeouts, t32_rcfg_last_len;

/*
 * T33 per-block lists (tx_isp_t32_rcfg33.h): one page per module, built at
 * stream-on, started all at once.  Unused on the T32.
 */
static unsigned long t33_mod_page[T33_MODULES];
static u64 t33_mod_built;
u32 t33_rcfg_runs, t33_rcfg_timeouts;

/* a module's list page, uncached (the core reads it by DMA); NULL without memory */
void t33_osd_list_rebuilt(void);	/* tx_isp_t33_osd.c */

static u32 *t33_mod_buf(unsigned int id)
{
	if (id >= T33_MODULES)
		return NULL;
	if (!t33_mod_page[id])
		t33_mod_page[id] = __get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!t33_mod_page[id]) {
		isp_printf(2, "tx-isp-t33: no memory for register list %u\n", id);
		return NULL;
	}
	return (u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_mod_page[id]));
}

static void t33_mod_done(unsigned int id, int len)
{
	if (len < 0) {
		isp_printf(2, "tx-isp-t33: register list %u not built\n", id);
		t33_mod_built &= ~(1ULL << id);
		return;
	}
	t33_mod_built |= 1ULL << id;
}

/* list from the block's writes in ascending register order (top, CSC, CSCCR, CDNS) */
static void t33_mod_set(unsigned int id, const struct t32_front_write *w, unsigned int n)
{
	struct t32_rcfg_list l;
	u32 *buf;

	if (!n || !(buf = t33_mod_buf(id)))
		return;
	t33_mod_done(id, t33_rcfg_build(&l, buf, T33_SLOT_WORDS, w, n));
}

/* list in the module's fixed layout (tx_isp_t33_blk.h) */
static void __maybe_unused t33_mod_set_tmpl(unsigned int id, const struct t33_tcmd *t, unsigned int nt,
			     const struct t32_front_write *w, int n)
{
	u32 *buf;

	if (n <= 0) {
		isp_printf(1, "tx-isp-t33: block of list %u not built (%d)\n", id, n);
		return;
	}
	if (!(buf = t33_mod_buf(id)))
		return;
	t33_mod_done(id, t33_rcfg_build_tmpl(buf, T33_SLOT_WORDS, t, nt, w, (unsigned int)n));
}
#define T33_TMPL(t) (t), ARRAY_SIZE(t)

/* LSC mesh table, read by the core by DMA from the address in 0x5078 */
static unsigned long t33_lsc_pages;
#define T33_LSC_ORDER	get_order(T33_LSC_LUT_BYTES)
/* defog statistics area (four 2 KiB parts, 0xa210..0xa21c), written by the core */
static unsigned long t33_defog_pages;
static u8 t33_defog_sec[T33_DEFOG_SEC_BYTES];
#define T33_DEFOG_ORDER	get_order(T33_DEFOG_DMA_BYTES)
/* LCE curve buffer (0xc0a4, written by us) and statistics buffer (0xc090.., by the core) */
static unsigned long t33_lce_curve_pages, t33_lce_stat_pages;
static u8 t33_lce_sec[T33_LCE_SEC_BYTES];
#define T33_LCE_CURVE_ORDER	get_order(T33_LCE_CURVE_BYTES)
#define T33_LCE_STAT_ORDER	get_order(T33_LCE_STAT_BYTES)
/* LSF buffer (list 37 slots 7 / 8) and, in the LSF mode, its histogram buffer (slot 5) */
static unsigned long t33_lsf_pages, t33_lsf_hist_pages;
static u8 t33_lsf_sec[T33_LCE_SEC_BYTES];
#define T33_LSF_ORDER		get_order(T33_LSF_BUF_BYTES)
#define T33_LSF_HIST_ORDER	get_order(T33_LSF_HIST_BYTES)
/* AF statistics buffer (0x18108..0x18114, written by the core) */
static unsigned long t33_af_pages;
static u8 t33_af_sec[T33_AF_SEC_BYTES];
#define T33_AF_ORDER	get_order(T33_AF_DMA_BYTES)
/* TMO V table (0x70e0, read by the core; also written through 0x32260 / 0x32264) */
static unsigned long t33_tmo_pages;
static unsigned long t33_ae_stat_pages, t33_ae_hist_pages;	/* AE statistics / histograms (DMA) */
static unsigned long t33_awb_stat_pages;	/* AWB zone statistics (DMA, 4 x 0x7400) */
static u32 t33_awb_rows, t33_awb_cols, t33_ae_rows, t33_ae_cols;	/* statistics grids (init) */
/* T33 statistics read-out (tx_isp_t33_stats.h): last frame and counts, for the proc file */
static struct {
	u32 awb_frames, awb_bad, ae_frames, ae_bad, hist_frames, hist_bad, mean16;
	struct t33_awb_sum awb;
	struct t33_ae_sum ae;
} t33_st;
#define T33_TMO_ORDER	get_order(T33_TMO_V_BYTES)

static void t33_mod_free(void)
{
	unsigned int i;

#ifdef TX_ISP_T33
	t33_awbc_detach();
	t33_aec_detach();
	t33_lcepf_free();	/* before its buffers */
#endif

	if (t33_lsc_pages)
		free_pages(t33_lsc_pages, T33_LSC_ORDER);
	t33_lsc_pages = 0;
	if (t33_defog_pages)
		free_pages(t33_defog_pages, T33_DEFOG_ORDER);
	t33_defog_pages = 0;
	if (t33_lce_curve_pages)
		free_pages(t33_lce_curve_pages, T33_LCE_CURVE_ORDER);
	t33_lce_curve_pages = 0;
	if (t33_lce_stat_pages)
		free_pages(t33_lce_stat_pages, T33_LCE_STAT_ORDER);
	t33_lce_stat_pages = 0;
#ifdef TX_ISP_T33
	if (t33_lsf_pages)
		free_pages(t33_lsf_pages, T33_LSF_ORDER);
	t33_lsf_pages = 0;
	if (t33_lsf_hist_pages)
		free_pages(t33_lsf_hist_pages, T33_LSF_HIST_ORDER);
	t33_lsf_hist_pages = 0;
#endif
	if (t33_af_pages)
		free_pages(t33_af_pages, T33_AF_ORDER);
	t33_af_pages = 0;
	if (t33_tmo_pages)
		free_pages(t33_tmo_pages, T33_TMO_ORDER);
	t33_tmo_pages = 0;
	if (t33_ae_stat_pages)
		free_pages(t33_ae_stat_pages, get_order(T33_AE_STAT_BYTES));
	t33_ae_stat_pages = 0;
	if (t33_ae_hist_pages)
		free_pages(t33_ae_hist_pages, get_order(T33_AE_HIST_BYTES));
	t33_ae_hist_pages = 0;
	if (t33_awb_stat_pages)
		free_pages(t33_awb_stat_pages, get_order(T33_AWB_STAT_BYTES));
	t33_awb_stat_pages = 0;

	for (i = 0; i < T33_MODULES; i++) {
		if (t33_mod_page[i])
			free_page(t33_mod_page[i]);
		t33_mod_page[i] = 0;
	}
	t33_mod_built = 0;
#ifdef TX_ISP_T33
	t33_mod_dirty = 0;
	t33_awb_refresh = false;
#endif
}

/* tisp_rcfg_init: table, control and masks; at the core init */
static void t33_rcfg_core_init(void)
{
	isp_wr(T33_RCFG_MODE, 0);
	isp_wr(T33_RCFG_TABLE, rcfg_pa);
	isp_wr(T33_RCFG_CTRL, 1);
	isp_wr(T33_RCFG_MASK0, 0xffffffff);
	isp_wr(T33_RCFG_MASK1, 0xffffffff);
}

/* lists the vendor's tisp_rcfg_t33_write_all_reg does not start (LSF 37: LCE irq only) */
#ifdef TX_ISP_T33
#define T33_NOT_AT_STREAMON	(1ULL << T33_MOD_LSF)
#else
#define T33_NOT_AT_STREAMON	0ULL
#endif

/*
 * tisp_rcfg_t33_write_all_reg + lib_tisp_stream_on: every built list into
 * its table word, all but the top list started together, then the top list,
 * then wait until both trigger words read 0.
 */
static void t33_rcfg_write_all(void)
{
	u32 *table = (u32 *)CKSEG1ADDR(rcfg_pa);
	u32 lo = 0, hi = 0;
	unsigned int i;

	if (!rcfg_pages || !t33_mod_built)
		return;
	memset(table, 0, T32_RCFG_TABLE_WORDS * 4);
	for (i = 0; i < T33_MODULES; i++) {
		if (!(t33_mod_built & (1ULL << i)) || (T33_NOT_AT_STREAMON & (1ULL << i)))
			continue;
		table[i] = virt_to_phys((void *)t33_mod_page[i]);
		if (i < 32)
			lo |= 1U << i;
		else
			hi |= 1U << (i - 32);
	}
	wmb();
	if (lo)
		isp_wr(T33_RCFG_TRIG, lo);
	if (hi)
		isp_wr(T33_RCFG_TRIG_HI, hi);
	for (i = 0; i < 1000; i++) {
		if (!isp_rd(T33_RCFG_TRIG) && !isp_rd(T33_RCFG_TRIG_HI))
			break;
		udelay(1);
	}
	if (i == 1000) {
		t33_rcfg_timeouts++;
		isp_printf(2, "tx-isp-t33: register lists did not finish (%#x %#x)\n",
			   isp_rd(T33_RCFG_TRIG), isp_rd(T33_RCFG_TRIG_HI));
	}
	t33_rcfg_runs++;
#ifdef TX_ISP_T33
	/*
	 * The init's tisp*Refresh flags: the vendor runs these lists once more
	 * at the first frame end, list 29 at the first AWB interrupt (QEMU trace).
	 */
	t33_mod_dirty |= lo & T33_INIT_REFRESH;
	t33_awb_refresh = !!(lo & (1U << T33_MOD_AWB));
	t33_first_fe = true;
#endif
}

#ifdef TX_ISP_T33
static void t33_first_frame_words(void)
{
	u32 *l, w;

	if ((t33_mod_built & (1ULL << T33_MOD_TMO)) && (l = t33_mod_buf(T33_MOD_TMO)) &&
	    (w = t33_list_word(l, T33_TMO_START_REG))) {
		l[w] = 0;
		t33_mod_dirty |= 1U << T33_MOD_TMO;
	}
	if ((t33_mod_built & (1ULL << T33_MOD_MDNS)) && (l = t33_mod_buf(T33_MOD_MDNS)) &&
	    (w = t33_list_word(l, T33_MDNS_START_REG)))
		l[w] = 0;
}
#endif

static unsigned int lsc_ct = 5000;
module_param(lsc_ct, uint, 0644);
MODULE_PARM_DESC(lsc_ct, "colour temperature for the LSC mesh at stream-on (vendor init: 5000)");
static u32 lsc_scratch[T32_LSC_WORDS];
static struct t32_lsc_work lsc_work;

/*
 * AE and AWB statistics engines (tx_isp_t32_stats.h), initialised from the IQ
 * bin as the vendor does at AddSensor; here at stream-on, when the sensor size
 * and the bin are known.  The buffers live as long as the module, and the
 * engines are switched off before they are freed (t32_hw_exit).
 */
#define T32_AE_BUF_ORDER	3	/* 32 KiB >= 0x6000 */
#define T32_AWB_BUF_ORDER	5	/* 128 KiB >= 0x1c000 */
static unsigned long ae_buf, awb_buf;

/* the four AE zone buffers (4 KiB each, at the start of the AE statistics buffer), uncached */
const u8 *t32_hw_ae_zones(void)
{
	if (!ae_buf)
		return NULL;
#ifdef CONFIG_MIPS
	return (const u8 *)CKSEG1ADDR(virt_to_phys((void *)ae_buf));
#else
	return (const u8 *)ae_buf;
#endif
}

/* the four AWB statistics buffers (0x7000 each), uncached */
static const u8 *t32_hw_awb_bufs(void)
{
	if (!awb_buf)
		return NULL;
#ifdef CONFIG_MIPS
	return (const u8 *)CKSEG1ADDR(virt_to_phys((void *)awb_buf));
#else
	return (const u8 *)awb_buf;
#endif
}

static void t32_stats_off(void)
{
	isp_wr(T32_AE_TRIG, 0);
	isp_wr(0x17088, 0);
	isp_wr(T32_AWB_TRIG, 0);
	isp_wr(0x16088, 0);
}

static void t32_stats_init(void)
{
	struct t32_stats_write w[T32_STATS_MAX_WRITES];
	const u8 *bin;
	u32 bytes, width, height;
	unsigned int n, i;

	if (t32_core_iq_bin(&bin, &bytes) || t32_core_sensor_size(&width, &height))
		return;
	if (T32_SOC_LIST_IRQ) {
		/* T33: other AE/AWB register layout (lists 29/30), open (#389) */
		isp_printf(1, "tx-isp-t33: AE/AWB statistics not built for the T33 yet\n");
		return;
	}
	/* the AE/AWB blocks where this bin's section table puts them */
	if (t32_iq_section(bin, bytes, T32_IQ_SEC_AE, NULL) != T32_AE_PARAM ||
	    t32_iq_section(bin, bytes, T32_IQ_SEC_AWB, NULL) != T32_AWB_PARAM) {
		isp_printf(1, "tx-isp-t32: IQ bin AE/AWB sections not at the known offsets, statistics off\n");
		return;
	}
	if (!ae_buf)
		ae_buf = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T32_AE_BUF_ORDER);
	if (!awb_buf)
		awb_buf = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T32_AWB_BUF_ORDER);
	if (!ae_buf || !awb_buf) {
		isp_printf(2, "tx-isp-t32: no memory for the statistics buffers\n");
		return;
	}
	n = t32_ae_stats_init(bin, width, height, virt_to_phys((void *)ae_buf), w);
	for (i = 0; i < n && i < T32_STATS_MAX_WRITES; i++)
		isp_wr(w[i].reg, w[i].val);
	if (t32_pp_on(T32_PP_AE)) {	/* the vendor AE context (tisp_ae_init state part, openimp #234) */
		int r = t32_pf_attach_ae(bin + T32_AE_PARAM, width, height);

		if (r)
			isp_printf(2, "tx-isp-t32: vendor AE context not built (%d)\n", r);
	}
	n = t32_awb_stats_init(bin, width, height, virt_to_phys((void *)awb_buf), T32_AWB_INIT_EV, w);
	for (i = 0; i < n && i < T32_STATS_MAX_WRITES; i++)
		isp_wr(w[i].reg, w[i].val);
	if (t32_pp_on(T32_PP_AWB) &&	/* the vendor AWB context (tisp_awb_init state part, openimp #234) */
	    t32_awbc_attach(bin + T32_AWB_PARAM, width, height, t32_hw_awb_bufs()))
		isp_printf(2, "tx-isp-t32: vendor AWB per-frame chain not started\n");
	t32_hw_stats.stats_inits++;
}

/*
 * Front end (tx_isp_t32_front.h): CSC, CSCCR and the ISP top block from the
 * IQ bin, in the vendor's AddSensor order, before the statistics engines.
 */
static void t32_front_init(void)
{
	struct t32_front_write w[T32_FRONT_MAX_WRITES];
	const u8 *bin;
	u32 bytes, width, height, code, off, len;
	int bayer, r;
	unsigned int n, i;

	if (t32_core_iq_bin(&bin, &bytes) || t32_core_sensor_size(&width, &height) ||
	    t32_core_sensor_mbus(&code))
		return;
	bayer = t32_mbus_to_bayer(code);
	if (bayer < 0) {
		isp_printf(1, "tx-isp-t32: sensor format 0x%x not handled by the front end\n", code);
		return;
	}
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &len);
	if (off && T32_IQ_SEC_OK(len, T32_CSC_BYTES)) {
		n = t32_csc_init(bin + off, w);
		if (T32_SOC_LIST_IRQ)
			t33_mod_set(T33_MOD_CSC, w, n);
		for (i = 0; !T32_SOC_LIST_IRQ && i < n; i++)
			isp_wr(w[i].reg, w[i].val);
	}
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_CSCCR, &len);
	if (off && T32_IQ_SEC_OK(len, T32_CSCCR_BYTES)) {
		r = t32_csccr_init(bin + off, w);
		if (T32_SOC_LIST_IRQ && r > 0) {
			for (i = 0; i < (unsigned int)r; i++)
				if (w[i].reg == 0x14010)
					w[i].val = T33_CSCCR_EN;
			t33_mod_set(T33_MOD_CSCCR, w, r);
		}
		for (i = 0; !T32_SOC_LIST_IRQ && r > 0 && i < (unsigned int)r; i++)
			isp_wr(w[i].reg, w[i].val);
	}
#ifdef TX_ISP_T33
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_TOP, &len);
	if (off && len == T33_TOP_BYTES) {
		struct t32_front_write dw[T33_TOP_DIRECT_WRITES];

		t33_top_init(bin + off, width, height, (u32)bayer, 0, w, dw);	/* buffered path only */
		for (i = 0; i < T33_TOP_DIRECT_WRITES; i++)
			isp_wr(dw[i].reg, dw[i].val);
		if (t32_ldc_top_hldc())	/* tisp_top_ldc_bypass_refresh: HLDC takes the LDC out of bypass */
			w[0].val &= ~0x800000U;
		t33_top_byp = w[0].val;
		t33_mod_set(T33_MOD_TOP, w, T33_TOP_LIST_WRITES);
	}
	return;
#endif
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_TOP, &len);
	if (off && T32_IQ_SEC_OK(len, T32_TOP_BYTES)) {
		n = t32_top_init(bin + off, width, height, (u32)bayer, 0, w);
		for (i = 0; i < n; i++)
			isp_wr(w[i].reg, w[i].val);
	}
}

/*
 * Remaining tisp init blocks of the vendor's AddSensor that are derived from
 * the IQ bin (S4 steps 5 and 6), in the vendor's order: DMSC, gamma, CDNS, GIB, DPC, YDNS, CCM, LCE, SDNS, YSP, MDNS, CLM,
 * RDNS (only on its ported path, t32_ported_paths bit 9: the old path never programmed RDNS).
 * Each block is one write list; a block whose section is missing or does
 * not have the expected size is skipped (the hardware keeps its reset state).
 */
#define T32_TISP_MAX_WRITES	T32_MDNS_MAX_WRITES
static struct t32_front_write *tisp_w;

/*
 * LCE DMA buffers of tisp_lce_malloc_cfg (statistics banks 0x26400 bytes, curve 0x5a00 bytes, histogram
 * 0x9000 bytes of which tisp_lsf_interrupt_static syncs 0x9900), kept until the module is removed
 */
#define T32_LCE_WORK_ORDER	6	/* 256 KiB >= 0x26400 */
#define T32_LCE_CURVE_ORDER	3	/* 32 KiB >= 0x5a00 */
#define T32_LCE_HIST_ORDER	4	/* 64 KiB >= 0x9900 */
static unsigned long lce_work, lce_curve, lce_hist;

static void *t32_uncached(unsigned long buf)
{
	unsigned long pa = virt_to_phys((void *)buf);

#ifdef CONFIG_MIPS
	return (void *)CKSEG1ADDR(pa);		/* uncached: the engine reads by DMA */
#else
	return phys_to_virt(pa);
#endif
}

static int t32_lce_build(const u8 *sec, u32 width, u32 height)
{
	if (!lce_work)
		lce_work = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T32_LCE_WORK_ORDER);
	if (!lce_curve)
		lce_curve = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T32_LCE_CURVE_ORDER);
	if (!lce_hist)
		lce_hist = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T32_LCE_HIST_ORDER);
	if (!lce_work || !lce_curve || !lce_hist)
		return -ENOMEM;
#ifdef CONFIG_MIPS
	/*
	 * The zeroing above went through the data cache; write it back before the uncached curve writes (a
	 * later eviction of those lines would overwrite them) and before the per-frame path (tx_isp_t32_blkirq.c)
	 * reads and writes the buffers through the cache with the vendor's cache syncs.
	 */
	dma_cache_wback_inv(lce_work, PAGE_SIZE << T32_LCE_WORK_ORDER);
	dma_cache_wback_inv(lce_curve, PAGE_SIZE << T32_LCE_CURVE_ORDER);
	dma_cache_wback_inv(lce_hist, PAGE_SIZE << T32_LCE_HIST_ORDER);
#endif
	t32_lce_curve(t32_uncached(lce_curve));			/* both halves hold the default curve */
	t32_lce_curve((u8 *)t32_uncached(lce_curve) + 0x2d00);
	wmb();
	t32_blk_lce_bufs((void *)lce_curve, (void *)lce_work, (void *)lce_hist);
	return t32_lce_init(sec, width, height, virt_to_phys((void *)lce_work),
			    virt_to_phys((void *)lce_curve), tisp_w);
}

static void t32_tisp_flush(const char *name, int n)
{
	int i;

	if (n <= 0) {
		isp_printf(1, "tx-isp-t32: IQ bin block %s not built (%d)\n", name, n);
		return;
	}
	for (i = 0; i < n && i < T32_TISP_MAX_WRITES; i++)
		isp_wr(tisp_w[i].reg, tisp_w[i].val);
}

/* MDNS has a 1.7 KB value block: heap, not stack */
static int t32_mdns_build(u8 *sec, u32 width, u32 height)
{
	u8 *o = kmalloc(T32_MDNS_VALUES, GFP_KERNEL);
	int n;

	if (!o)
		return -ENOMEM;
	n = t32_mdns_init(sec, width, height, t32_core_memopt(), o, tisp_w);
	if (n > 0)	/* the per-frame context keeps the section and the value block */
		t32_pf_attach_mdns(kmemdup(sec, T32_MDNS_SEC_BYTES, GFP_KERNEL), o, width, height,
				   t32_core_memopt());
	else
		kfree(o);
	return n;
}

#ifdef TX_ISP_T33
/*
 * T33 blocks in their module layouts (tx_isp_t33_blk.h), at the init gain
 * 0x10000 as the vendor's tisp_*_init.  A section of another size than the
 * PRJ008 one is skipped (the block keeps its reset state).
 */
#define T33_INIT_GAIN	0x10000U

/* block state of the blocks filled the vendor's way (layout + slot stores) */
static u32 t33_dmsc_v[T33_DMSC_WORDS], t33_dpc_v[T33_DPC_WORDS], t33_ysp_v[T33_YSP_WORDS];
static u32 t33_rdns_v[T33_RDNS_WORDS], t33_ccm_v[T33_CCM_WORDS], t33_lsc_v[T33_LSC_WORDS];
static struct t33_lsc t33_lsc_m;
static u8 t33_bcsh_sec[T33_BCSH_SEC_BYTES], t33_bcsh_st[T33_BCSH_ST_BYTES];
static u32 t33_tmo_v[T33_TMO_WORDS], t33_mdns_v[T33_MDNS_WORDS];
#define T33_INIT_EV	0xffffffffU	/* the vendor's EV before the first AE run */

#define T33_MOD_VB(blk, BLK, v)								\
	do {										\
		u32 *buf_;								\
		off = t32_iq_section(bin, bytes, T33_SEC_##BLK, &len);			\
		if (off && len == T33_##BLK##_SEC_BYTES && (buf_ = t33_mod_buf(T33_MOD_##BLK)))	\
			t33_mod_done(T33_MOD_##BLK, t33_##blk##_init(bin + off, T33_INIT_GAIN, v,	\
								     buf_, T33_SLOT_WORDS));	\
	} while (0)

/* TMO, MSCA, CLM, BCSH: a frame of their own (stack) */
static noinline_for_stack void t33_tisp_init_tone(const u8 *bin, u32 bytes)
{
	u32 off, len, width, height;

	off = t32_iq_section(bin, bytes, T33_SEC_TMO, &len);
	if (off && len == T33_TMO_SEC_BYTES && !t32_core_sensor_size(&width, &height)) {
		u32 *buf = t33_mod_buf(T33_MOD_TMO), *vt, i;

		if (!t33_tmo_pages)
			t33_tmo_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T33_TMO_ORDER);
		if (buf && t33_tmo_pages) {
			/* the V table uncached, then the vendor's direct writes (0x7094, V ram) */
			vt = (u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_tmo_pages));
			t33_tmo_vtable(vt);
			t33_mod_done(T33_MOD_TMO, t33_tmo_init(bin + off, width, height, T33_CCM_INIT_EV,
							       (u32)virt_to_phys((void *)t33_tmo_pages), t33_tmo_v,
							       buf, T33_SLOT_WORDS));
			isp_wr(0x7094, 1);
			isp_wr(0x32260, 0x101);
			for (i = 0; i < T33_TMO_V_WORDS; i++)
				isp_wr(0x32264, vt[i]);
			isp_wr(0x32260, 0x10102);
		}
	}
	if (!t32_core_sensor_size(&width, &height)) {
		u32 *buf = t33_mod_buf(T33_MOD_MSCA), i;

		/* MSCA init list (single sensor: per-channel 0xf0048 blocks at 0xf06b4) */
		off = t32_iq_section(bin, bytes, T33_SEC_MSCA, &len);
		if (buf) {
			for (i = 0; i < 3; i++)
				isp_wr(0xf0128 + 0x100 * i, 0);
			t33_mod_done(T33_MOD_MSCA, t33_msca_init(width, height, off && len > 0x30 ? bin + off : NULL,
								 buf, T33_SLOT_WORDS));
			t33_osd_list_rebuilt();	/* OSD / mask set before a stream-off survive in the new list */
		}
	}
	off = t32_iq_section(bin, bytes, T33_SEC_CLM, &len);
	if (off && len == T33_CLM_SEC_BYTES) {
		u32 *buf = t33_mod_buf(T33_MOD_CLM), coff, clen;

		/* CT 5000 K as the CCM; the colour space section for the csc set */
		coff = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &clen);
		if (buf && coff && clen == T32_CSC_BYTES)
			t33_mod_done(T33_MOD_CLM, t33_clm_init(bin + off, bin + off + len,
							       t32_iq_payload(bin + off, bytes - off, T33_SEC_CLM),
							       bin + coff, T33_CCM_INIT_CT, buf, T33_SLOT_WORDS));
	}
	off = t32_iq_section(bin, bytes, T33_SEC_BCSH, &len);
	if (off && len == T33_BCSH_SEC_BYTES) {
		u32 *buf = t33_mod_buf(T33_MOD_BCSH), coff, clen;

		/* CT 5000 K, EV 0 as the CCM; the colour space section for the csc set */
		coff = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &clen);
		memcpy(t33_bcsh_sec, bin + off, sizeof(t33_bcsh_sec));
		if (buf && coff && clen == T32_CSC_BYTES)
			t33_mod_done(T33_MOD_BCSH,
				     t33_bcsh_init(t33_bcsh_sec, bin + off + len,
						   t32_iq_payload(bin + off, bytes - off, T33_SEC_BCSH), bin + coff,
						   T33_CCM_INIT_CT, T33_CCM_INIT_EV, t33_bcsh_st, buf, T33_SLOT_WORDS));
	}
}

/*
 * MDNS: init gain, the driver's memopt as the zip ratio, the ISP working
 * memory libimp handed over (SET_ISP_MEM_INFO); 0xd080 / 0xd084 direct
 * (single sensor); then the decode words from the YSP block state.
 */
static noinline_for_stack void t33_tisp_init_mdns(const u8 *bin, u32 bytes)
{
	u32 off, len, width, height, pa, d[2], *buf;

	off = t32_iq_section(bin, bytes, T33_SEC_MDNS, &len);
	if (!off || len != T33_MDNS_SEC_BYTES || t32_core_sensor_size(&width, &height) || t32_core_isp_mem(&pa))
		return;
	buf = t33_mod_buf(T33_MOD_MDNS);
	if (!buf)
		return;
	t33_mod_done(T33_MOD_MDNS, t33_mdns_init(bin + off, T33_INIT_GAIN, width, height, t32_core_memopt(), pa,
						 t33_mdns_v, buf, T33_SLOT_WORDS, d));
	if (!(t33_mod_built & (1ULL << T33_MOD_MDNS)))
		return;
	isp_wr(0xd084, d[1]);
	isp_wr(0xd080, d[0]);
	if (t33_mod_built & (1ULL << T33_MOD_YSP))
		t33_mdns_decode(t33_ysp_v, buf);
}

/*
 * tisp_ae_init's lists: AE statistics set-up (list 30) on our statistics
 * and histogram buffers, DG (list 5) at 1.0.
 */
static const u8 *t33_stat_va(unsigned long pages, u32 off);

static noinline_for_stack void t33_tisp_init_ae(const u8 *bin, u32 bytes)
{
	u32 off, len, width, height, *buf;

	buf = t33_mod_buf(T33_MOD_DG);
	if (buf)
		t33_mod_done(T33_MOD_DG, t33_dg_init(T33_AE_DG_INIT, buf, T33_SLOT_WORDS));
	off = t32_iq_section(bin, bytes, T33_SEC_AE, &len);
	if (!off || len < T33_AE_SEC_MIN || t32_core_sensor_size(&width, &height))
		return;
	if (!t33_ae_stat_pages)
		t33_ae_stat_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order(T33_AE_STAT_BYTES));
	if (!t33_ae_hist_pages)
		t33_ae_hist_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order(T33_AE_HIST_BYTES));
	/* tisp_ae_weight_mean's grid: rows 0xad2, columns 0xad6 (15 a row in the buffer) */
	t33_ae_rows = min_t(u32, t32_tp_le16(bin + off + 0xad2), T33_AE_GRID);
	t33_ae_cols = min_t(u32, t32_tp_le16(bin + off + 0xad6), T33_AE_GRID);
	buf = t33_mod_buf(T33_MOD_AE);
	if (buf && t33_ae_stat_pages && t33_ae_hist_pages)
		t33_mod_done(T33_MOD_AE, t33_ae_init(bin + off, width, height,
						     (u32)virt_to_phys((void *)t33_ae_stat_pages),
						     (u32)virt_to_phys((void *)t33_ae_hist_pages), buf, T33_SLOT_WORDS));
	/* tisp_ae_init's state, then the AE frames per statistics interrupt */
	if (t33_ae_stat_pages && t33_ae_hist_pages &&
	    t33_aec_attach(bin + off, len, width, height, t33_stat_va(t33_ae_stat_pages, 0),
			   t33_stat_va(t33_ae_hist_pages, 0)))
		isp_printf(1, "tx-isp-t33: per-frame AE not started\n");
}

/* a statistics buffer, uncached: the engines write by DMA */
static const u8 *t33_stat_va(unsigned long pages, u32 off)
{
	unsigned long pa = virt_to_phys((void *)pages) + off;

#ifdef CONFIG_MIPS
	return (const u8 *)CKSEG1ADDR(pa);
#else
	return phys_to_virt(pa);
#endif
}

/*
 * The statistics frames the engines finished, read as the vendor's
 * interrupt handlers pick them (current buffer from 0x1608c / 0x1708c);
 * the AE / AWB algorithms will run on them (openimp #389).
 */
static void t33_stats_work(struct work_struct *w)
{
	struct t33_awb_sum as;
	struct t33_ae_sum es;
	u32 idx, tag, m16;

	if (t33_awb_stat_pages && t33_awb_rows) {
		idx = t33_awb_buf_idx(isp_rd(T33_AWB_IDX_REG));
		if (!t33_awb_read(t33_stat_va(t33_awb_stat_pages, idx * T33_AWB_STAT_ONE), T33_AWB_STAT_ONE,
				  t33_awb_rows, t33_awb_cols, &as)) {
			t33_st.awb = as;
			t33_st.awb_frames++;
		} else {
			t33_st.awb_bad++;
		}
	}
	if (t33_ae_stat_pages && t33_ae_rows) {
		idx = isp_rd(T33_AE_IDX_REG);
		if (!t33_ae_read(t33_stat_va(t33_ae_stat_pages, t33_ae_zbuf_idx(idx) * T33_AE_ZBUF_BYTES),
				 t33_ae_rows, t33_ae_cols, &es)) {
			t33_st.ae = es;
			t33_st.ae_frames++;
		} else {
			t33_st.ae_bad++;
		}
		if (t33_ae_hist_pages &&
		    !t33_ae_hist_read(t33_stat_va(t33_ae_hist_pages, t33_ae_hbuf_idx(idx) * T33_AE_HBUF_BYTES),
				      &tag, &m16)) {
			t33_st.mean16 = m16;
			t33_st.hist_frames++;
		} else if (t33_ae_hist_pages) {
			t33_st.hist_bad++;
		}
	}
}
static DECLARE_WORK(t33_stats_w, t33_stats_work);

void t33_stats_show(struct seq_file *m)
{
	seq_printf(m, "t33 statistics: awb %u (bad %u) grid %ux%u white %u/%u rg %u bg %u tag %u\n",
		   t33_st.awb_frames, t33_st.awb_bad, t33_awb_cols, t33_awb_rows, t33_st.awb.white,
		   t33_st.awb.zones, t33_st.awb.rg, t33_st.awb.bg, t33_st.awb.tag);
	seq_printf(m, "t33 statistics: ae %u (bad %u) grid %ux%u luma %u lo %u hi %u tag %u, hist %u (bad %u) mean %u/16\n",
		   t33_st.ae_frames, t33_st.ae_bad, t33_ae_cols, t33_ae_rows, t33_st.ae.luma, t33_st.ae.lo,
		   t33_st.ae.hi, t33_st.ae.tag, t33_st.hist_frames, t33_st.hist_bad, t33_st.mean16);
	t33_awbc_show(m);
	t33_aec_show(m);
	seq_printf(m, "t33 lce/lsf: irq8 %u irq30 %u curves %u list16 %u list37 %u ev %u\n", t33_lcepf_irq8s,
		   t33_lcepf_irq30s, t33_lcepf_curves, t33_lcepf_lce_runs, t33_lcepf_lsf_runs, t33_lcepf_evs);
}

/* for ../t33/tx_isp_t33_awbc.c: register read, and the per-frame AWB results into lists 7 / 29 */
u32 t33_hw_rd(u32 reg)
{
	return isp_rd(reg);
}

/* tisp_awb_set_gain: AWBG list 7 slots 3 / 4, run at the next frame end (tispAwbgRefresh) */
void t33_awb_list_gain(u32 r, u32 b)
{
	u32 *l;

	if (!(t33_mod_built & (1ULL << T33_MOD_AWBG)) || !(l = t33_mod_buf(T33_MOD_AWBG)))
		return;
	T33_SW(l, 3, r);
	T33_SW(l, 4, b);
	t33_mod_dirty |= 1U << T33_MOD_AWBG;
}

/*
 * tisp_ae_long_ev_alloc's DG: list 5 slots 3 / 4 (0x4060 / 0x4064) and 1
 * (0x4010 = 1), run at once when no list runs (tisp_rcfg_get_dma_state),
 * else at the next frame end.
 */
void t33_dg_list_set(u32 v)
{
	unsigned long flags;
	u32 *l;

	if (!(t33_mod_built & (1ULL << T33_MOD_DG)) || !(l = t33_mod_buf(T33_MOD_DG)))
		return;
	T33_SW(l, 3, v);
	T33_SW(l, 4, v);
	T33_SW(l, 1, 1);
	local_irq_save(flags);
	if (!isp_rd(T33_RCFG_TRIG))
		isp_wr(T33_RCFG_TRIG, 1U << T33_MOD_DG);
	else
		t33_mod_dirty |= 1U << T33_MOD_DG;
	local_irq_restore(flags);
}

/* tisp_awb_tgain_update's tispAwbRefresh = 1: list 29 runs at the next AWB interrupt */
void t33_awb_list_refresh(void)
{
	if (t33_mod_built & (1ULL << T33_MOD_AWB))
		ACCESS_ONCE(t33_awb_refresh) = true;
}

/* tisp_reg_write_t33(29, slot, v) of spec_calculate: list memory only (runs with tispAwbRefresh) */
void t33_awb_list_slot(int slot, u32 v)
{
	u32 *l;

	if (slot < 1 || !(t33_mod_built & (1ULL << T33_MOD_AWB)) || !(l = t33_mod_buf(T33_MOD_AWB)))
		return;
	T33_SW(l, slot, v);
}

/* tisp_reg_write_t33(ch, 23, slot, v): MSCA list memory only (the OSD / mask registers) */
void t33_msca_list_slot(int slot, u32 v)
{
	u32 *l;

	if (slot < 1 || !(t33_mod_built & (1ULL << T33_MOD_MSCA)) || !(l = t33_mod_buf(T33_MOD_MSCA)))
		return;
	T33_SW(l, slot, v);
}

/* tispMscaRefresh = 1: list 23 runs at the next frame end */
void t33_msca_list_refresh(void)
{
	if (t33_mod_built & (1ULL << T33_MOD_MSCA))
		t33_mod_dirty |= 1U << T33_MOD_MSCA;
}

/*
 * AF API on the T33 section (tisp_af_api_{set,get}_attr / _{set,get}_weight / _get_metric, vendor
 * 0x463a8 / 0x462d4 / 0x4621c / 0x46248 / 0x4627c): the section copy t33_af_sec is the vendor's
 * af_info + 0x4e8 block.  set_attr keeps the 24 B attribute (af_par_attr), patches the section words,
 * runs tisp_af_par_init + tisp_af_t33_all_reg_write again (t33_af_init on the patched section: the
 * list 31 words) and marks the list for the next frame end (tispAfRefresh; the vendor runs it at the
 * next AF interrupt).  All return false until the section exists (stream-on).
 */
static u8 t33_af_par_attr[24];

static bool t33_af_ready(void)
{
	return t33_af_pages && (t33_mod_built & (1ULL << T33_MOD_AF));
}

bool t33_af_attr_set(const u8 *a)
{
	u32 w, h;
	u32 *l;
	u8 tmp[T33_AF_SEC_BYTES];
	int n;

	memcpy(t33_af_par_attr, a, 24);
	if (!t33_af_ready() || t32_core_sensor_size(&w, &h) || !(l = t33_mod_buf(T33_MOD_AF)))
		return false;
	/*
	 * Patch a copy and build the list from it: the vendor divides by the window counts in
	 * tisp_af_par_init (a zero count crashes it); here a refused attribute leaves section and list as
	 * they were.
	 */
	memcpy(tmp, t33_af_sec, sizeof(tmp));
	t33_af_attr_patch(tmp, a);
	n = t33_af_init(tmp, w, h, (u32)virt_to_phys((void *)t33_af_pages), l, T33_SLOT_WORDS);
	if (n < 0) {
		isp_printf(1, "tx-isp-t33: AF attribute refused (window counts / sizes)\n");
		return false;
	}
	memcpy(t33_af_sec, tmp, sizeof(tmp));
	t33_mod_dirty |= 1U << T33_MOD_AF;
	return true;
}

bool t33_af_attr_get(u8 *o)
{
	if (!t33_af_ready())
		return false;
	t33_af_attr_read(t33_af_sec, t33_af_par_attr, o);
	return true;
}

bool t33_af_weight_set(const u8 *b)
{
	if (!t33_af_ready())
		return false;
	memcpy(t33_af_sec + 0x21f, b, 0xe1);
	return true;
}

bool t33_af_weight_get(u8 *b)
{
	if (!t33_af_ready())
		return false;
	memcpy(b, t33_af_sec + 0x21f, 0xe1);
	return true;
}

/* tisp_af_api_get_metric: two words of the section (written by the AF algorithm), shifted by the attribute */
bool t33_af_metric_get(u8 *b)
{
	u32 sh = t33_af_par_attr[9] & 0x1f, v;

	if (!t33_af_ready())
		return false;
	memcpy(&v, t33_af_sec + 0x44, 4);
	v >>= sh;
	memcpy(b, &v, 4);
	memcpy(&v, t33_af_sec + 0x40, 4);
	v >>= sh;
	memcpy(b + 4, &v, 4);
	return true;
}

/*
 * tisp_awb_init's lists: AWB statistics set-up (list 29) on our statistics
 * buffers at the init interpolation point, AWBG (list 7) at the section's
 * start gains.
 */
static noinline_for_stack void t33_tisp_init_awb(const u8 *bin, u32 bytes)
{
	u32 off, len, width, height, r, b, *buf;

	off = t32_iq_section(bin, bytes, T33_SEC_AWB, &len);
	if (!off || len < T33_AWB_SEC_MIN || t32_core_sensor_size(&width, &height))
		return;
	if (!t33_awb_stat_pages)
		t33_awb_stat_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order(T33_AWB_STAT_BYTES));
	buf = t33_mod_buf(T33_MOD_AWB);
	if (buf && t33_awb_stat_pages)
		t33_mod_done(T33_MOD_AWB, t33_awb_init(bin + off, width, height, T33_AWB_INIT_TGAIN,
						       (u32)virt_to_phys((void *)t33_awb_stat_pages), buf,
						       T33_SLOT_WORDS));
	{
		u32 zs;

		t33_awb_cols = t33_awb_zones(width, t33_awb_s16(bin + off, 0xc74), t33_awb_s16(bin + off, 0xc76), &zs);
		t33_awb_rows = t33_awb_zones(height, t33_awb_s16(bin + off, 0xc70), t33_awb_s16(bin + off, 0xc72), &zs);
	}
	t33_awb_start_gain(bin + off, 0, &r, &b);
	buf = t33_mod_buf(T33_MOD_AWBG);
	if (buf)
		t33_mod_done(T33_MOD_AWBG, t33_awbg_init(r, b, buf, T33_SLOT_WORDS));
	/* tisp_awb_init's run state, then tisp_awb_process per statistics frame */
	if (t33_awb_stat_pages && t33_awbc_attach(bin + off, len, width, height, t33_stat_va(t33_awb_stat_pages, 0)))
		isp_printf(1, "tx-isp-t33: per-frame AWB not started\n");
}

/*
 * tisp_long_tgain_update's block refreshes (AE event 4, the long total
 * gain, log2 Q16; process context, the AE work): DPC, DMSC, YSP, YDNS,
 * SDNS, RDNS, MDNS, CDNS.  Each vendor
 * tisp_*_par_refresh keeps the last gain and leaves when the new one is
 * closer than the init parameter word +1000 (fstframe_defaultp word 20,
 * 0x100); DPC has no such gate, MDNS reruns its list unchanged below it.
 * The blocks are rebuilt with their init builders at the new gain into a
 * scratch page and copied into the list page when that list is idle; the
 * list runs at the next frame end (the vendor's tisp*Refresh at the
 * block's frame-done interrupt).
 */
#define T33_GAIN_THR	0x100U
static u32 t33_gr_last[T33_MODULES], t33_gr_ev = T33_INIT_EV;
static int t33_clm_ct_last = T33_CCM_INIT_CT, t33_clm_seg_last = -1;	/* tisp_clm_ct_interp state */
static DEFINE_MUTEX(t33_gr_lock);
static u32 *t33_gr_tmp;
u32 t33_gr_runs[T33_MODULES];

/* the last AE / AWB values the blocks saw (the vendor blocks' own copies), for the bin switch */
static u32 t33_sw_gain = T33_INIT_GAIN, t33_sw_ev_lo = T33_CCM_INIT_EV, t33_sw_ev_hi, t33_sw_bv;
static u32 t33_sw_ct = T33_CCM_INIT_CT;
u32 t33_sw_runs, t33_sw_blocks;

static void t33_gref_reset(void)
{
	unsigned int i;

	for (i = 0; i < T33_MODULES; i++)
		t33_gr_last[i] = T33_INIT_GAIN;
	t33_sw_gain = T33_INIT_GAIN;
	t33_sw_ev_lo = T33_CCM_INIT_EV;
	t33_sw_ev_hi = t33_sw_bv = 0;
	t33_sw_ct = T33_CCM_INIT_CT;
	t33_gr_ev = T33_INIT_EV;
	t33_clm_ct_last = T33_CCM_INIT_CT;
	t33_clm_seg_last = -1;
}

/* the vendor's gate: true (and the gain kept) when the block refreshes */
static bool t33_gr_gate(unsigned int id, u32 gain)
{
	u32 last = t33_gr_last[id], d = last > gain ? last - gain : gain - last;

	if (last != 0xffffffffU && d < T33_GAIN_THR)
		return false;
	t33_gr_last[id] = gain;
	return true;
}

/* the rebuilt list (t33_gr_tmp) into the list page, then run it at the next frame end */
static void t33_gr_commit(unsigned int id, int len)
{
	unsigned long flags;
	u32 *buf;
	unsigned int i;

	if (len <= 0 || !(t33_mod_built & (1ULL << id)) || !(buf = t33_mod_buf(id)))
		return;
	t32_wait_clear(T33_RCFG_TRIG, 1U << id, 2000);	/* the core reads the page while the list runs */
	local_irq_save(flags);
	for (i = 0; i < T33_SLOT_WORDS; i++)
		if (buf[i] != t33_gr_tmp[i])
			buf[i] = t33_gr_tmp[i];
	t33_mod_dirty |= 1U << id;
	local_irq_restore(flags);
	t33_gr_runs[id]++;
}

static void t33_gr_rerun(unsigned int id)
{
	unsigned long flags;

	if (!(t33_mod_built & (1ULL << id)))
		return;
	local_irq_save(flags);
	t33_mod_dirty |= 1U << id;
	local_irq_restore(flags);
	t33_gr_runs[id]++;
}

/* the word of list l that holds reg's value (register commands only), 0 when absent */
static u32 t33_list_word(const u32 *l, u32 reg)
{
	u32 i = 1, n = l[0], c, cnt, off;

	while (i <= n && i < T33_SLOT_WORDS) {
		c = l[i];
		cnt = t32_rcfg_cmd_count(c);
		off = t32_rcfg_cmd_offset(c);
		if ((c & T32_RCFG_CMD) && reg >= off && reg < off + 4 * cnt)
			return i + 1 + (reg - off) / 4;
		i += 1 + cnt;
	}
	return 0;
}

static int t33_gr_tmpl(const struct t33_tcmd *t, unsigned int nt, int n)
{
	return n > 0 ? t33_rcfg_build_tmpl(t33_gr_tmp, T33_SLOT_WORDS, t, nt, tisp_w, (unsigned int)n) : n;
}

void t33_tgain_refresh(u32 gain)
{
	const u8 *bin;
	u32 bytes, off, len, width, height, pa, d[2], blc = 0, dg = 0, *b;

	ACCESS_ONCE(t33_sw_gain) = gain;
	if (t32_core_iq_bin(&bin, &bytes) || !tisp_w)
		return;
	mutex_lock(&t33_gr_lock);
	if (!t33_gr_tmp)
		t33_gr_tmp = kmalloc(T33_SLOT_WORDS * 4, GFP_KERNEL);
	if (!t33_gr_tmp)
		goto out;
#define T33_GR_SEC(BLK)	((off = t32_iq_section(bin, bytes, T33_SEC_##BLK, &len)) && len == T33_##BLK##_SEC_BYTES)
	/* tisp_blc_tgain_update: write_reg sets tispBlcRefresh at every update (PRJ008: same words) */
	t33_gr_rerun(T33_MOD_BLC);
	/* tisp_dpc_l_gain_update: every change */
	if (T33_GR_SEC(DPC)) {
		t33_gr_gate(T33_MOD_DPC, gain);
		memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
		t33_gr_commit(T33_MOD_DPC, t33_dpc_init(bin + off, gain, t33_dpc_v, t33_gr_tmp, T33_SLOT_WORDS));
	}
	if (T33_GR_SEC(DMSC) && t33_gr_gate(T33_MOD_DMSC, gain)) {
		memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
		t33_gr_commit(T33_MOD_DMSC, t33_dmsc_init(bin + off, gain, t33_dmsc_v, t33_gr_tmp, T33_SLOT_WORDS));
	}
	if (T33_GR_SEC(YSP) && t33_gr_gate(T33_MOD_YSP, gain)) {
		memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
		t33_gr_commit(T33_MOD_YSP, t33_ysp_init(bin + off, gain, t33_ysp_v, t33_gr_tmp, T33_SLOT_WORDS));
	}
	if (T33_GR_SEC(YDNS) && t33_gr_gate(T33_MOD_YDNS, gain)) {
		memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
		t33_gr_commit(T33_MOD_YDNS, t33_gr_tmpl(T33_TMPL(t33_ydns_tmpl), t33_ydns_init(bin + off, gain, tisp_w)));
	}
	if (T33_GR_SEC(SDNS) && t33_gr_gate(T33_MOD_SDNS, gain)) {
		memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
		t33_gr_commit(T33_MOD_SDNS, t33_gr_tmpl(T33_TMPL(t33_sdns_tmpl), t33_sdns_init(bin + off, gain, tisp_w)));
	}
	if (T33_GR_SEC(RDNS) && t33_gr_gate(T33_MOD_RDNS, gain)) {
		if ((t33_mod_built & (1ULL << T33_MOD_BLC)) && (b = t33_mod_buf(T33_MOD_BLC)))
			blc = b[4];
		if ((t33_mod_built & (1ULL << T33_MOD_DG)) && (b = t33_mod_buf(T33_MOD_DG)))
			dg = b[4];
		memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
		t33_gr_commit(T33_MOD_RDNS, t33_rdns_init(bin + off, gain, t33_gr_ev, blc, dg, t33_rdns_v, t33_gr_tmp,
							  T33_SLOT_WORDS));
	}
	if (T33_GR_SEC(MDNS) && !t32_core_sensor_size(&width, &height) && !t32_core_isp_mem(&pa)) {
		if (t33_gr_gate(T33_MOD_MDNS, gain)) {
			memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
			len = (u32)t33_mdns_init(bin + off, gain, width, height, t32_core_memopt(), pa, t33_mdns_v,
						 t33_gr_tmp, T33_SLOT_WORDS, d);
			if ((int)len > 0 && (t33_mod_built & (1ULL << T33_MOD_YSP)))
				t33_mdns_decode(t33_ysp_v, t33_gr_tmp);
			if ((int)len > 0) {	/* the start word 0xd018 is not part of tisp_mdns_par_refresh */
				u32 *cur = t33_mod_buf(T33_MOD_MDNS), a = t33_list_word(t33_gr_tmp, T33_MDNS_START_REG),
				    b = cur ? t33_list_word(cur, T33_MDNS_START_REG) : 0;

				if (a && b)
					t33_gr_tmp[a] = cur[b];
			}
			t33_gr_commit(T33_MOD_MDNS, (int)len);
		} else {
			t33_gr_rerun(T33_MOD_MDNS);	/* tispMdnsRefresh = 1 below the gate */
		}
	}
	off = t32_iq_section(bin, bytes, T32_SEC_CDNS, &len);
	if (off && len == T33_CDNS_SEC_BYTES && t33_gr_gate(T33_MOD_CDNS, gain)) {
		struct t32_rcfg_list l;
		int n = t33_cdns_gain(bin + off, gain, tisp_w);

		memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
		t33_gr_commit(T33_MOD_CDNS, n > 0 ? t33_rcfg_build(&l, t33_gr_tmp, T33_SLOT_WORDS, tisp_w, (unsigned int)n) : n);
	}
#undef T33_GR_SEC
out:
	mutex_unlock(&t33_gr_lock);
}

/*
 * tisp_long_ev_update's block part (AE event 6, on an EV change): BCSH
 * (tisp_bcsh_ev_update) and CCM (tisp_ccm_ev_update: the CT matrix of the
 * last CT update reaches the list words here, list 10 reruns every time),
 * LCE (tisp_lce_ev_update, top bypass bit 11 clear: lists 16 / 37 words
 * from the EV / BV interpolations, picked up by the next irq 8 start;
 * openimp #500).  The vendor's gamma / RDNS / defog / TMO EV updates leave
 * their words unchanged on PRJ008 (flat EV tables, QEMU trace); RDNS, defog
 * and TMO set their refresh flag every time (tisp_rdns_ev_update,
 * tisp_defog_ev_update, tisp_tmo_ev_update): lists 3, 12 and 8 rerun.
 * bv: the AE event's brightness word (the vendor's x argument).
 */
void t33_ev_refresh(u32 lo, u32 hi, u32 bv)
{
	const u8 *bin;
	u32 *buf, bytes;

	mutex_lock(&t33_gr_lock);
	t33_sw_ev_lo = lo;
	t33_sw_ev_hi = hi;
	t33_sw_bv = bv;
	t33_gr_ev = hi << 22 | lo >> 10;
	if (!t33_gr_tmp)
		t33_gr_tmp = kmalloc(T33_SLOT_WORDS * 4, GFP_KERNEL);
	if (t33_gr_tmp && (t33_mod_built & (1ULL << T33_MOD_BCSH)) && (buf = t33_mod_buf(T33_MOD_BCSH))) {
		memcpy(t33_gr_tmp, buf, T33_SLOT_WORDS * 4);
		if (t33_bcsh_ev_update(t33_bcsh_st, t33_bcsh_sec, lo, hi, t33_gr_tmp))
			t33_gr_commit(T33_MOD_BCSH, 1);
	}
	/* tisp_ccm_ev_update: the matrix words follow the CT here, list 10 reruns either way */
	if (t33_gr_tmp && (t33_mod_built & (1ULL << T33_MOD_CCM)) && (buf = t33_mod_buf(T33_MOD_CCM)) &&
	    !t32_core_iq_bin(&bin, &bytes)) {
		u32 len, off = t32_iq_section(bin, bytes, T33_SEC_CCM, &len);

		if (off && len == T33_CCM_SEC_BYTES) {
			memcpy(t33_gr_tmp, buf, T33_SLOT_WORDS * 4);
			if (t33_ccm_ev_update(t33_ccm_v, bin + off, lo, hi, t33_gr_tmp))
				t33_gr_commit(T33_MOD_CCM, 1);
			else
				t33_gr_rerun(T33_MOD_CCM);
		}
	}
	if (!(t33_top_byp & 0x800))
		t33_lcepf_ev(lo, hi, bv);
	t33_gr_rerun(T33_MOD_RDNS);
	t33_gr_rerun(T33_MOD_DEFOG);
	t33_gr_rerun(T33_MOD_TMO);
	mutex_unlock(&t33_gr_lock);
}

/*
 * tisp_ct_update's block part (AWB event 11, on a CT change): CCM
 * (tisp_ccm_ct_update: interp_by_ct unless the CT stays in an even
 * segment or within the hysteresis sec+0x738 of the last one; write_reg
 * stores the old matrix words: no calc_para_refresh here, list 10 reruns)
 * and CLM (tisp_clm_ct_interp: the same gate with the per-knot hysteresis
 * sec+0x1a and the state's 1, then write_reg).  BCSH / LSC CT updates
 * leave their words unchanged on PRJ008 (QEMU trace).
 */
static u32 t33_ccm_save[T33_CCM_WORDS];

/* the knot segment of tisp_clm_ct_interp: 2k on knot k, 2k + 1 between k and k + 1 */
static int t33_clm_seg(const u8 *sec, const u8 *lut, int ct)
{
	int n = sec[0x3b], i;

#define KCT(k)	((int)(s16)t32_tp_le16(lut + (k) * T33_CLM_KNOT16))
#define HY(k)	((int)t32_tp_le16(sec + 0x1a + 2 * (k)))
	if (n < 1 || !(KCT(0) + HY(0) < ct) || !(ct < KCT(n - 1) - HY(n - 1)))
		return KCT(0) + HY(0) < ct ? n : 0;
	for (i = 1; i < n; i++) {
		if (ct < KCT(i) - HY(i)) {
			if (KCT(i - 1) + HY(i - 1) < ct)
				return (2 * (i - 1) + 1) & 0xff;
		} else if (ct <= KCT(i) + HY(i)) {
			return (2 * i) & 0xff;
		}
	}
	return n;
#undef KCT
#undef HY
}

void t33_ct_refresh(u32 ct)
{
	const u8 *bin;
	u32 bytes, off, len, coff, clen, *buf;
	int seg, d;

	ACCESS_ONCE(t33_sw_ct) = ct;
	if (t32_core_iq_bin(&bin, &bytes))
		return;
	mutex_lock(&t33_gr_lock);
	if (!t33_gr_tmp)
		t33_gr_tmp = kmalloc(T33_SLOT_WORDS * 4, GFP_KERNEL);
	if (!t33_gr_tmp)
		goto out;
	off = t32_iq_section(bin, bytes, T33_SEC_CCM, &len);
	if (off && len == T33_CCM_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_CCM)) &&
	    (buf = t33_mod_buf(T33_MOD_CCM))) {
		const u8 *sec = bin + off, *lut = bin + off + len;

		memcpy(t33_ccm_save, t33_ccm_v, sizeof(t33_ccm_save));
		t33_ccm_ct(t33_ccm_v, sec, lut, ct);
		d = (int)(ct - t33_ccm_save[4]);
		if ((t33_ccm_v[5] == t33_ccm_save[5] && !(t33_ccm_v[5] & 1)) ||
		    (d < 0 ? -d : d) <= (int)t32_tp_le16(sec + 0x738))
			memcpy(t33_ccm_v, t33_ccm_save, sizeof(t33_ccm_save));
		/* no calc_para_refresh here: write_reg stores the unchanged matrix words; tispCcmRefresh */
		t33_gr_rerun(T33_MOD_CCM);
	}
	off = t32_iq_section(bin, bytes, T33_SEC_CLM, &len);
	coff = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &clen);
	if (off && len == T33_CLM_SEC_BYTES && coff && clen == T32_CSC_BYTES) {
		const u8 *sec = bin + off, *lut = bin + off + len;
		u32 lb = t32_iq_payload(sec, bytes - off, T33_SEC_CLM);

		seg = t33_clm_seg(sec, lut, (int)ct);
		d = (int)ct - t33_clm_ct_last;
		if (t33_clm_seg_last < 0)
			t33_clm_seg_last = t33_clm_seg(sec, lut, t33_clm_ct_last);
		if (!((seg == t33_clm_seg_last && !(seg & 1)) || (d < 0 ? -d : d) <= 1)) {
			t33_clm_ct_last = (int)ct;
			t33_clm_seg_last = seg;
			memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
			t33_gr_commit(T33_MOD_CLM, t33_clm_init(sec, lut, lb, bin + coff, (int)ct, t33_gr_tmp,
								T33_SLOT_WORDS));
		}
	}
out:
	mutex_unlock(&t33_gr_lock);
}

/*
 * LSF list 37 from the LCE section (tisp_lce_init / malloc_cfg): built at
 * init like the vendor's, not started at stream-on (the vendor runs it from
 * its LCE interrupt only).  Own frame: the Gauss tables are large.
 */
static noinline void t33_tisp_init_lsf(const u8 *sec, u32 width, u32 height)
{
	u32 *buf = t33_mod_buf(T33_MOD_LSF);

	if (!t33_lsf_pages)
		t33_lsf_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T33_LSF_ORDER);
	memcpy(t33_lsf_sec, sec, sizeof(t33_lsf_sec));
	/* tisp_lce_malloc_cfg: the histogram buffer always (irq 8 stores it into slot 5) */
	if (!t33_lsf_hist_pages) {
		t33_lsf_hist_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T33_LSF_HIST_ORDER);
		t33_lcepf_wbinv(t33_lsf_hist_pages, T33_LSF_HIST_BYTES);	/* read cached by irq 30 */
	}
	if (!buf || !t33_lsf_pages || !t33_lsf_hist_pages)
		return;
	t33_lsf_write_curve((u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_lsf_pages)));	/* tisp_lsf_write_curve */
	t33_lce_size(t33_lsf_sec, height, width);
	t33_mod_done(T33_MOD_LSF,
		     t33_lsf_init(t33_lsf_sec, 0, 0, (u32)virt_to_phys((void *)t33_lsf_pages),
				  (u32)virt_to_phys((void *)t33_lsf_hist_pages), buf, T33_SLOT_WORDS));
}

/*
 * LCE / LSF per frame (openimp #444, tx_isp_t33_lcepf.h): the vendor's
 * system irq 8 (tisp_lce_interrupt_static) and 30 (tisp_lsf_interrupt_static)
 * from our ISR, its event 0xe (tisp_lce_soft_process) on the T33 event queue.
 * The statistics part and the histogram buffer are read cached after an
 * invalidate (the vendor syncs the histogram buffer only); the curve
 * buffer and the lists are written uncached.
 * An irq 30 while the soft process is still queued merges into that run
 * (the vendor's event list drops events when it is full).
 */
static struct t33_lce_pf *t33_lcepf;
static DEFINE_SPINLOCK(t33_lcepf_lock);
static bool t33_lcepf_ready;
static struct work_struct t33_lcepf_w;
u32 t33_lcepf_irq8s, t33_lcepf_irq30s, t33_lcepf_curves, t33_lcepf_lce_runs, t33_lcepf_lsf_runs;

static void t33_lcepf_lock_irq(void)
{
	spin_lock_irq(&t33_lcepf_lock);
}

static void t33_lcepf_unlock_irq(void)
{
	spin_unlock_irq(&t33_lcepf_lock);
}

/* after a zeroing allocation: no dirty line may later overwrite what the engines write */
static void t33_lcepf_wbinv(unsigned long pages, u32 len)
{
#ifdef CONFIG_MIPS
	if (pages)
		dma_cache_sync(NULL, (void *)pages, len, DMA_BIDIRECTIONAL);
#endif
}

static void t33_lcepf_inv(const void *p, u32 len)
{
#ifdef CONFIG_MIPS
	dma_cache_sync(NULL, (void *)p, len, DMA_FROM_DEVICE);	/* private_dma_cache_sync(0, p, len, 2) */
#endif
}

static u32 *t33_lcepf_list(unsigned int id)
{
	if (!(t33_mod_built & (1ULL << id)) || !t33_mod_page[id])
		return NULL;
	return (u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_mod_page[id]));
}

/* tisp_rcfg_module_cfg_trig: the list into its table word, then its start bit (no busy check, as the vendor) */
static void t33_rcfg_trig_list(unsigned int id)
{
	u32 *table = (u32 *)CKSEG1ADDR(rcfg_pa);

	if (!rcfg_pages || !t33_lcepf_list(id))
		return;
	table[id] = virt_to_phys((void *)t33_mod_page[id]);
	wmb();
	isp_wr(id < 32 ? T33_RCFG_TRIG : T33_RCFG_TRIG_HI, 1U << (id & 31));
	(void)isp_rd(T33_RCFG_TRIG);
	t33_rcfg_runs++;
}

/* system irq 8, ISR context */
static void t33_lcepf_isr8(void)
{
	u32 idx, *l16, *l37, dummy[T33_LCE_SLOT_CURVE + 2];
	u8 *stat;

	if (!ACCESS_ONCE(t33_lcepf_ready))
		return;
	idx = isp_rd(T33_LCE_IDX_REG);
	l37 = t33_lcepf_list(T33_MOD_LSF);
	l16 = t33_lcepf_list(T33_MOD_LCE);
	if (!l37)
		return;
	stat = (u8 *)t33_lce_stat_pages;
	t33_lcepf_inv(stat + (idx & 3) * T33_LCE_STAT_PART, T33_LCE_STAT_PART);
	spin_lock(&t33_lcepf_lock);
	t33_lcepf_irq8(t33_lcepf, t33_lce_sec, idx, stat, (u32)virt_to_phys((void *)t33_lce_stat_pages),
		       (u32)virt_to_phys((void *)t33_lsf_hist_pages),
		       (u8 *)CKSEG1ADDR(virt_to_phys((void *)t33_lce_curve_pages)),
		       (u32)virt_to_phys((void *)t33_lce_curve_pages), l16 ? l16 : dummy, l37);
	spin_unlock(&t33_lcepf_lock);
	t33_lcepf_irq8s++;
	/* stIspInitPar sensors == 1 */
	if (l16) {
		t33_rcfg_trig_list(T33_MOD_LCE);
		t33_lcepf_lce_runs++;
	}
	t33_lcepf_irq8_lsf(l37);
	if (!isp_rd(T33_LSF_BUSY_REG)) {
		t33_rcfg_trig_list(T33_MOD_LSF);
		t33_lcepf_lsf_runs++;
	}
}

/* system irq 30, ISR context: the histograms (or curves), then event 0xe */
static void t33_lcepf_isr30(void)
{
	u32 d_irq, d_hist;
	u8 *hist;

	if (!ACCESS_ONCE(t33_lcepf_ready))
		return;
	d_irq = isp_rd(T33_LSF_DATAOUT_REG);
	hist = (u8 *)t33_lsf_hist_pages;
	t33_lcepf_inv(hist, T33_LSF_HIST_BYTES);
	d_hist = isp_rd(T33_LSF_DATAOUT_REG);
	spin_lock(&t33_lcepf_lock);
	t33_lcepf_irq30(t33_lcepf, t33_lce_sec, hist, d_irq, d_hist);
	spin_unlock(&t33_lcepf_lock);
	t33_lcepf_irq30s++;
	t33_evq_queue(&t33_lcepf_w);
}

/* event 0xe: tisp_lce_soft_process */
static void t33_lcepf_work(struct work_struct *w)
{
	if (!ACCESS_ONCE(t33_lcepf_ready))
		return;
	if (t33_lcepf_process(t33_lcepf, t33_lce_sec, isp_rd(T33_LSF_DATAOUT_REG),
			      (u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_lce_curve_pages) + T33_LCE_CURVE_HALF)))
		t33_lcepf_curves++;
}
static DECLARE_WORK(t33_lcepf_w, t33_lcepf_work);

/* tisp_lce_ev_update (AE event 6, process context): lists 16 / 37 words, uncached, under the irq 8 lock */
static u32 t33_lce_mode[2];	/* the vendor's lcemodeflag / lcemodecounter */
u32 t33_lcepf_evs;
static void t33_lcepf_ev(u32 lo, u32 hi, u32 bv)
{
	u32 *l16, *l37, dummy[0x30];

	if (!ACCESS_ONCE(t33_lcepf_ready))
		return;
	l37 = t33_lcepf_list(T33_MOD_LSF);
	l16 = t33_lcepf_list(T33_MOD_LCE);	/* not built in the LSF mode */
	if (!l37)
		return;
	if (t33_lce_sec[0x2fb] == 1 && t33_lce_sec[0xc8] == 1)
		t33_lcepf_inv((void *)t33_lsf_hist_pages, T33_LSF_HIST_BYTES);
	spin_lock_irq(&t33_lcepf_lock);
	t33_lcepf_ev_update(t33_lcepf, t33_lce_sec, lo, hi, bv, (u32)virt_to_phys((void *)t33_lce_curve_pages),
			    (u32)virt_to_phys((void *)t33_lsf_hist_pages), l16 ? l16 : dummy, l37, t33_lce_mode);
	spin_unlock_irq(&t33_lcepf_lock);
	t33_lcepf_evs++;
}

static void t33_lcepf_stop(void)
{
	ACCESS_ONCE(t33_lcepf_ready) = false;
	if (irqs_requested)
		synchronize_irq(isp_irq);
	cancel_work_sync(&t33_lcepf_w);
}

/* the lceInfo state of tisp_lce_init, after lists 16 / 37 (stream-on, process context) */
static void t33_lcepf_attach(u32 width, u32 height)
{
	t33_lcepf_stop();
	if (!t33_lcepf)
		t33_lcepf = vzalloc(sizeof(*t33_lcepf));
	if (!t33_lcepf || !t33_lce_curve_pages || !t33_lce_stat_pages || !t33_lsf_hist_pages ||
	    !(t33_mod_built & (1ULL << T33_MOD_LSF))) {
		isp_printf(1, "tx-isp-t33: per-frame LCE not started\n");
		return;
	}
	t33_lcepf_init(t33_lcepf, t33_lce_sec, width, height);
	t33_lcepf_irq8s = t33_lcepf_irq30s = t33_lcepf_curves = t33_lcepf_lce_runs = t33_lcepf_lsf_runs = t33_lcepf_evs = 0;
	ACCESS_ONCE(t33_lcepf_ready) = true;
}

static void t33_lcepf_free(void)
{
	t33_lcepf_stop();
	vfree(t33_lcepf);
	t33_lcepf = NULL;
}

static void t33_tisp_init(const u8 *bin, u32 bytes)
{
	u32 off, len, code, width, height;

	t33_gref_reset();
	t33_tisp_init_ae(bin, bytes);
	t33_tisp_init_awb(bin, bytes);
	off = t32_iq_section(bin, bytes, T33_SEC_BLC, &len);
	if (off && len == T33_BLC_SEC_BYTES && !t32_core_sensor_mbus(&code) && t32_mbus_to_bayer(code) >= 0)
		t33_mod_set_tmpl(T33_MOD_BLC, T33_TMPL(t33_blc_tmpl), tisp_w,
				 t33_blc_init(bin + off, (u32)t32_mbus_to_bayer(code), T33_INIT_GAIN, tisp_w));
	off = t32_iq_section(bin, bytes, T33_SEC_YDNS, &len);
	if (off && len == T33_YDNS_SEC_BYTES)
		t33_mod_set_tmpl(T33_MOD_YDNS, T33_TMPL(t33_ydns_tmpl), tisp_w,
				 t33_ydns_init(bin + off, T33_INIT_GAIN, tisp_w));
	off = t32_iq_section(bin, bytes, T33_SEC_SDNS, &len);
	if (off && len == T33_SDNS_SEC_BYTES)
		t33_mod_set_tmpl(T33_MOD_SDNS, T33_TMPL(t33_sdns_tmpl), tisp_w,
				 t33_sdns_init(bin + off, T33_INIT_GAIN, tisp_w));
	T33_MOD_VB(dpc, DPC, t33_dpc_v);
	T33_MOD_VB(dmsc, DMSC, t33_dmsc_v);
	T33_MOD_VB(ysp, YSP, t33_ysp_v);
	{
		u32 *buf, blc = 0, dg = 0;

		/* tisp_reg_read_t33(BLC list, slot 3): our BLC list word 4 (0x3044) */
		if ((t33_mod_built & (1ULL << T33_MOD_BLC)) && (buf = t33_mod_buf(T33_MOD_BLC)))
			blc = buf[4];
		/* and the DG list, slot 3 (0x4060) */
		if ((t33_mod_built & (1ULL << T33_MOD_DG)) && (buf = t33_mod_buf(T33_MOD_DG)))
			dg = buf[4];
		off = t32_iq_section(bin, bytes, T33_SEC_RDNS, &len);
		if (off && len == T33_RDNS_SEC_BYTES && (buf = t33_mod_buf(T33_MOD_RDNS)))
			t33_mod_done(T33_MOD_RDNS, t33_rdns_init(bin + off, T33_INIT_GAIN, T33_INIT_EV, blc,
								 dg, t33_rdns_v, buf, T33_SLOT_WORDS));
	}
	{
		u32 *buf;

		/* CCM: section 12 and its knots behind it, CT 5000 K, EV 0 (the vendor's init values) */
		off = t32_iq_section(bin, bytes, T33_SEC_CCM, &len);
		if (off && len == T33_CCM_SEC_BYTES && (buf = t33_mod_buf(T33_MOD_CCM)))
			t33_mod_done(T33_MOD_CCM, t33_ccm_init(bin + off, bin + off + len,
							       t32_iq_payload(bin + off, bytes - off, T33_SEC_CCM),
							       T33_CCM_INIT_CT, T33_CCM_INIT_EV, 0, t33_ccm_v, buf,
							       T33_SLOT_WORDS));
	}
	off = t32_iq_section(bin, bytes, T33_SEC_GAMMA, &len);
	if (off && len == T33_GAMMA_SEC_BYTES) {
		u32 *buf = t33_mod_buf(T33_MOD_GAMMA);

		if (buf)
			t33_mod_done(T33_MOD_GAMMA,
				     t33_gamma_init(bin + off, bin + off + len,
						    t32_iq_payload(bin + off, bytes - off, T33_SEC_GAMMA),
						    T33_CCM_INIT_EV, 0, buf, T33_SLOT_WORDS));
	}
	off = t32_iq_section(bin, bytes, T33_SEC_DEFOG, &len);
	if (off && len == T33_DEFOG_SEC_BYTES && !t32_core_sensor_size(&width, &height)) {
		u32 *buf = t33_mod_buf(T33_MOD_DEFOG);

		if (!t33_defog_pages)
			t33_defog_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T33_DEFOG_ORDER);
		/* a copy: weights for another size / zoom are recomputed into it */
		memcpy(t33_defog_sec, bin + off, sizeof(t33_defog_sec));
		if (t33_defog_weights_needed(t33_defog_sec, width, height)) {
			u8 *m = vzalloc(t33_defog_scratch_bytes(width, height));

			if (m)
				t33_defog_count_weight(t33_defog_sec, width, height, m);
			vfree(m);
		}
		if (buf && t33_defog_pages)
			t33_mod_done(T33_MOD_DEFOG, t33_defog_init(t33_defog_sec, width, height,
								   (u32)virt_to_phys((void *)t33_defog_pages),
								   buf, T33_SLOT_WORDS));
	}
	off = t32_iq_section(bin, bytes, T33_SEC_AF, &len);
	if (off && len == T33_AF_SEC_BYTES && !t32_core_sensor_size(&width, &height)) {
		u32 *buf = t33_mod_buf(T33_MOD_AF);

		if (!t33_af_pages)
			t33_af_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T33_AF_ORDER);
		memcpy(t33_af_sec, bin + off, sizeof(t33_af_sec));
		if (buf && t33_af_pages)
			t33_mod_done(T33_MOD_AF, t33_af_init(t33_af_sec, width, height,
							     (u32)virt_to_phys((void *)t33_af_pages), buf,
							     T33_SLOT_WORDS));
	}
	off = t32_iq_section(bin, bytes, T33_SEC_LCE, &len);
	if (off && len == T33_LCE_SEC_BYTES && !t32_core_sensor_size(&width, &height)) {
		u32 *buf = t33_mod_buf(T33_MOD_LCE);

		if (!t33_lce_curve_pages)
			t33_lce_curve_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T33_LCE_CURVE_ORDER);
		if (!t33_lce_stat_pages)
			t33_lce_stat_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T33_LCE_STAT_ORDER);
		/* EV / BV 0 as the vendor's init; the curve written uncached */
		memcpy(t33_lce_sec, bin + off, sizeof(t33_lce_sec));
		t33_lcepf_wbinv(t33_lce_stat_pages, T33_LCE_STAT_BYTES);	/* read cached by irq 8 */
		if (buf && t33_lce_curve_pages && t33_lce_stat_pages) {
			t33_lce_curve_default((u8 *)CKSEG1ADDR(virt_to_phys((void *)t33_lce_curve_pages)));
			t33_mod_done(T33_MOD_LCE,
				     t33_lce_init(t33_lce_sec, width, height, 0, 0,
						  (u32)virt_to_phys((void *)t33_lce_curve_pages),
						  (u32)virt_to_phys((void *)t33_lce_stat_pages), buf, T33_SLOT_WORDS));
		}
		t33_tisp_init_lsf(bin + off, width, height);
		t33_lcepf_attach(width, height);
	}
	t33_tisp_init_tone(bin, bytes);
	t33_tisp_init_mdns(bin, bytes);
	off = t32_iq_section(bin, bytes, T33_SEC_LSC, &len);
	if (off && len == T33_LSC_SEC_BYTES && !t32_core_sensor_size(&width, &height)) {
		u32 *buf = t33_mod_buf(T33_MOD_LSC);

		if (!t33_lsc_pages)
			t33_lsc_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO, T33_LSC_ORDER);
		/* CT 5000 K as the CCM; the table written uncached */
		if (buf && t33_lsc_pages)
			t33_mod_done(T33_MOD_LSC,
				     t33_lsc_init(bin + off, bin + off + len,
						  t32_iq_payload(bin + off, bytes - off, T33_SEC_LSC), height, width,
						  T33_INIT_GAIN, T33_CCM_INIT_CT, t33_lsc_v, &t33_lsc_m,
						  (u8 *)CKSEG1ADDR(virt_to_phys((void *)t33_lsc_pages)),
						  (u32)virt_to_phys((void *)t33_lsc_pages), buf, T33_SLOT_WORDS));
	}
}

/*
 * lib_tisp_refresh_para's block part on the T33 (openimp #504).  libimp
 * loads the other IQ bin (day / night, IMP_ISP_Tuning_SetISPRunningMode)
 * and hands it over with SET_TIZIANO_PAR; while the ISP runs
 * (ispInitFinish) the vendor calls every block's *_switch_para, which
 * points the block at its section of the new bin and rebuilds its
 * registers at the block's current gain / EV / CT.  Here each list is
 * rebuilt with its init builder from the new bin at the last AE / AWB
 * values (t33_sw_*), copied into the list page when idle and run at the
 * next frame end (TOP: list 33 from the frame end, LCE / LSF: lists 16 /
 * 37 from the next irq 8).  Vendor order: top, dpc, blc, ae, awb, lsc,
 * tmo, dmsc, gamma, ccm, clm, bcsh, ysp, mdns, ydns, sdns, cdns, defog,
 * lce, rdns, af, csccr; the gain blocks (dpc, dmsc, ysp, mdns, ydns,
 * sdns, cdns, rdns) come from one forced t33_tgain_refresh after blc.
 * Not rebuilt (open, #504): BCSH (its switch: the init
 * builder resets the block state, the night list differs), the AWB list 29 / AWB and AE algorithm
 * parameters (tisp_awb_dn_params_refresh, the AE context), the AE list 30
 * (the five AE registers of the AE switch), the night byte 0x1f of
 * tparamsP and the TOP direct writes.
 */
static void t33_sw_commit_w(unsigned int id, unsigned int n)
{
	struct t32_rcfg_list l;

	memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
	t33_gr_commit(id, n ? t33_rcfg_build(&l, t33_gr_tmp, T33_SLOT_WORDS, tisp_w, n) : 0);
}

#define T33_SW_SEC(BLK)	((off = t32_iq_section(bin, bytes, T33_SEC_##BLK, &len)) && len == T33_##BLK##_SEC_BYTES)
#define T33_SW_TMP()	memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4)

/* tisp_top_switch_para: tisp_top_reg_cfg into list 33, tispTopRefresh */
static void t33_sw_top(const u8 *bin, u32 bytes, u32 width, u32 height, int bayer)
{
	struct t32_front_write dw[T33_TOP_DIRECT_WRITES];
	struct t32_rcfg_list l;
	unsigned long flags;
	u32 off, len, *buf;
	int n;

	off = t32_iq_section(bin, bytes, T32_IQ_SEC_TOP, &len);
	if (!off || len != T33_TOP_BYTES || bayer < 0 || !(t33_mod_built & (1ULL << T33_MOD_TOP)) ||
	    !(buf = t33_mod_buf(T33_MOD_TOP)))
		return;
	t33_top_init(bin + off, width, height, (u32)bayer, 0, tisp_w, dw);
	if (t32_ldc_top_hldc())
		tisp_w[0].val &= ~0x800000U;
	T33_SW_TMP();
	n = t33_rcfg_build(&l, t33_gr_tmp, T33_SLOT_WORDS, tisp_w, T33_TOP_LIST_WRITES);
	if (n <= 0)
		return;
	t32_wait_clear(T33_RCFG_TRIG_HI, 1U << (T33_MOD_TOP - 32), 2000);
	local_irq_save(flags);
	memcpy(buf, t33_gr_tmp, T33_SLOT_WORDS * 4);
	t33_top_byp = tisp_w[0].val;
	t33_top_sw = true;
	local_irq_restore(flags);
	t33_gr_runs[T33_MOD_TOP]++;
}

/* tisp_lce_switch_para: the new section 15, then the EV part at the last EV / BV (lists 16 / 37) */
static void t33_sw_lce(const u8 *bin, u32 bytes)
{
	u32 off, len, *l16, *l37, dummy[0x30];

	if (!T33_SW_SEC(LCE) || !ACCESS_ONCE(t33_lcepf_ready) || !(l37 = t33_lcepf_list(T33_MOD_LSF)))
		return;
	l16 = t33_lcepf_list(T33_MOD_LCE);
	spin_lock_irq(&t33_lcepf_lock);
	memcpy(t33_lce_sec, bin + off, sizeof(t33_lce_sec));
	t33_lcepf_switch_para(t33_lcepf, t33_lce_sec,
			      (u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_lce_curve_pages) + T33_LCE_CURVE_HALF),
			      (u32)virt_to_phys((void *)t33_lce_curve_pages),
			      (u32)virt_to_phys((void *)t33_lsf_hist_pages), l16 ? l16 : dummy, l37, t33_lce_mode);
	spin_unlock_irq(&t33_lcepf_lock);
	t33_gr_runs[T33_MOD_LCE]++;
}

/* lsc, tmo, gamma, ccm */
static noinline_for_stack void t33_sw_tone(const u8 *bin, u32 bytes, u32 width, u32 height, u32 gain, u32 ct, u32 lo,
					   u32 hi)
{
	unsigned long long ev = (unsigned long long)hi << 32 | lo;
	u32 off, len;

	/* tisp_lsc_switch_para: interp_by_tgain / _by_ct (forced), write_reg */
	if (T33_SW_SEC(LSC) && t33_lsc_pages) {
		T33_SW_TMP();
		t33_gr_commit(T33_MOD_LSC,
			      t33_lsc_init(bin + off, bin + off + len, t32_iq_payload(bin + off, bytes - off, T33_SEC_LSC),
					   height, width, gain, ct, t33_lsc_v, &t33_lsc_m,
					   (u8 *)CKSEG1ADDR(virt_to_phys((void *)t33_lsc_pages)),
					   (u32)virt_to_phys((void *)t33_lsc_pages), t33_gr_tmp, T33_SLOT_WORDS));
	}
	/* tisp_tmo_switch_para */
	if (T33_SW_SEC(TMO) && t33_tmo_pages) {
		T33_SW_TMP();
		t33_gr_commit(T33_MOD_TMO, t33_tmo_init(bin + off, width, height, ev,
							(u32)virt_to_phys((void *)t33_tmo_pages), t33_tmo_v,
							t33_gr_tmp, T33_SLOT_WORDS));
	}
	/* tisp_gamma_switch_para: interp_by_ev (forced), update_curve, write_lut */
	if (T33_SW_SEC(GAMMA)) {
		T33_SW_TMP();
		t33_gr_commit(T33_MOD_GAMMA, t33_gamma_init(bin + off, bin + off + len,
							    t32_iq_payload(bin + off, bytes - off, T33_SEC_GAMMA),
							    lo, hi, t33_gr_tmp, T33_SLOT_WORDS));
	}
	/* tisp_ccm_switch_para: interp_by_ct (forced) / _by_ev, calc_para_refresh, write_reg */
	if (T33_SW_SEC(CCM)) {
		T33_SW_TMP();
		t33_gr_commit(T33_MOD_CCM, t33_ccm_init(bin + off, bin + off + len,
							t32_iq_payload(bin + off, bytes - off, T33_SEC_CCM), ct, lo, hi,
							t33_ccm_v, t33_gr_tmp, T33_SLOT_WORDS));
	}
}

/* clm */
static noinline_for_stack void t33_sw_color(const u8 *bin, u32 bytes)
{
	u32 off, len, coff, clen;

	coff = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &clen);
	/* tisp_clm_switch_para: ct_interp (forced) at the block's last CT (clm_info word 0x18), calc_para_refresh, write_reg */
	if (T33_SW_SEC(CLM) && coff && clen == T32_CSC_BYTES) {
		T33_SW_TMP();
		t33_clm_seg_last = t33_clm_seg(bin + off, bin + off + len, t33_clm_ct_last);
		t33_gr_commit(T33_MOD_CLM, t33_clm_init(bin + off, bin + off + len,
							t32_iq_payload(bin + off, bytes - off, T33_SEC_CLM), bin + coff,
							t33_clm_ct_last, t33_gr_tmp, T33_SLOT_WORDS));
	}
}

/* defog, af, csccr */
static noinline_for_stack void t33_sw_space(const u8 *bin, u32 bytes, u32 width, u32 height)
{
	u32 off, len;
	int r, i;

	/* tisp_defog_switch_para: weights for the size, write_reg_para1..3 */
	if (T33_SW_SEC(DEFOG) && t33_defog_pages) {
		memcpy(t33_defog_sec, bin + off, sizeof(t33_defog_sec));
		if (t33_defog_weights_needed(t33_defog_sec, width, height)) {
			u8 *m = vzalloc(t33_defog_scratch_bytes(width, height));

			if (m)
				t33_defog_count_weight(t33_defog_sec, width, height, m);
			vfree(m);
		}
		T33_SW_TMP();
		t33_gr_commit(T33_MOD_DEFOG, t33_defog_init(t33_defog_sec, width, height,
							    (u32)virt_to_phys((void *)t33_defog_pages),
							    t33_gr_tmp, T33_SLOT_WORDS));
	}
	/* tisp_af_switch_para: af_par_init from the new section, all_reg_write */
	if (T33_SW_SEC(AF) && t33_af_pages) {
		memcpy(t33_af_sec, bin + off, sizeof(t33_af_sec));
		T33_SW_TMP();
		t33_gr_commit(T33_MOD_AF, t33_af_init(t33_af_sec, width, height, (u32)virt_to_phys((void *)t33_af_pages),
						      t33_gr_tmp, T33_SLOT_WORDS));
	}
	/* tisp_csccr_switch_para: para_refresh_by_mode, bin_par_update, write_reg */
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_CSCCR, &len);
	if (off && T32_IQ_SEC_OK(len, T32_CSCCR_BYTES) && (r = t32_csccr_init(bin + off, tisp_w)) > 0) {
		for (i = 0; i < r; i++)
			if (tisp_w[i].reg == 0x14010)
				tisp_w[i].val = T33_CSCCR_EN;
		t33_sw_commit_w(T33_MOD_CSCCR, (unsigned int)r);
	}
}

void t33_refresh_para(u32 mode)
{
	const u8 *bin;
	u32 bytes, off, len, width, height, code, gain, ct, lo, hi;
	unsigned int id;
	int bayer = -1;

	if (t32_core_iq_bin(&bin, &bytes) || !tisp_w || !t33_mod_built || t32_core_sensor_size(&width, &height))
		return;
	if (!t32_core_sensor_mbus(&code))
		bayer = t32_mbus_to_bayer(code);
	gain = ACCESS_ONCE(t33_sw_gain);
	ct = ACCESS_ONCE(t33_sw_ct);
	mutex_lock(&t33_gr_lock);
	if (!t33_gr_tmp)
		t33_gr_tmp = kmalloc(T33_SLOT_WORDS * 4, GFP_KERNEL);
	if (!t33_gr_tmp) {
		mutex_unlock(&t33_gr_lock);
		return;
	}
	lo = t33_sw_ev_lo;
	hi = t33_sw_ev_hi;
	t33_sw_top(bin, bytes, width, height, bayer);
	/* tisp_blc_switch_para: interp_by_tgain at the current gain, write_reg */
	if (T33_SW_SEC(BLC) && bayer >= 0)
		t33_gr_commit(T33_MOD_BLC, t33_gr_tmpl(T33_TMPL(t33_blc_tmpl),
						       t33_blc_init(bin + off, (u32)bayer, gain, tisp_w)));
	t33_sw_tone(bin, bytes, width, height, gain, ct, lo, hi);
	t33_sw_color(bin, bytes);
	t33_sw_space(bin, bytes, width, height);
	t33_sw_lce(bin, bytes);
	/* the gain blocks at the current gain, every gate open (dpc, dmsc, ysp, mdns, ydns, sdns, cdns, rdns) */
	for (id = 0; id < T33_MODULES; id++)
		t33_gr_last[id] = 0xffffffffU;
	t33_sw_runs++;
	mutex_unlock(&t33_gr_lock);
	t33_tgain_refresh(gain);
	isp_printf(0, "tx-isp-t33: IQ bin switch (mode %u): lists rebuilt at gain %#x ct %u ev %#x:%#x\n",
		   mode, gain, ct, hi, lo);
}
#undef T33_SW_SEC
#undef T33_SW_TMP
#endif

static void t32_tisp_init(void)
{
	const u8 *bin;
	u32 bytes, off, len, coff, width, height, code;
	unsigned int mode;

	if (t32_core_iq_bin(&bin, &bytes))
		return;
	if (!tisp_w)
		tisp_w = kmalloc_array(T32_TISP_MAX_WRITES, sizeof(*tisp_w), GFP_KERNEL);
	if (!tisp_w)
		return;
	off = t32_iq_section(bin, bytes, T32_SEC_DMSC, &len);
	if (off && T32_IQ_SEC_OK(len, T32_DMSC_SEC_BYTES)) {
		u8 *cp = kmemdup(bin + off, len, GFP_KERNEL);	/* the block edits its section */

		if (cp) {
			t32_tisp_flush("dmsc", t32_dmsc_init(cp, tisp_w));
			t32_pf_attach(T32_PP_DMSC, cp);	/* the per-frame context keeps the edited copy */
		}
	}
	off = t32_iq_section(bin, bytes, T32_SEC_GAMMA, &len);
	if (off && T32_IQ_SEC_OK(len, T32_GAMMA_SEC_BYTES))
		t32_tisp_flush("gamma", t32_gamma_init(bin + off, bin + off + len, bytes - off - len, tisp_w));
	off = t32_iq_section(bin, bytes, T32_SEC_CDNS, &len);
#ifdef TX_ISP_T33
	if (off && len == T33_CDNS_SEC_BYTES)
		t33_mod_set(T33_MOD_CDNS, tisp_w, t33_cdns_init(bin + off, tisp_w));
	t33_tisp_init(bin, bytes);
#else
	if (off && T32_IQ_SEC_OK(len, T32_CDNS_SEC_BYTES)) {
		t32_tisp_flush("cdns", t32_cdns_init(bin + off, tisp_w));
		t32_pf_attach(T32_PP_CDNS, kmemdup(bin + off, len, GFP_KERNEL));
	}
#endif
	off = t32_iq_section(bin, bytes, T32_SEC_GIB, &len);
	if (off && T32_IQ_SEC_OK(len, T32_GIB_SEC_BYTES) && !t32_core_sensor_mbus(&code) && t32_mbus_to_bayer(code) >= 0) {
		u8 *cp = kmemdup(bin + off, len, GFP_KERNEL);	/* the block stores into its section */

		if (cp) {
			int n = t32_gib_init(cp, (u32)t32_mbus_to_bayer(code), tisp_w);

			t32_tisp_flush("gib", n);
			if (n > 0)	/* the per-frame context keeps the edited copy */
				t32_pf_attach_gib(cp, (u32)t32_mbus_to_bayer(code));
			else
				kfree(cp);
		}
	}
	off = t32_iq_section(bin, bytes, T32_SEC_DPC, &len);
	if (off && T32_IQ_SEC_OK(len, T32_DPC_SEC_BYTES)) {
		t32_tisp_flush("dpc", t32_dpc_init(bin + off, 0, tisp_w));	/* wide: ISP reg 0x88 & 0x1f > 4, never with this front end */
		t32_pf_attach(T32_PP_DPC, kmemdup(bin + off, len, GFP_KERNEL));
	}
	off = t32_iq_section(bin, bytes, T32_SEC_YDNS, &len);
	if (off && T32_IQ_SEC_OK(len, T32_YDNS_SEC_BYTES)) {
		t32_tisp_flush("ydns", t32_ydns_init(bin + off, tisp_w));
		t32_pf_attach(T32_PP_YDNS, kmemdup(bin + off, len, GFP_KERNEL));
	}
	off = t32_iq_section(bin, bytes, T32_SEC_CCM, &len);
	if (off && T32_IQ_SEC_OK(len, T32_CCM_SEC_BYTES) && t32_pp_on(T32_PP_CT)) {
		/* the state kept for the ct fan-out (tisp_ct_update after each AWB frame, openimp #234) */
		if (t32_ctup_attach_ccm(bin + off, len, bytes - off - len))
			isp_printf(2, "tx-isp-t32: CCM section refused\n");
	} else if (off && T32_IQ_SEC_OK(len, T32_CCM_SEC_BYTES)) {
		u8 *cp = kmemdup(bin + off, len, GFP_KERNEL);	/* the block clears two bytes of its section */

		if (cp) {
			t32_tisp_flush("ccm", t32_ccm_init(cp, bin + off + len, bytes - off - len, tisp_w));
			kfree(cp);
		}
	}
	off = t32_iq_section(bin, bytes, T32_SEC_BCSH, &len);
	if (off && T32_IQ_SEC_OK(len, T32_BCSH_SEC_BYTES)) {
		u32 clen = 0, coff = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &clen);
		u8 *cp = kmemdup(bin + off, len, GFP_KERNEL);	/* the block clears ten bytes of its section */

		if (cp && coff && clen >= T32_BCSH_CSC_BYTES && t32_pp_on(T32_PP_CT)) {
			if (t32_ctup_attach_bcsh(bin + off, len, bytes - off - len, bin + coff))
				isp_printf(2, "tx-isp-t32: BCSH section refused\n");
		} else if (cp && coff && clen >= T32_BCSH_CSC_BYTES)
			t32_tisp_flush("bcsh", t32_bcsh_init(cp, bin + off + len, bytes - off - len, bin + coff, tisp_w));
		kfree(cp);
	}
	off = t32_iq_section(bin, bytes, T32_SEC_LCE, &len);
	if (off && T32_IQ_SEC_OK(len, T32_LCE_SEC_BYTES) && !t32_core_sensor_size(&width, &height)) {
		t32_tisp_flush("lce", t32_lce_build(bin + off, width, height));
		t32_pf_attach_lce(bin + off, width, height);	/* per-frame state (tisp_lce_init) */
	}
	if (!t32_core_sensor_size(&width, &height)) {
		t32_afgsm_attach_af(bin, bytes, width, height);	/* tisp_af_init, bit 6 (tx_isp_t32_afgsm.c) */
		t32_tmo_attach(bin, bytes, width, height);	/* tisp_tmo_init, bits 9 / 31 (tx_isp_t32_tmo.c) */
	}	/* tisp_tmo_init, bits 9 / 31 (tx_isp_t32_tmo.c) */
	off = t32_iq_section(bin, bytes, T32_SEC_SDNS, &len);
	if (off && T32_IQ_SEC_OK(len, T32_SDNS_SEC_BYTES)) {
		t32_tisp_flush("sdns", t32_sdns_init(bin + off, tisp_w));
		t32_pf_attach(T32_PP_SDNS, kmemdup(bin + off, len, GFP_KERNEL));
	}
	off = t32_iq_section(bin, bytes, T32_SEC_YSP, &len);
	if (off && T32_IQ_SEC_OK(len, T32_YSP_SEC_BYTES)) {
		u8 *cp = kmemdup(bin + off, len, GFP_KERNEL);

		if (cp) {
			t32_tisp_flush("ysp", t32_ysp_init(cp, tisp_w));
			t32_pf_attach(T32_PP_YSP, cp);
		}
	}
	off = t32_iq_section(bin, bytes, T32_SEC_MDNS, &len);
	if (off && T32_IQ_SEC_OK(len, T32_MDNS_SEC_BYTES) && !t32_core_sensor_size(&width, &height)) {
		u8 *cp = kmemdup(bin + off, len, GFP_KERNEL);	/* the block stores into its section */

		if (cp) {
			t32_tisp_flush("mdns", t32_mdns_build(cp, width, height));
			kfree(cp);
		}
	}
	coff = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &len);
	off = t32_iq_section(bin, bytes, T32_SEC_CLM, &len);
	if (coff && off && T32_IQ_SEC_OK(len, T32_CLM_SEC_BYTES) && bin[coff + 0x5b] < 6) {
		mode = bin[coff + 0x5b];
		t32_tisp_flush("clm", t32_clm_init(bin + off, bin + off + len, bytes - off - len, mode,
						   t32_csc_modes[mode].m, tisp_w));
		if (t32_pp_on(T32_PP_CT) && t32_ctup_attach_clm(bin + off, len, bytes - off - len))
			isp_printf(2, "tx-isp-t32: CLM state for the ct fan-out refused\n");
	}
	if (!t32_core_sensor_size(&width, &height))
		t32_afgsm_attach_gsm(width, height);	/* tisp_gsm_init, bits 7 / 63 (tx_isp_t32_afgsm.c) */
	off = t32_iq_section(bin, bytes, T32_SEC_RDNS, &len);
	if (off && len >= T32_RDNS_SEC_BYTES && t32_pp_on(T32_PP_RDNS))
		t32_tisp_flush("rdns", t32_pf_attach_rdns(kmemdup(bin + off, len, GFP_KERNEL), tisp_w));
}

/*
 * Bring-up controller, NOT the vendor AE: with bringup_ae=1 every AE
 * histogram interrupt schedules one proportional integration time step
 * towards bringup_target (mean histogram bin, 0..255), gain untouched.  It
 * exists only to see the statistics close a loop through the sensor until
 * the vendor AE flow is reimplemented; no tuning goes into it.
 */
static unsigned int bringup_ae;
module_param(bringup_ae, uint, 0644);
MODULE_PARM_DESC(bringup_ae, "1: bring-up exposure controller (not the vendor AE), default 0");
static unsigned int bringup_target = 64;
module_param(bringup_target, uint, 0644);
MODULE_PARM_DESC(bringup_target, "bring-up controller target mean AE histogram bin (0..255)");

static void t32_bringup_ae_work(struct work_struct *w)
{
	u32 idx = isp_rd(T32_AE_CUR) & 3, vts = t32_core_sensor_vts(), it, mean16;
	unsigned long pa;
	const u8 *h;

	if (!ae_buf || vts < 8)
		return;
	pa = virt_to_phys((void *)ae_buf) + 0x4000 + idx * 0x800 + T32_AE_HIST_HEAD;
#ifdef CONFIG_MIPS
	h = (const u8 *)CKSEG1ADDR(pa);		/* uncached: the engine writes by DMA */
#else
	h = phys_to_virt(pa);
#endif
	mean16 = t32_ae_hist_mean16(h);
	it = t32_hw_stats.bringup_it ? t32_hw_stats.bringup_it : vts / 2;
	it = t32_bringup_ae_step(it, mean16, min(bringup_target, 255U) * 16, vts - 4);
	t32_hw_stats.bringup_mean16 = mean16;
	if (it != t32_hw_stats.bringup_it && !t32_core_sensor_expo(it)) {
		t32_hw_stats.bringup_it = it;
		t32_hw_stats.bringup_steps++;
	}
}
static DECLARE_WORK(t32_bringup_ae_w, t32_bringup_ae_work);

static void t32_bringup_ae_kick(void)
{
#ifdef TX_ISP_T33
	return;		/* it reads the T32 statistics layout */
#endif
	if (bringup_ae)
		schedule_work(&t32_bringup_ae_w);
}

/*
 * The stream-on list: the LSC block from the IQ bin (tx_isp_t32_lsc.h), as
 * the vendor's list carries it (identical word for word for the cv8001
 * PRJ007 bin).  Without a bin, or one without an LSC block, it stays empty.
 */
static void t32_rcfg_stream_on_fill(struct t32_rcfg_list *l)
{
	const u8 *bin, *p;
	struct t32_lsc s;
	u32 bytes, w, h, code;
	int bayer;

	if (t32_core_iq_bin(&bin, &bytes) || t32_core_sensor_size(&w, &h) || t32_core_sensor_mbus(&code))
		return;
	bayer = t32_mbus_to_bayer(code);
	if (bayer < 0)
		return;
	p = t32_lsc_block(bin, bytes);
	if (!p || t32_lsc_setup(p, &s, &lsc_work, w, h, lsc_ct)) {
		isp_printf(1, "tx-isp-t32: no usable LSC block in the IQ bin, LSC off\n");
		return;
	}
	isp_wr(T32_LSC_ID_REG, T32_LSC_ID);
	if (t32_lsc_fill_list(p, &s, &lsc_work, l, lsc_scratch, (u32)bayer) == 0) {
		isp_printf(0, "tx-isp-t32: LSC %ux%u cells %u table %u/%u at %u K\n",
			   s.cols, s.rows, s.cells, s.table_lo, s.table_hi, lsc_ct);
		t32_pf_attach_lsc(p, &s, w, h, (u32)bayer, lsc_ct);	/* per-frame strength (tisp_lsc_tgain_update) */
		t32_lsc_drv_attach(p, &s, &lsc_work, w, h, (u32)bayer, lsc_ct);	/* ct path (tisp_lsc_ct_update) */
	}
}

void t32_hw_rcfg_run_stream_on(void)
{
	u32 *table, *list;
	struct t32_rcfg_list l;
	u32 list_pa;

	if (!rcfg_pages)
		return;
	/* the core reads both through DMA: write them uncached */
	table = (u32 *)CKSEG1ADDR(rcfg_pa);
	list = table + T32_RCFG_TABLE_WORDS;
	list_pa = rcfg_pa + T32_RCFG_TABLE_WORDS * 4;
	t32_rcfg_init(&l, list, T32_RCFG_LIST_WORDS);
	t32_rcfg_stream_on_fill(&l);
	if (l.overflow) {
		isp_printf(2, "tx-isp-t32: stream-on register list overflow\n");
		t32_rcfg_init(&l, list, T32_RCFG_LIST_WORDS);
	}
	memset(table, 0, T32_RCFG_TABLE_WORDS * 4);
	table[0] = list_pa;
	table[T32_RCFG_STREAM_ON_LIST] = list_pa;
	wmb();
	isp_wr(T32_RDMA_TABLE, rcfg_pa);
	isp_wr(T32_RDMA_TRIG, T32_RCFG_STREAM_ON_BIT);
	isp_wr(T32_RDMA_TRIG2, T32_RCFG_STREAM_ON_BIT);
	if (t32_wait_clear(T32_RDMA_TRIG, T32_RDMA_BUSY_MASK, 1000)) {
		t32_rcfg_timeouts++;
		isp_printf(2, "tx-isp-t32: stream-on register list did not finish\n");
	}
	t32_rcfg_runs++;
	t32_rcfg_last_len = list[0];
}

/*
 * tisp_single_lsc_rcfg_cfg_trig for the LSC list of tx_isp_t32_lscct.c (uncached, so
 * nothing to flush): table word 49 and word 0, the table, bit 17 in 0x22014 / 0x2201c,
 * then 0x2c = 2.  Not waited for: the next writer checks 0x22014 (vendor).  Any context.
 */
void t32_hw_rcfg_lsc_trig(u32 list_pa)
{
	u32 *table;

	if (!rcfg_pages)
		return;
	table = (u32 *)CKSEG1ADDR(rcfg_pa);
	table[T32_RCFG_STREAM_ON_LIST] = list_pa;
	table[0] = list_pa;
	wmb();
	isp_wr(T32_RDMA_TABLE, rcfg_pa);
	isp_wr(T32_RDMA_TRIG, T32_RCFG_STREAM_ON_BIT);
	isp_wr(T32_RDMA_TRIG2, T32_RCFG_STREAM_ON_BIT);
	isp_wr(0x002c, 2);
}

static struct t32_csi_cfg csi_cfg;
static int csi_reset_done;

/*
 * VIC input configuration, written by the vendor at S_INPUT (tisp init):
 * both VICs reset, then VIC0 programmed from the sensor attributes
 * (tx_isp_t32_vic.h), then the MSCA input size.
 */
int t32_hw_input_setup(const void *attr, u32 w, u32 h)
{
#ifdef TX_ISP_T33
	/* lib_tisp_init -> tisp_top_ipc_init (one sensor) */
	isp_wr(0x1080, 0);
	isp_wr(0x1084, 0);
	isp_wr(0x10a0, 0xffffffff);
	isp_wr(0x10a4, 4);
	isp_wr(0x10c4, w * h / 6);
	isp_wr(0x10c8, w * h / 6);
#endif
	struct t32_reg_seq seq;
	u32 size = (w << 16) | h;
	unsigned int i;
	int ret;

	ret = t32_vic_input_seq(attr, w, h, &seq);
	if (!ret)
		ret = t32_csi_cfg_from_attr(attr, &csi_cfg);
	if (ret) {
		isp_printf(2, "tx-isp-t32: sensor interface not supported (dbus %u, csi %u, fmt %u): %d\n",
			   t32_attr_u32(attr, T32_ATTR_DBUS_TYPE),
			   t32_attr_u8(attr, T32_ATTR_MIPI_INDEX),
			   t32_attr_u32(attr, T32_ATTR_SC_CSI_FMT), ret);
		return ret;
	}
	isp_wr(T32_RDMA_IRQ_EN, 0xffffffff);
	isp_wr(T32_RDMA_IRQ_CLR, 0xffffffff);
	isp_wr(T32_CORE_IDLE_REQ, 1);
	isp_wr(0x0024, 0x40000001);
	if (T32_SOC_LIST_IRQ) {
		isp_wr(T32_INT5_EN, 0xffffffff);
		isp_wr(T32_INT4_EN, 0xffffffff);
		t33_rcfg_core_init();
	} else {
		isp_wr(0x0070, size);
		isp_wr(0x0078, 0x100);
	}
	isp_wr(0x0080, size);
	isp_wr(0x0088, 0x100);
	t32_vic_wr(0, 0x1a8, 0x4444);
	t32_vic_wr(1, 0x1a8, 0x4444);
	/* interrupts off (the vendor's reset and its interrupt disable both
	 * write it: twice, as in the reference trace) */
	t32_vic_wr(0, T32_VIC_INT_CFG, 0);
	t32_vic_wr(0, T32_VIC_INT_CFG, 0);
	t32_vic_wr(1, T32_VIC_INT_CFG, 0);
	t32_vic_wr(1, T32_VIC_INT_CFG, 0);
	t32_vic_wr(0, 0x018, 0);
	t32_vic_wr(1, 0x018, 0);
	t32_vic_wr(0, T32_VIC_CTRL, 4);		/* reset */
	t32_vic_wr(1, T32_VIC_CTRL, 4);
	if (t32_wait_clear(T32_VIC_RESET_BUSY, 0xf, 1000))
		isp_printf(2, "tx-isp-t32: VIC reset did not finish\n");
	for (i = 0; i < seq.n; i++)
		t32_vic_wr(0, seq.w[i].off, seq.w[i].val);
	/* MSCA input size and channel defaults (T33: list 23 carries them) */
	if (!T32_SOC_LIST_IRQ) {
		isp_wr(0xf0404, size);
		isp_wr(0xf0090, 0);
		isp_wr(0xf0094, size);
		isp_wr(T32_MSCA_CH_EN2, 0);
		isp_wr(T32_MSCA_CH_EN, 0);
		isp_wr(T32_MSCA_UPDATE, 1);
	}
	isp_wr(T32_CORE_IDLE_REQ, 0);
	isp_printf(0, "tx-isp-t32: VIC %ux%u fmt %u line %u words, CSI %u lanes settle %#x\n",
		   w, h, t32_attr_u32(attr, T32_ATTR_SC_CSI_FMT), seq.w[1].val,
		   csi_cfg.lanes_m1 + 1, csi_cfg.settle);
	return 0;
}

static void t32_csi_start(void)
{
	int i;

	/* the host is reset once per load (vendor: first stream-on only) */
	if (!csi_reset_done) {
		writel(0, csi_base + 0x0c);
		msleep(1);
		writel(1, csi_base + 0x0c);
		csi_reset_done = 1;
	}
	writel(csi_cfg.lanes_m1, csi_base + 0x04);
	writel(csi_cfg.host_100, csi_base + 0x100);
	writel(0, csi_base + 0x08);
	writel(0, csi_base + 0x10);
	writel(1, csi_base + 0x10);
	writel(0xffffffff, csi_base + 0x28);
	writel(0xffffffff, csi_base + 0x2c);
	writel(0x7d, dphy_base + 0x000);
	writel(0, csi_base + 0x80);
	writel(csi_cfg.phy_80, dphy_base + 0x080);
	for (i = 0; i < 6; i++)
		writel(csi_cfg.phy_lane, dphy_base + 0x128 + i * 0x80);
	for (i = 0; i < 5; i++)
		writel(csi_cfg.settle, dphy_base + 0x160 + i * 0x80);
}

static void t32_csi_stop(void)
{
	writel(0, csi_base + 0x10);
}

/*
 * STREAMON in the vendor's order: interrupts on, the stream-on register
 * list, core run bits, VIC interrupt setup, both VICs reset and run, then
 * the CSI host and D-PHY.
 */
void t32_hw_stream_on(void)
{
	isp_wr(T32_INT_EN, 0xffffffff);
	isp_wr(T32_CHDONE_EN, 0xffffffff);
	isp_wr(T32_INT2_EN, 0xffffffff);
	isp_wr(T32_MSCADONE_EN, 0xffffffff);
	isp_wr(T32_INT3_EN, 0xffffffff);
	if (T32_SOC_LIST_IRQ)
		isp_wr(T32_INT5_EN, 0xffffffff);
	isp_wr(T32_INT4_EN, 0xffffffff);
	t32_front_init();
	t32_pf_reset();		/* before the statistics init: it builds the AE context (t32_pf_attach_ae) */
	t32_stats_init();
	t32_tisp_init();
	if (T32_SOC_LIST_IRQ) {
		int ch;

#ifdef TX_ISP_T33
		{
			/* online LDC: list 25 in the vendor layout (tisp_rcfg_ldc_reg_init) */
			static const struct t33_tcmd ldc_tmpl[] = {
				{ 0x27040, 3, 0 }, { 0x2706c, 3, 0 }, { 0x27020, 2, 0 },
				{ 0x2104c, 2, 0 }, { 0x21084, 1, 0 }, { 0x20058, 2, 0 },
			};
			struct t32_front_write lw[T33_LDC_LIST_WRITES];
			unsigned int ln = t32_ldc_stream_list(lw);

			if (ln)
				t33_mod_set_tmpl(T33_MOD_LDC, T33_TMPL(ldc_tmpl), lw, (int)ln);
			t32_ldc_stream_on();
		}
#endif
		t33_rcfg_write_all();
		/* lib_tisp_msca_chx_reg_stream_off for the three channels */
		for (ch = 0; ch < 3; ch++)
			isp_wr(T32_MSCA_CH_EN, isp_rd(T32_MSCA_CH_EN) & ~(1U << ch));
	} else {
		t32_pf_start();
		t32_hw_rcfg_run_stream_on();
		isp_wr(0x002c, 2);
	}
	isp_wr(0x1040, 1);
	t32_vic_wr(0, T32_VIC_INT_MSK, 0);
	t32_vic_wr(0, T32_VIC_INT_MSK2, 0);
	t32_vic_wr(0, T32_VIC_INT_CFG, 0);
	t32_vic_wr(0, T32_VIC_INT_CFG, 0x80000011);
	t32_vic_wr(1, T32_VIC_INT_CFG, 0);
	t32_vic_wr(1, T32_VIC_INT_CFG, 0);
	t32_vic_wr(0, 0x018, 0);
	t32_vic_wr(1, 0x018, 0);
	t32_vic_wr(0, T32_VIC_CTRL, 4);
	t32_vic_wr(1, T32_VIC_CTRL, 4);
	t32_wait_clear(T32_VIC_RESET_BUSY, 0xf, 1000);
	t32_vic_wr(0, T32_VIC_CTRL, 1);
	t32_vic_wr(1, T32_VIC_CTRL, 1);
	t32_csi_start();
	t32_hw_stats.stream_ons++;
}

void t32_hw_stream_off(void)
{
#ifdef TX_ISP_T33
	t33_awbc_detach();
	t33_aec_detach();
#endif
	t32_vic_wr(0, T32_VIC_CTRL, 4);		/* stop + reset */
	t32_vic_wr(1, T32_VIC_CTRL, 4);
	t32_wait_clear(T32_VIC_RESET_BUSY, 0xf, 1000);
	t32_csi_stop();
	if (T32_SOC_LIST_IRQ) {
		t32_vic_wr(0, T32_VIC_INT_MSK, 0xffffffff);
		t32_vic_wr(0, T32_VIC_INT_MSK2, 0xffffffff);
	}
	isp_wr(T32_INT_EN, 0);
	isp_wr(T32_CHDONE_EN, 0);
	isp_wr(T32_INT2_EN, 0);
	isp_wr(T32_MSCADONE_EN, 0);
	isp_wr(T32_INT3_EN, 0);
	if (T32_SOC_LIST_IRQ)
		isp_wr(T32_INT5_EN, 0);
	isp_wr(T32_INT4_EN, 0);
	/* drop what is latched so a later start begins clean */
	isp_wr(T32_INT_CLR, isp_rd(T32_INT_STA));
	isp_wr(T32_CHDONE_CLR, isp_rd(T32_CHDONE_STA));
	isp_wr(T32_MSCADONE_CLR, isp_rd(T32_MSCADONE_STA));
	t32_vic_wr(0, T32_VIC_INT_CLR, readl(isp_base + T32_VIC_BASE(0) + T32_VIC_INT_STA));
	t32_vic_wr(0, T32_VIC_INT_CLR2, readl(isp_base + T32_VIC_BASE(0) + T32_VIC_INT_STA2));
	t32_lsc_drv_detach();	/* no more LSC list runs */
	t32_pf_stop();		/* per-frame contexts (the vmalloc'd LCE state) end with the stream */
}

/* ------------------------------------------------------------------ */
/* MSCA                                                               */

static inline void ch_wr(int ch, u32 off, u32 v)
{
	isp_wr(T32_MSCA_CH(ch) + off, v);
}

static inline u32 ch_rd(int ch, u32 off)
{
	return isp_rd(T32_MSCA_CH(ch) + off);
}

void t32_hw_msca_push(int ch, u32 y, u32 uv)
{
	ch_wr(ch, T32_MSCA_Y_PUSH, y);
	ch_wr(ch, T32_MSCA_UV_PUSH, uv);
	/* the T32 vendor writes 0 to the clears after a push: only bit 0
	 * clears (T33 single sensor: the two addresses only) */
	if (T32_SOC_LIST_IRQ)
		return;
	ch_wr(ch, T32_MSCA_Y_CLR, 0);
	ch_wr(ch, T32_MSCA_UV_CLR, 0);
}

/* scaling of the channels whose curves are loaded (others: the flat box) */
static struct {
	u32 in_w, in_h, w, h;
	bool on;
} curve_geom[T32_MSCA_CHANNELS];

static void curve_words(int ch, bool vertical, unsigned int words[T32_CURVE_WORDS])
{
	int c[T32_CURVE_TAPS];

	if (!curve_geom[ch].on)
		t32_msca_curve_box(c);
	else if (vertical)
		t32_msca_curve(curve_geom[ch].in_h, curve_geom[ch].h, c);
	else
		t32_msca_curve(curve_geom[ch].in_w, curve_geom[ch].w, c);
	t32_msca_curve_pack(c, words);
}

/*
 * Polyphase curves of all channels through the shadow port, as the vendor
 * tisp loads them at every channel start (tisp_msca_ch_curve_write):
 * BEGIN, (value, offset) pairs, the tail field, END, the pair count.
 */
static void t32_hw_msca_load_curves(void)
{
	unsigned int words[T32_CURVE_WORDS], tail = 0;
	int ch, v, i, pairs = 0;

	isp_wr(T32_MSCA_SHADOW_OPEN, T32_MSCA_SHADOW_BEGIN);
	for (ch = 0; ch < T32_MSCA_CHANNELS; ch++)
		for (v = 0; v < 2; v++) {
			u32 base = T32_MSCA_CURVE(ch) + (v ? T32_MSCA_CURVE_V : 0);

			curve_words(ch, v, words);
			for (i = 0; i < T32_CURVE_WORDS; i++, pairs++) {
				isp_wr(T32_MSCA_SHADOW_DATA, words[i]);
				isp_wr(T32_MSCA_SHADOW_DATA, base + i * 4);
			}
			tail = words[T32_CURVE_WORDS - 1];
		}
	isp_wr(T32_MSCA_SHADOW_DATA, (tail & 0xffff) | T32_MSCA_CURVE_TAIL_FIELD);
	isp_wr(T32_MSCA_SHADOW_DATA, T32_MSCA_CURVE_TAIL);
	isp_wr(T32_MSCA_SHADOW_OPEN, T32_MSCA_SHADOW_END);
	isp_wr(T32_MSCA_SHADOW_COUNT, pairs << 4 | 1);
}

/*
 * Channel start in the vendor's order (S3 runs v1/v2, three channels):
 * the buffers are already pushed; strides, the polyphase curves (S4,
 * tx_isp_t32_msca_curve.h), then the channel's size, crop and scaler steps,
 * the filter select pulse, the update and the enable.  in_w x in_h is the
 * MSCA input (the sensor picture).
 */
#ifdef TX_ISP_T33
/* channels started on the direct path since the last stream-on (no list 23 runs then) */
static u32 t33_msca_direct, t33_msca_listed;

/* one module list alone (the vendor's single-bit 0x22010 trigger), its table word set at stream-on */
static void t33_rcfg_run(unsigned int id)
{
	unsigned int i;

	wmb();
	isp_wr(T33_RCFG_TRIG, 1U << id);
	for (i = 0; i < 1000 && (isp_rd(T33_RCFG_TRIG) & (1U << id)); i++)
		udelay(1);
	if (i == 1000)
		t33_rcfg_timeouts++;
	t33_rcfg_runs++;
}

/*
 * tisp_msca_t33_chx_cfg_load + tisp_msca_t33_chx_en on the single-sensor
 * path: the scaling enables 0 (tisp_msca_t33_scaling_algorithm, PRJ008
 * flags), the curves (tisp_msca_t33_curve_calc / ch_curve_write: ratios
 * out << 14 / in, channel 0 against the section 25 crop size when its
 * flag is set), sizes and steps (in << 14 / out), 0xf0128 and the strides
 * direct, the slot stores (CfgMode slot numbers, ch * 0x1e + ...), the
 * enable bit, then list 23 alone.  A cropped channel 1 / 2 (section 25
 * flags) or an upscale goes the direct way.
 */
static bool t33_msca_list_start(int ch, u32 w, u32 h, u32 stride, u32 in_w, u32 in_h)
{
	u32 *l = (t33_mod_built & (1ULL << T33_MOD_MSCA)) ? t33_mod_buf(T33_MOD_MSCA) : NULL;
	u32 b = 0x1e * (u32)ch, wh = (w & 0xffff) << 16 | (h & 0xffff), cw = w, chh = h, bytes, len, off;
	const u8 *bin, *s25 = NULL;

	if (!t32_core_iq_bin(&bin, &bytes) && (off = t32_iq_section(bin, bytes, T33_SEC_MSCA, &len)) && len >= 0xc8)
		s25 = bin + off;
	if (!l || !s25 || t33_msca_direct || l[0] < 0x12d || !w || !h || w > in_w || h > in_h) {
		t33_msca_direct |= 1U << ch;
		return false;
	}
	if (t32_tp_le16(s25 + 0xb8 + 2 * ch)) {	/* crop: tisp_msca_t33_curve_calc's ratio base */
		if (ch) {
			t33_msca_direct |= 1U << ch;
			return false;
		}
		cw = t32_tp_le16(s25 + 0xbe);
		chh = t32_tp_le16(s25 + 0xc0);
	}
	T33_SW(l, 6, 0);
	T33_SW(l, 1, 1);
	t33_msca_curves(l, ch, t33_msca_ratio(cw, in_w), t33_msca_ratio(chh, in_h));
	{
		u32 cpos = 0, csize = wh;

		/* FrameSource input crop (vendor format): position / size slots; ratios and steps stay on the sensor size */
		if (!t32_fs_crop(ch, &cpos, &csize)) {
			cpos = 0;
			csize = wh;
		}
		T33_SW(l, b + 0x1a, 0);
		T33_SW(l, b + 0x1b, wh);
		T33_SW(l, b + 0x1c, cpos);
		T33_SW(l, b + 0x1d, csize);
	}
	T33_SW(l, b + 0x1e, (in_h << 14) / h);
	T33_SW(l, b + 0x1f, (in_w << 14) / w);
	isp_wr(0xf0128 + 0x100 * ch, 0);
	T33_SW(l, b + 0x22, 0xffffffff);
	ch_wr(ch, T32_MSCA_Y_STRIDE, stride);
	ch_wr(ch, T32_MSCA_UV_STRIDE, stride);
	T33_SW(l, 1, 1);
	T33_SW(l, b + 0x2a, 0);
	T33_SW(l, b + 0x32, 0);
	T33_SW(l, 3, l[3 + 1] | 1U << ch);
	t33_msca_listed |= 1U << ch;
	t33_rcfg_run(T33_MOD_MSCA);
	t33_mod_dirty |= 1U << T33_MOD_MSCA;	/* tisp_msca_t33_api_chx_stream_on: tispMscaRefresh */
	return true;
}

/* lib_tisp_msca_chx_reg_stream_off, the FIFO clears, tisp_msca_t33_api_chx_stream_off */
static bool t33_msca_list_stop(int ch)
{
	u32 *l = t33_mod_buf(T33_MOD_MSCA), b = 0x1e * (u32)ch;

	t33_msca_direct &= ~(1U << ch);
	if (!(t33_msca_listed & (1U << ch)) || !l)
		return false;
	t33_msca_listed &= ~(1U << ch);
	isp_wr(T32_MSCA_CH_EN, isp_rd(T32_MSCA_CH_EN) & ~(1U << ch));
	ch_wr(ch, T32_MSCA_Y_CLR, 1);
	ch_wr(ch, T32_MSCA_UV_CLR, 1);
	ch_wr(ch, T32_MSCA_Y_DONE_CLR, 1);
	ch_wr(ch, T32_MSCA_UV_DONE_CLR, 1);
	T33_SW(l, b + 0x2a, 0);
	T33_SW(l, b + 0x32, 0);
	T33_SW(l, 3, l[3 + 1] & ~(1U << ch));
	t33_mod_dirty |= 1U << T33_MOD_MSCA;	/* tispMscaRefresh: at the next frame end (none after stream-off) */
	return true;
}
#endif

void t32_hw_msca_start(int ch, u32 w, u32 h, u32 stride, u32 in_w, u32 in_h)
{
	u32 size = t32_msca_size_word(w, h);
	unsigned long fl;

#ifdef TX_ISP_T33
	if (t33_msca_list_start(ch, w, h, stride, in_w, in_h))
		return;
#endif

	curve_geom[ch].in_w = in_w;
	curve_geom[ch].in_h = in_h;
	curve_geom[ch].w = w;
	curve_geom[ch].h = h;
	curve_geom[ch].on = true;
	if (ch == 0)
		t32_isposd_msca_start(in_w, in_h);	/* ISP OSD picture size, RAM flag (#336) */
	ch_wr(ch, T32_MSCA_Y_STRIDE, stride);
	ch_wr(ch, T32_MSCA_UV_STRIDE, stride);
	isp_wr(T32_MSCA_CH_EN2, 0);
	isp_wr(T32_MSCA_UPDATE, 1);
	fl = t32_isposd_ram_lock();	/* the shadow-RAM port is shared with the OSD commit */
	t32_hw_msca_load_curves();
	t32_isposd_ram_unlock(fl);
	ch_wr(ch, T32_MSCA_CTRL, 0);
	ch_wr(ch, T32_MSCA_SCL_SIZE, size);
	ch_wr(ch, T32_MSCA_PARAM2, T32_MSCA_PARAM2_DEFAULT);
	ch_wr(ch, T32_MSCA_CROP_POS, 0);
	ch_wr(ch, T32_MSCA_CROP_SIZE, size);
	ch_wr(ch, T32_MSCA_PARAM0, T32_MSCA_PARAM0_DEFAULT);
	ch_wr(ch, T32_MSCA_PARAM1, T32_MSCA_PARAM1_DEFAULT);
	ch_wr(ch, T32_MSCA_STEP_H, t32_msca_step(in_w, w));
	ch_wr(ch, T32_MSCA_STEP_V, t32_msca_step(in_h, h));
	ch_wr(ch, T32_MSCA_FILTER_SEL, T32_MSCA_FILTER_SEL_DEFAULT(ch));
	ch_wr(ch, T32_MSCA_FILTER_SEL, 0);
	isp_wr(T32_MSCA_UPDATE, 1);
	ch_wr(ch, T32_MSCA_Y_CLR, 0);
	ch_wr(ch, T32_MSCA_UV_CLR, 0);
	isp_wr(T32_MSCA_CH_EN, isp_rd(T32_MSCA_CH_EN) | (1U << ch));
	isp_wr(T32_MSCA_UPDATE, 1);
}

void t32_hw_msca_stop(int ch)
{
#ifdef TX_ISP_T33
	if (t33_msca_list_stop(ch))
		return;
#endif
	curve_geom[ch].on = false;
	isp_wr(T32_MSCA_CH_EN, isp_rd(T32_MSCA_CH_EN) & ~(1U << ch));
	isp_wr(T32_MSCA_UPDATE, 1);
	ch_wr(ch, T32_MSCA_Y_CLR, 1);
	ch_wr(ch, T32_MSCA_UV_CLR, 1);
	ch_wr(ch, T32_MSCA_Y_DONE_CLR, 1);
	ch_wr(ch, T32_MSCA_UV_DONE_CLR, 1);
}

/* pop one done entry: returns 0 and the addresses, or -1 when empty */
int t32_hw_msca_pop_done(int ch, u32 *y, u32 *uv)
{
	if (ch_rd(ch, T32_MSCA_FIFO_STATE) & T32_MSCA_FIFO_EMPTY)
		return -1;
	*uv = ch_rd(ch, T32_MSCA_UV_DONE);	/* before the Y read pops */
	*y = ch_rd(ch, T32_MSCA_Y_DONE);
	return 0;
}

/* ------------------------------------------------------------------ */

int t32_hw_init(void)
{
	int ret;

	isp_base = ioremap(T32_ISP_PHYS, T32_ISP_SIZE);
	csi_base = ioremap(T32_CSI_HOST_PHYS, 0x1000);
	dphy_base = ioremap(T32_DPHY_PHYS, 0x1000);
	if (!isp_base || !csi_base || !dphy_base) {
		ret = -ENOMEM;
		goto err;
	}
	rcfg_pages = __get_free_pages(GFP_KERNEL | __GFP_ZERO,
				      get_order((T32_RCFG_TABLE_WORDS + T32_RCFG_LIST_WORDS) * 4));
	if (!rcfg_pages) {
		ret = -ENOMEM;
		goto err;
	}
	rcfg_pa = virt_to_phys((void *)rcfg_pages);
	/*
	 * Nothing latched from an earlier load may fire before we are ready.
	 * T33: the ISP clock is still off here and the vendor module does not
	 * touch the ISP before the first open; stream-off and module exit
	 * leave the same state (enables 0, VIC masked).
	 */
	if (!T32_SOC_LIST_IRQ) {
		isp_wr(T32_INT_EN, 0);
		isp_wr(T32_CHDONE_EN, 0);
		isp_wr(T32_MSCADONE_EN, 0);
		isp_wr(T32_INT3_EN, 0);
		isp_wr(T32_INT4_EN, 0);
		t32_vic_wr(0, T32_VIC_INT_MSK, 0xffffffff);
		t32_vic_wr(0, T32_VIC_INT_MSK2, 0xffffffff);
	}
	ret = request_irq(isp_irq, t32_isp_irq, 0, "isp-m0", &isp_base);
	if (ret)
		goto err;
	ret = request_irq(vic_irq, t32_vic_irq, 0, "isp-w02", &csi_base);
	if (ret) {
		free_irq(isp_irq, &isp_base);
		goto err;
	}
	irqs_requested = 1;
	return 0;
err:
	isp_printf(2, "tx-isp-t32: hardware init failed (%d)\n", ret);
	if (rcfg_pages)
		free_pages(rcfg_pages, get_order((T32_RCFG_TABLE_WORDS + T32_RCFG_LIST_WORDS) * 4));
	rcfg_pages = 0;
	if (dphy_base)
		iounmap(dphy_base);
	if (csi_base)
		iounmap(csi_base);
	if (isp_base)
		iounmap(isp_base);
	isp_base = csi_base = dphy_base = NULL;
	return ret;
}

void t32_hw_exit(void)
{
	int i, regs;

	if (!isp_base)
		return;
	/*
	 * T33: rmmod runs after the last close, with the ISP clock gated; the
	 * vendor module does not touch the ISP there (the streams and engines
	 * were stopped at stream-off / close).  Register writes into a gated
	 * block are lost or hang the bus, so only the software teardown runs.
	 * T32 unchanged.
	 */
	regs = !T32_SOC_LIST_IRQ || t32_core_clk_active();
	if (regs) {
		t32_hw_stream_off();
		for (i = 0; i < T32_MSCA_CHANNELS; i++)
			t32_hw_msca_stop(i);
		t32_awbc_detach();	/* the per-frame AWB reads the AWB buffers */
		t32_ctup_detach();	/* after the AWB: its work runs the ct fan-out */
		t32_afgsm_exit();	/* tisp_af_deinit */
		t32_stats_off();	/* no statistics DMA into the buffers freed below */
	}
	if (irqs_requested) {
		free_irq(vic_irq, &csi_base);
		free_irq(isp_irq, &isp_base);
		irqs_requested = 0;
	}
	cancel_work_sync(&t32_bringup_ae_w);	/* it reads registers and the AE buffer */
	t32_pf_exit();				/* the per-frame work writes registers */
	t32_lsc_drv_exit();			/* after the interrupts and the ct / per-frame work */
	t32_blk_exit();				/* block event work (after the LCE state is gone) */
#ifdef TX_ISP_T33
	cancel_work_sync(&t33_stats_w);	/* it reads registers and the statistics buffers */
	t33_evq_free();
	kfree(t33_gr_tmp);
	t33_gr_tmp = NULL;
#endif
	if (regs)
		writel(0, csi_base + 0x0c);	/* CSI host back into reset */
	csi_reset_done = 0;
	iounmap(dphy_base);
	iounmap(csi_base);
	iounmap(isp_base);
	isp_base = csi_base = dphy_base = NULL;
	if (ae_buf)
		free_pages(ae_buf, T32_AE_BUF_ORDER);
	if (awb_buf)
		free_pages(awb_buf, T32_AWB_BUF_ORDER);
	ae_buf = awb_buf = 0;
	if (lce_work)
		free_pages(lce_work, T32_LCE_WORK_ORDER);
	if (lce_curve)
		free_pages(lce_curve, T32_LCE_CURVE_ORDER);
	if (lce_hist)
		free_pages(lce_hist, T32_LCE_HIST_ORDER);
	lce_work = lce_curve = lce_hist = 0;
	t32_blk_lce_bufs(NULL, NULL, NULL);
	kfree(tisp_w);
	tisp_w = NULL;
	t33_mod_free();
	free_pages(rcfg_pages, get_order((T32_RCFG_TABLE_WORDS + T32_RCFG_LIST_WORDS) * 4));
	rcfg_pages = 0;
}
