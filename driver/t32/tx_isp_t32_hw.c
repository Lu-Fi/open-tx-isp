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
#include "tx_isp_t33_gammav.h"
#include "tx_isp_t33_defog.h"
#include "tx_isp_t33_lce.h"
#include "tx_isp_t33_lsf.h"
#ifdef TX_ISP_T33
#include "tx_isp_t33_defogpf.h"	/* the per-frame defog (irq 10, event 3, irq 47; openimp #878) */
static bool t33_dpf_active(void);
static u32 t33_dpf_runs[5];	/* irqs, pars, ev updates, event queue full, irq 47 list starts */
static void t33_lcepf_lock_irq(void);
static void t33_lcepf_unlock_irq(void);
static void t33_lcepf_wbinv(unsigned long pages, u32 len);
static void t33_lcepf_free(void);
extern u32 t33_lcepf_irq8s, t33_lcepf_irq30s, t33_lcepf_curves, t33_lcepf_lce_runs, t33_lcepf_lsf_runs, t33_lcepf_evs;
#define T33_LCEPF_LOCK(c)	t33_lcepf_lock_irq()
#define T33_LCEPF_UNLOCK(c)	t33_lcepf_unlock_irq()
#include "tx_isp_t33_lceev.h"	/* tx_isp_t33_lcepf.h + the EV update (openimp #500) */
static void t33_lcepf_ev(u32 lo, u32 hi, u32 bv);
#endif
#include "tx_isp_t33_af.h"
#include "tx_isp_t33_afalgo.h"	/* AF per-frame algorithm (#437) */
#include "tx_isp_t33_bcsh.h"
#include "tx_isp_t33_tmo.h"
#include "tx_isp_t33_tmoA.h"
#include "tx_isp_t33_clm.h"
#include "tx_isp_t33_msca.h"
#include "tx_isp_t33_mdns.h"
#include "tx_isp_t33_ae.h"
#include "tx_isp_t33_awb.h"
#include "tx_isp_t33_binsw.h"
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
#include "tx_isp_t33_append.h"
#include "tx_isp_t33_ldcB.h"	/* lib_tisp_ldc_get_par (group 12) */
#include "tx_isp_t33_rcfg1.h"
#include "tx_isp_t33_rcfg2.h"
#include "tx_isp_t33_rcfg3.h"
#include "tx_isp_t33_rcfg4.h"
#include "tx_isp_t33_rcfg5.h"
#include "tx_isp_t33_rcfg6.h"
#include "tx_isp_t33_rcfg7.h"
#include "tx_isp_t33_rcfg8.h"
#include "tx_isp_t33_rcfg9.h"
#include "tx_isp_t33_rcfg10.h"
#include "tx_isp_t33_rcfgcore.h"
#include "tx_isp_t33_deinit.h"
#endif

static void t32_bringup_ae_kick(void);
#ifdef TX_ISP_T33
static struct work_struct t33_stats_w;
static struct work_struct t33_af_w;
static void t33_af_show(struct seq_file *m);
#define T33_INT_AF	(1U << 6)
#endif
static struct work_struct t33_flip_w;	/* sensor flip, T33 #669, T32 #689 */
static void t33_flip_kick(void);
static void t33_vic_hold_tick(u32 sta);

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
static u8 t33_top_refresh;		/* tispTopRefresh: 1 = list 33 at the next frame end (bin switch #504, LDC) */
static u8 t33_msca_refresh;		/* tispMscaRefresh: 1 = list 23 at the next MSCA frame done */
static u32 t33_msca_test_num;		/* testNum of tisp_msca_t33_frame_done_interrupt */
static u32 t33_mod_dirty;	/* lists 0..31 to run again at the next frame end (tisp*Refresh) */

/* t33_mod_dirty |= bits with the frame-end irq off (it reads and clears the word; openimp #579) */
static void t33_mod_mark(u32 bits)
{
	unsigned long flags;

	local_irq_save(flags);
	t33_mod_dirty |= bits;
	local_irq_restore(flags);
}
static bool t33_awb_refresh;
static bool t33_first_fe;	/* the TMO start word is still 1 */
static u8 t33_mdns_init_flag, t33_mdns_started;	/* mdnsInit (set by the init), g_mdnsStart */
static void t33_first_frame_words(void);
static void t33_mdns_fd(void);
/*
 * tmoInfo[0] in the vendor layout (10880 bytes; tisp_tmo_switch_para below fills it): byte 10845 the
 * "configured" flag of tisp_tmo_t33_frame_done_interrupt (cleared at every init, as tisp_tmo_init does).
 */
#define T33_TMOINFO_BYTES	10880U
static u32 t33_swg_tmo_info[T33_TMOINFO_BYTES / 4];
static u8 t33_tmo_fd_refresh, t33_tmo_fd_mulsensor;	/* tispTmoRefresh, mulsensornum (frame done) */

static void t33_bst_irq(u32 msca);	/* tx_isp_t33_bst.h: the blocks' frame-done handlers */
static void t33_csccr_fd(u32 msca);	/* tx_isp_t33_bst.h: CSCCR frame done (irq 57) */
static void t33_csc_fra_down(const u32 *init);	/* system irq 48: tisp_csc_fra_down_interrupt */
static void t33_ccm_fd(void);			/* system irq 45: tisp_ccm_irq_frame_done (tx_isp_t33_ccmv.h) */
static void t33_gamma_fd(void);			/* system irq 46: tisp_gamma_irq_frame_done (tx_isp_t33_gammav.h) */
static void t33_bcsh_fd(void);			/* system irq 52: tisp_bcsh_irq_frame_down (tx_isp_t33_g14glue.h) */
static void t33_dpf_isr(void);			/* system irq 10: tisp_defog_interrupt_static (#878) */
static void t33_dpf_fd(void);			/* system irq 47: tisp_defog_t33_frame_done_interrupt (#878) */
static bool t33_dpf_ev(u32 lo, u32 hi, u32 x, u32 y);	/* tisp_defog_ev_update (#878) */
extern u32 t33_st_isp_init_par_mode[1008];	/* stIspInitPar (word 226 = byte 904: 1) */
int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);
int tisp_rcfg_get_dma_state(u32 mod);
int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
/*
 * tisp_top_reg_write for the TOP null-down handler: our list 33 already holds the new words (t33_sw_top), so
 * only the refresh state of the vendor's tail (1 = idle: start at the next frame end, 2 = the list runs).
 */
static u32 tisp_top_reg_write(u32 ch)
{
	t33_top_refresh = tisp_rcfg_get_dma_state(T33_MOD_TOP) ? 2 : 1;
	return t33_top_refresh;
}
#include "tx_isp_t33_fd_misc.h"
#include "tx_isp_t33_tisp.h"
/* /proc/tisp level 3 (#589): the bypass bytes of stTopPar (the TOP section, +0xc..+0x23) as last loaded */
static u8 t33_tisp_top[0x24];
static bool t33_tisp_top_ok;

const u8 *t33_tisp_top_par(void)
{
	return t33_tisp_top_ok ? t33_tisp_top : NULL;
}

static void t33_tisp_top_save(const u8 *sec)
{
	memcpy(t33_tisp_top, sec, sizeof(t33_tisp_top));
	t33_tisp_top_ok = true;
}
/* lists 2..4, 6..9, 12..15, 17..22 */
#define T33_INIT_REFRESH	0x007ef3dcU
#define T33_TMO_START_REG	0x70f0U
extern u32 t33_rcfg_runs;
#define T33_INT_LCE	(1U << 8)
#define T33_INT_LSF	(1U << 30)
static void t33_lcepf_isr8(void);
static void t33_lcepf_isr30(void);
#endif

#ifndef TX_ISP_T33
/*
 * openimp #897 diagnostics: interrupt status words seen since the last stream-on (OR of isp 0x30028,
 * chdone 0x30038, 0x30068, msca 0x300a8, 0x300b8, 0x300c8, 0x300d8; VIC0/1 0x1e0 / 0x1e4) and the
 * VIC0 status bit counts, shown by t32_flip_show.  Counting only, no register access.
 */
static u32 t32_irq_or[7], t32_vic_or[4], t32_vic_bits[2][32], t32_vic_err_restarts;

static void t32_vic_count(u32 s0, u32 s1)
{
	int b;

	for (b = 0; b < 32; b++) {
		if (s0 & (1U << b))
			t32_vic_bits[0][b]++;
		if (s1 & (1U << b))
			t32_vic_bits[1][b]++;
	}
}
#endif

static irqreturn_t t32_isp_irq(int irq, void *dev)
{
	u32 sta, done, s2, msca, s3, s4, s5 = 0;

	sta = isp_rd(T32_INT_STA);
	done = isp_rd(T32_CHDONE_STA);
	/*
	 * st[2] (0x30068, acknowledged at 0x30064 when nonzero) as the vendor
	 * lib_tisp_intp_handler: enabled at stream-on, never acknowledged here
	 * before #393 -> on the T33 silicon an interrupt storm and
	 * "irq 37: nobody cared" right after the first frame.
	 */
	s2 = isp_rd(T32_INT2_STA);
	msca = isp_rd(T32_MSCADONE_STA);
	s3 = isp_rd(T32_INT3_STA);
	s4 = isp_rd(T32_INT4_STA);
	if (T32_SOC_LIST_IRQ)
		s5 = isp_rd(T32_INT5_STA);
	if (!(sta | done | s2 | msca | s3 | s4 | s5))
		return IRQ_NONE;
#ifndef TX_ISP_T33
	t32_irq_or[0] |= sta;
	t32_irq_or[1] |= done;
	t32_irq_or[2] |= s2;
	t32_irq_or[3] |= msca;
	t32_irq_or[4] |= s3;
	t32_irq_or[5] |= s5;
	t32_irq_or[6] |= s4;
#endif
	isp_wr(T32_INT_CLR, sta);
	isp_wr(T32_CHDONE_CLR, done);
	if (s2)
		isp_wr(T32_INT2_STA - 4, s2);
	isp_wr(T32_MSCADONE_CLR, msca);
	isp_wr(T32_INT3_CLR, s3);
	if (T32_SOC_LIST_IRQ)
		isp_wr(T32_INT5_CLR, s5);
	isp_wr(T32_INT4_CLR, s4);
#ifdef TX_ISP_T33
	isp_wr(T32_RDMA_IRQ_CLR, 0);
#else
	/*
	 * lib_tisp_intp_handler (vendor T32 ko 0xac390..0xac39c): st[5] (0x300d8) is also
	 * written to 0x2202c.  On the silicon that write-1-to-clear acknowledges a finished
	 * register list: bit 17 clears bit 17 of 0x22014 (list 49, the stream-on / LSC
	 * list).  Writing 0 here left 0x22014 = 0x20000 forever (openimp #852).
	 */
	isp_wr(T32_RDMA_IRQ_CLR, s4);
#endif
	t32_hw_stats.isp_irqs++;
#ifdef TX_ISP_T33
	t33_tisp_count_isp(sta, done, s2, msca, s3, s5, s4);	/* /proc/tisp counters (#589) */
#endif
	if (sta & 1) {
		t32_hw_stats.frame_ends++;
#ifdef TX_ISP_T33
		/*
		 * First frame end after stream-on: tisp_tmo_t33_frame_done_interrupt
		 * (TMO start word 0x70f0 = 0, tispTmoRefresh).
		 */
		if (t33_first_fe && !isp_rd(T33_RCFG_TRIG)) {
			t33_first_fe = false;
			t33_first_frame_words();
			if (t32_ldc_top_refresh())
				t33_top_refresh = 1;	/* tispTopRefresh */
		}
		/* system irq 0: tisp_top_t33_pipe_null_down_interrupt (tx_isp_t33_fd_misc.h) */
		t33_top_pipe_null_down(0, (const u8 *)t33_st_isp_init_par_mode, &t33_top_refresh);
		/* the vendor's frame-done list refresh: modules marked since, when no list runs */
		if (t33_mod_dirty && !isp_rd(T33_RCFG_TRIG)) {
			isp_wr(T33_RCFG_TRIG, t33_mod_dirty);
			t33_mod_dirty = 0;
			t33_rcfg_runs++;
		}
		t32_tunctl_frame_done(0);	/* isp_frame_done_wakeup (tuning id 0x08000091) */
		t33_flip_kick();		/* ispcore_irq_fd_work0: pending sensor flip (#669) */
#else
		t32_tunctl_frame_done(0);	/* isp_frame_done_wakeup (tuning id 0x08000091) */
		t33_flip_kick();		/* ispcore_irq_fd_work0: pending sensor flip (#689) */
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
	if (sta & T33_INT_AF) {		/* system irq 6: tisp_af_interrupt_static -> event 15 (#437) */
		t32_afg_stats.af_irqs++;
		schedule_work(&t33_af_w);
	}
	if (sta & T33_INT_LCE)		/* system irq 8: tisp_lce_interrupt_static */
		t33_lcepf_isr8();
	if (sta & (1U << T33_DPF_IRQ_BIT))	/* system irq 10: tisp_defog_interrupt_static (#878) */
		t33_dpf_isr();
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
#ifdef TX_ISP_T33
	if (msca)			/* system irqs 38..56: tisp_*_t33_frame_done_interrupt */
		t33_bst_irq(msca);
	if (msca & (1U << 18))		/* system irq 50: tisp_mdns_t33_frame_done_interrupt (oracle block mdnsA) */
		t33_mdns_fd();
	t33_csccr_fd(msca);		/* system irq 57 after 50 (#755) */
	if (msca & (1U << 16))		/* system irq 48: tisp_csc_fra_down_interrupt */
		t33_csc_fra_down(t33_st_isp_init_par_mode);
	if (msca & (1U << 13))		/* system irq 45: tisp_ccm_irq_frame_done (block ccmP) */
		t33_ccm_fd();
	if (msca & (1U << T33_GAMMA_IRQ_BIT))	/* system irq 46: tisp_gamma_irq_frame_done (block gammav) */
		t33_gamma_fd();
	if (msca & (1U << T33_DPF_FD_BIT))	/* system irq 47: tisp_defog_t33_frame_done_interrupt (#878) */
		t33_dpf_fd();
	if (msca & (1U << 20))		/* system irq 52: tisp_bcsh_irq_frame_down (block bcshO) */
		t33_bcsh_fd();
#endif
#ifdef TX_ISP_T33
	if (msca & 2)			/* system irq 33: tisp_msca_t33_frame_done_interrupt */
		t33_msca_frame_done(0, (const u8 *)t33_st_isp_init_par_mode, &t33_msca_refresh, &t33_msca_test_num);
#endif
	if (msca & 2)			/* MSCA frame done: tisp_msca_irq_frame_done (ISP OSD, #336) */
		t32_isposd_frame_done();
#ifndef TX_ISP_T33
	if (msca & 2)			/* tisp_msca_cfg_shd's mirror / flip part (openimp #717) */
		t32_tunctl_msca_frame_done();
#endif
	if (msca & T32_MSCADONE_LSC)	/* interrupt bit 36: tisp_lsc_irq_frame_done (tx_isp_t32_lscct.c) */
		t32_lsc_drv_frame_done();
	return IRQ_HANDLED;
}

#ifndef TX_ISP_T33
/*
 * VIC error recovery (openimp #897), vendor SDK 2.0.0 (20260804) libPRJ007-firmware-310-540.a
 * isp_vic_interrupt_service_routine 0x2da4..0x2e00: per VIC channel, a status with any of the bits
 * 0xffffff00 on a one-channel VIC (+1092 == 1) runs lib_tisp_vic_reset (0xabc1c: read 0x10 of both VICs,
 * pulse 0x200 of each VIC to 0 and back, 0x18 = 0, 0x10 = 4 on VIC 0 and 1) and lib_tisp_vic_stream_on
 * (0xac9e0: 0x10 = 1 on VIC 0 and 1).  The older module firmware ko (71bfc866, 0x1da8) only counts the
 * error bits.  Without it a VIC that starts in error (PRJ007 gc4653: bit 14 once, then bit 28 on every
 * frame) never delivers another frame end until the next stream-on.
 */
static void t32_vic_wr(int n, u32 off, u32 v);

/*
 * #915: the error restart only while the stream runs.  The vendor stream-off (vic_core_s_stream 0x13b6c)
 * masks the VIC interrupts (tx_vic_disable_irq -> lib_tisp_vic_intp_disable 0xbdf38: VIC 0x1e8 / 0x1ec =
 * 0xffffffff) before lib_tisp_vic_stop, so no late error interrupt can re-arm the VIC after the stop; the
 * flag covers an interrupt that was already being serviced.
 */
static bool t32_vic_on;

static void t32_vic_err_restart(void)
{
	int i;
	u32 v;

	(void)readl(isp_base + T32_VIC_BASE(0) + T32_VIC_CTRL);
	(void)readl(isp_base + T32_VIC_BASE(1) + T32_VIC_CTRL);
	for (i = 0; i < 2; i++) {
		v = readl(isp_base + T32_VIC_BASE(i) + 0x200);
		t32_vic_wr(i, 0x200, 0);
		t32_vic_wr(i, 0x200, v);
	}
	t32_vic_wr(0, 0x018, 0);
	t32_vic_wr(1, 0x018, 0);
	t32_vic_wr(0, T32_VIC_CTRL, 4);
	t32_vic_wr(1, T32_VIC_CTRL, 4);
	t32_vic_wr(0, T32_VIC_CTRL, 1);
	t32_vic_wr(1, T32_VIC_CTRL, 1);
	t32_vic_err_restarts++;
}
#endif

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
#ifdef TX_ISP_T33
	t33_tisp_count_vic(0, s0, s1);	/* /proc/tisp VIC counters (#589) */
	t33_tisp_count_vic(1, t0, t1);
#else
	t32_vic_or[0] |= s0;
	t32_vic_or[1] |= s1;
	t32_vic_or[2] |= t0;
	t32_vic_or[3] |= t1;
	t32_vic_count(s0, s1);
	if (ACCESS_ONCE(t32_vic_on) && (s0 & 0xffffff00U))	/* vendor: per VIC channel, one-channel VIC (#897) */
		t32_vic_err_restart();
	if (ACCESS_ONCE(t32_vic_on) && (t0 & 0xffffff00U))
		t32_vic_err_restart();
#endif
	t33_vic_hold_tick(s0);		/* the sensor flip's VIC hold (T33 #669, T32 #689) */
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
/*
 * Zeroed pages the ISP reads or writes by DMA while the CPU uses them
 * through KSEG1 (uncached).  __GFP_ZERO clears them through the cache: the
 * dirty zero lines must be written back and dropped before the first
 * uncached write, or a later eviction overwrites the register lists and
 * tables in DDR with zeros (T33 silicon: lists did not run or ran zeros,
 * no frames; QEMU has no cache, openimp #393).
 */
unsigned long t32_dma_zpages(unsigned int order)
{
	unsigned long p = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);

	if (p)
		dma_cache_wback_inv(p, PAGE_SIZE << order);
	return p;
}

static u32 rcfg_pa;
u32 t32_rcfg_runs, t32_rcfg_timeouts, t32_rcfg_last_len;

/*
 * T33 per-block lists (tx_isp_t32_rcfg33.h): one page per module, built at
 * stream-on, started all at once.  Unused on the T32.
 */
static unsigned long t33_mod_page[T33_MODULES];
static u64 t33_mod_built;
u32 t33_rcfg_runs, t33_rcfg_timeouts;
#ifdef TX_ISP_T33
static bool t33_rcfg_failed;	/* last stream-on list wait timed out: no 0x1040 (#927) */
#endif
#ifdef TX_ISP_T33
u32 t33_msca_vendor_starts, t33_msca_direct_starts;	/* channel starts on the vendor path / direct (g17-2) */
extern u32 t33_bst_bad;					/* tisp_reg_write_t33 refusals (tx_isp_t33_bst.h) */
#endif

/* a module's list page, uncached (the core reads it by DMA); NULL without memory */
void t33_osd_list_rebuilt(void);	/* tx_isp_t33_osd.c */
void t33_msca_attr_stream_on(u32 w, u32 h);
void t33_msca_attr_exit(void);
void t33_osd_lock(void);	/* tx_isp_t33_osd.c: MSCA attribute block + list 23 words (#578), core.lock -> osd */
void t33_osd_unlock(void);
int t33_msca_chx_stream_set(u32 ch, u32 on);	/* tx_isp_t33_osd.c: stream on / off + chx_en (g16-3) */

#ifdef TX_ISP_T33
static void t33_rc_register(unsigned int id);	/* tisp_rcfg_module_register (below) */

/* system_reg_write / system_reg_read: the vendor's direct ISP register access (tisp_msca_t33_init, group 11) */
void system_reg_write(u32 reg, u32 val)
{
	isp_wr(reg, val);
}

u32 system_reg_read(u32 reg)
{
	return isp_rd(reg);
}
#endif

static u32 *t33_mod_buf(unsigned int id)
{
	if (id >= T33_MODULES)
		return NULL;
	if (!t33_mod_page[id])
#ifdef TX_ISP_T33
		t33_rc_register(id);
#else
		t33_mod_page[id] = t32_dma_zpages(0);
#endif
	if (!t33_mod_page[id]) {
		isp_printf(2, "tx-isp-t33: no memory for register list %u\n", id);
		return NULL;
	}
	return (u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_mod_page[id]));
}

#ifdef TX_ISP_T33
/*
 * tisp_rcfg_module_register's tisp_rcfg_*_reg_init (tx_isp_t33_rcfg*.h, oracle-proven): the vendor's
 * list layout (length word + command words) of each module.  A built list must carry exactly these
 * words; they are written by the vendor function (no change when equal), a list whose layout differs
 * is counted and left as built (dmesg once per module).
 */
/*
 * the vendor's stIspInitPar of channel 0 as far as the T33 ports read it: word 226 (byte 904) = 1, list mode;
 * words 230..251 = fstframe_defaultp (ispcore_core_ops_init copies it in without RISC-V; group 14): EV 0x4000,
 * init gain 0x10000 (232, byte 0x3a0), CT 5000 K (244), gain tolerance 0x100 (250, byte 0x3e8) ...
 */
u32 t33_st_isp_init_par_mode[1008] = {
	[226] = 1,
	[230] = 0x4000, [231] = 0, [232] = 0x10000, [233] = 0x400, [234] = 0x400, [235] = 0x10, [236] = 0x400,
	[237] = 0, [238] = 0, [239] = 0x400, [240] = 0x400, [241] = 1, [242] = 0, [243] = 0, [244] = 0x1388,
	[245] = 0, [246] = 0x64, [247] = 0, [248] = 0x64, [249] = 0, [250] = 0x100, [251] = 0x64,
};

static u32 t33_msca_rinit(u32 *e)
{
	return t33_rcfg_msca_init(e, t33_st_isp_init_par_mode);
}

struct t33_rinit {
	u8 mod;
	u32 (*fn)(u32 *e);
};
static const struct t33_rinit t33_rinit_tab[] = {
	{ T33_MOD_DPC, t33_rcfg_dpc_init }, { T33_MOD_RDNS, t33_rcfg_rdns_init }, { T33_MOD_BLC, t33_rcfg_blc_init },
	{ T33_MOD_DG, t33_rcfg_dg_init }, { T33_MOD_LSC, t33_rcfg_lsc_init }, { T33_MOD_AWBG, t33_rcfg_awbg_init },
	{ T33_MOD_TMO, t33_rcfg_tmo_init }, { T33_MOD_DMSC, t33_rcfg_dmsc_init }, { T33_MOD_CCM, t33_rcfg_ccm_init },
	{ T33_MOD_GAMMA, t33_rcfg_gamma_init }, { T33_MOD_DEFOG, t33_rcfg_defog_init },
	{ T33_MOD_CSC, t33_rcfg_csc_init }, { T33_MOD_YSP, t33_rcfg_ysp_init }, { T33_MOD_MDNS, t33_rcfg_mdns_init },
	{ T33_MOD_LCE, t33_rcfg_lce_init }, { T33_MOD_YDNS, t33_rcfg_ydns_init }, { T33_MOD_BCSH, t33_rcfg_bcsh_init },
	{ T33_MOD_CLM, t33_rcfg_clm_init }, { T33_MOD_SDNS, t33_rcfg_sdns_init }, { T33_MOD_CDNS, t33_rcfg_cdns_init },
	{ T33_MOD_CSCCR, t33_rcfg_csccr_init }, { T33_MOD_LDC, t33_rcfg_ldc_init }, { T33_MOD_AWB, t33_rcfg_awb_init },
	{ T33_MOD_AE, t33_rcfg_ae_init }, { T33_MOD_AF, t33_rcfg_af_init }, { T33_MOD_TOP, t33_rcfg_ipc_init },
	{ T33_MOD_LSF, t33_rcfg_lsf_init }, { T33_MOD_MSCA, t33_msca_rinit },
};
static u32 t33_rinit_e[T33_SLOT_WORDS];
u32 t33_rinit_ok, t33_rinit_bad;
static u64 t33_rinit_logged;

static void t33_rinit_apply(unsigned int id)
{
	u32 *l, n, k, bad = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(t33_rinit_tab) && t33_rinit_tab[i].mod != id; i++)
		;
	if (i == ARRAY_SIZE(t33_rinit_tab) || !t33_mod_page[id])
		return;
	l = (u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_mod_page[id]));
	memset(t33_rinit_e, 0, sizeof(t33_rinit_e));
	n = t33_rinit_tab[i].fn(t33_rinit_e);
	if (n > T33_SLOT_WORDS)
		n = 0, bad = 1;
	/* word 0: the list may be longer than the reg_init's (TMO: tisp_t33_tmo_rcfg_reg_num, 0xc4 > 0xc1) */
	if (n && l[0] > t33_rinit_e[0])
		t33_rinit_e[0] = 0;
	for (k = 0; k < n; k++)
		if (t33_rinit_e[k] && t33_rinit_e[k] != l[k])
			bad++;
	if (bad) {
		t33_rinit_bad++;
		if (!(t33_rinit_logged & (1ULL << id)))
			isp_printf(1, "tx-isp-t33: list %u layout differs from tisp_rcfg reg_init (%u words)\n", id, bad);
		t33_rinit_logged |= 1ULL << id;
		return;
	}
	for (k = 0; k < n; k++)
		if (t33_rinit_e[k])
			l[k] = t33_rinit_e[k];
	t33_rinit_ok++;
}

/*
 * The vendor's register-list core (tx_isp_t33_rcfgcore.h, oracle block rcfgcore) on channel 0:
 * tisp_rcfg_module_register allocates a module's list when it is first built (the vendor: at the
 * block's init) and writes its reg_init skeleton, tisp_rcfg_module_release frees it at the ISP exit,
 * tisp_rcfg_module_cfg_trig (tx_isp_t33_bst.h, the LCE / LSF interrupt lists) and
 * tisp_rcfg_t33_write_all_reg (stream-on) set the table words from the module records and start
 * the lists.  The list pages are one zeroed page each (>= the vendor's word count), written uncached:
 * the vendor's cache write-backs are barriers here.  Ids the vendor has no list for keep a plain page.
 */
static u8 t33_rc_attr[T33_RC_CH_BYTES] __aligned(4);	/* stModuleAttr, channel 0 */
static u16 t33_rc_regnum[4];				/* tispRcfgRegNum */
static u16 t33_tmo_reg_num;				/* tisp_t33_tmo_rcfg_reg_num (TMO one-shot below) */
static u32 *t33_rc_bufinfo[2];				/* rcfg_buf_info: table (uncached), its DMA address */
static u32 **t33_rc_bufinfo_p = t33_rc_bufinfo;
u32 t33_rcfg_module;					/* RcfgModule (tisp_get_rcfg_module) */
static u32 t33_rc_trig_cnt[4];
static u8 t33_rc_refresh;
static unsigned int t33_rc_cur_id;
u32 t33_rc_errors;

static void *t33_rc_alloc(u32 n)
{
	return n <= PAGE_SIZE ? (void *)t32_dma_zpages(0) : NULL;
}

static void t33_rc_zero(void *p, u32 n)
{
	memset((void *)CKSEG1ADDR(virt_to_phys(p)), 0, n);
}

static void t33_rc_free(void *p)
{
	free_page((unsigned long)p);
}

static void t33_rc_sync(void *p, u32 n)
{
	wmb();
}

static void t33_rc_wr(u32 reg, u32 val)
{
	isp_wr(reg, val);
}

static u32 t33_rc_rd(u32 reg)
{
	return isp_rd(reg);
}

static void t33_rc_log(int level, u32 id)
{
	if (level == 0)
		return;
	t33_rc_errors++;
	isp_printf(2, "tx-isp-t33: rcfg module %u: %s\n", id,
		   level == -1 ? "register error" : level == -2 ? "list address not 8-byte aligned" : "no memory");
}

/* the vendor's reg_init(ch, list) of the module being registered: its skeleton into the (uncached) list */
static u32 t33_rc_rinit_cb(u32 ch, u32 *list)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(t33_rinit_tab); i++)
		if (t33_rinit_tab[i].mod == t33_rc_cur_id)
			return t33_rinit_tab[i].fn((u32 *)CKSEG1ADDR(virt_to_phys(list)));
	return 0;
}

static const struct t33_rcfgc t33_rcc = {
	.attr = t33_rc_attr, .regnum = t33_rc_regnum, .tmo_num = &t33_tmo_reg_num,
	.initpar = (const u8 *)t33_st_isp_init_par_mode, .buf_info = &t33_rc_bufinfo_p, .module = &t33_rcfg_module,
	.trig_cnt = t33_rc_trig_cnt, .refresh = &t33_rc_refresh, .alloc = t33_rc_alloc, .zero = t33_rc_zero,
	.free = t33_rc_free, .sync = t33_rc_sync, .wr = t33_rc_wr, .rd = t33_rc_rd, .log = t33_rc_log,
};

static u32 t33_rc_vaddr(unsigned int id)
{
	return id < 64 ? *t33_rc_w(&t33_rcc, 0, id, T33_RC_VADDR) : 0;
}

/* the module's list is the registered one and the table exists: the vendor's cfg_trig / write_all apply */
static bool t33_rc_ok(unsigned int id)
{
	return t33_rc_bufinfo[0] && id < T33_MODULES && t33_mod_page[id] && t33_rc_vaddr(id) == (u32)t33_mod_page[id];
}

static void t33_rc_register(unsigned int id)
{
	u32 r;

	t33_rc_cur_id = id;
	r = t33_rcfg_module_register(&t33_rcc, 0, id, (u32)(unsigned long)t33_rc_rinit_cb);
	if (!r)
		t33_mod_page[id] = t33_rc_vaddr(id);
	else if (r == T33_RC_ERR_ID)
		t33_mod_page[id] = t32_dma_zpages(0);	/* a list of ours the vendor does not have */
}

/* tisp_rcfg_t33_write_all_reg at stream-on: every list of the vendor's set must be registered */
static bool t33_rc_write_all(void)
{
	static const u8 extra[3] = { 30, 23, 33 };
	unsigned int i;

	for (i = 0; i < sizeof(t33_rc_all); i++)
		if (!t33_rc_ok(t33_rc_all[i]))
			return false;
	for (i = 0; i < 3; i++)
		if (!t33_rc_ok(extra[i]))
			return false;
	if (t33_st_isp_init_par_mode[11] & 0xff && !t33_rc_ok(T33_MOD_LDC))
		return false;
	t33_rcfg_write_all_reg(&t33_rcc, 0);
	return true;
}
#endif

static void t33_mod_done(unsigned int id, int len)
{
	if (len < 0) {
		isp_printf(2, "tx-isp-t33: register list %u not built\n", id);
		t33_mod_built &= ~(1ULL << id);
		return;
	}
	t33_mod_built |= 1ULL << id;
#ifdef TX_ISP_T33
	t33_rinit_apply(id);
#endif
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
#ifdef TX_ISP_T33
/*
 * IQ bin append data (tisp_bin_append_data_parse, tx_isp_t33_append.h), parsed with every bin (tisp_bin_data_parse:
 * load and day / night refresh): the LDC parameters for lib_tisp_ldc_get_par, and LSC tables behind the payload.
 * With appended tables the LSC block reads a copy of section 7 (count, lists) and the table buffer instead of
 * the bin.  One channel.
 */
static u8 t33_app_ldc_exist[4], t33_app_ldc_par[T33_LDC_PAR_BYTES];
static u8 t33_app_lsc_sec[T33_LSC_SEC_BYTES];
static struct t33_app_lsc t33_app_lsc;
static u32 t33_app_lsc_n;	/* appended tables; 0: the bin's section and payload */

static int t33_app_grow(struct t33_app_lsc *l, u32 need)
{
	u8 *n = vzalloc(need);

	if (!n)
		return -ENOMEM;
	vfree(l->lut);	/* the payload is copied in after the parse */
	l->lut = n;
	return 0;
}

static void t33_app_log(int what, u32 val)
{
	static const char fn[] = "tisp_bin_append_data_parse";

	if (what == T33_APP_LOG_NULL)
		isp_printf(2, "[ISP][%s, %d]: append data is null or size is %d beacuse of size < sizeof(tisp_append_par_t)!!!\n",
			   fn, 913, val);
	else if (what == T33_APP_LOG_LSC0)
		isp_printf(1, "[%s %d]: LSC mesh lut size = %d!!!\n", fn, 946, val);
	else if (what == T33_APP_LOG_LDCSZ)
		isp_printf(1, "[%s %d]: LDC par size=%d is invalid!!!\n", "tisp_ldc_get_extra_par", 391, val);
	else
		isp_printf(1, "[ISP][%s, %d]: append type %d is not supported!!!\n", fn, 982, val);
}

static const struct t33_app_ops t33_app_ops = { t33_app_grow, t33_app_log };

static void t33_bin_append(const u8 *bin, u32 bytes)
{
	u32 app = t33_bin_append_off(bin, bytes), off, len = 0, pay = 0;
	struct t33_app_lsc *l = &t33_app_lsc;

	t33_ldc_reset_par(t33_app_ldc_exist, 0);	/* tisp_ldc_reset_par_state */
	t33_app_lsc_n = 0;
	if (!app || app >= bytes)	/* no append data: "Bin file data parse successed" */
		return;
	off = t32_iq_section(bin, bytes, T33_SEC_LSC, &len);
	l->sec = NULL;
	if (off && len == T33_LSC_SEC_BYTES) {
		pay = t32_iq_payload(bin + off, bytes - off, T33_SEC_LSC);
		if (off + len + pay <= bytes) {
			memcpy(t33_app_lsc_sec, bin + off, len);
			l->sec = t33_app_lsc_sec;
			l->size16 = pay;
		}
	}
	t33_bin_append_parse(0, bin + app, bytes - app, t33_app_ldc_exist, t33_app_ldc_par, l, &t33_app_ops);
	if (l->sec && l->lut && l->sec[T33_APP_LSC_CNT] > bin[off + T33_APP_LSC_CNT] && pay <= l->cap) {
		memcpy(l->lut, bin + off + len, pay);
		t33_app_lsc_n = l->sec[T33_APP_LSC_CNT] - bin[off + T33_APP_LSC_CNT];
	}
}

/* the LSC section and its tables: the bin's, or the copy with the appended tables */
static const u8 *t33_lsc_src(const u8 *bin, u32 bytes, u32 off, u32 len, const u8 **tab, u32 *tabbytes)
{
	if (t33_app_lsc_n) {
		*tab = t33_app_lsc.lut;
		*tabbytes = t33_app_lsc.cap;
		return t33_app_lsc_sec;
	}
	*tab = bin + off + len;
	*tabbytes = t32_iq_payload(bin + off, bytes - off, T33_SEC_LSC);
	return bin + off;
}

/*
 * lib_tisp_ldc_get_par (tx_isp_t33_ldcB.h) on ldcParExist / stLdcPar (t33_app_ldc_exist / _par, filled by
 * tisp_ldc_get_extra_par at the bin parse): the 80 LDC parameter bytes of the bin's append data, -1 without.
 * Single sensor: an input other than 0 is refused (the vendor reads past its one-input state).
 */
int t33_bin_ldc_par(u32 ch, u8 *par)
{
	if (ch & 0xff)
		return -1;
	return t33_ldc_get_par(t33_app_ldc_exist, t33_app_ldc_par, ch, par);
}
#endif
/* defog statistics area (four 2 KiB parts, 0xa210..0xa21c), written by the core */
static unsigned long t33_defog_pages;
static u8 t33_defog_sec[T33_DEFOG_SEC_BYTES];
#ifdef TX_ISP_T33
static u8 *t33_defog_weight;		/* defog_weight[0] of tisp_defog_init (vmalloc) */
static void t33_g15_defog_init(u32 width, u32 height);	/* tisp_defog_init (group 15, below) */
extern u32 t33_g15_runs[3];
static u32 t33_swg_defog_blk[14];		/* defog_info[0] (below) */
static void t33_dpf_stop(void);			/* per-frame defog (#878, below) */
#endif
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
	vfree(t33_app_lsc.lut);
	memset(&t33_app_lsc, 0, sizeof(t33_app_lsc));
	t33_app_lsc_n = 0;
#endif

	if (t33_lsc_pages)
		free_pages(t33_lsc_pages, T33_LSC_ORDER);
	t33_lsc_pages = 0;
#ifdef TX_ISP_T33
	t33_dpf_stop();		/* before the statistics pages go (#878) */
#endif
	if (t33_defog_pages)
		free_pages(t33_defog_pages, T33_DEFOG_ORDER);
	t33_defog_pages = 0;
#ifdef TX_ISP_T33
	vfree(t33_defog_weight);
	t33_defog_weight = NULL;
#endif
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
#ifdef TX_ISP_T33
		if (t33_mod_page[i] && t33_rc_vaddr(i) == (u32)t33_mod_page[i])
			t33_rcfg_module_release(&t33_rcc, 0, i);	/* tisp_rcfg_module_release */
		else
#endif
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

#ifdef TX_ISP_T33
/*
 * The vendor tisp_rcfg_t33_reset_reg_cfg (oracle-proven port tx_isp_t33_rcfgmisc.h, block rcfgmisc) runs here:
 * its module data objects become these statics (rcfg_buf_info[0] + 4 holds the list table address).
 */
/* system_reg_write: above (global since group 11) */
static u32 t33_rcfg_buf[2];
static u8 *rcfg_buf_info[1] = { (u8 *)t33_rcfg_buf };
#include "tx_isp_t33_rcfgmisc.h"
#endif

/* tisp_rcfg_init: table, control and masks; at the core init */
static void t33_rcfg_core_init(void)
{
	isp_wr(T33_RCFG_MODE, 0);
#ifdef TX_ISP_T33
	t33_rcfg_buf[1] = rcfg_pa;
	t33_rcfg_reset_reg_cfg();	/* tisp_rcfg_t33_reset_reg_cfg: table, control, masks */
#else
	isp_wr(T33_RCFG_TABLE, rcfg_pa);
	isp_wr(T33_RCFG_CTRL, 1);
	isp_wr(T33_RCFG_MASK0, 0xffffffff);
	isp_wr(T33_RCFG_MASK1, 0xffffffff);
#endif
}

#ifdef TX_ISP_T33
/* tisp_all_reset hooks (tx_isp_t33_deinit.h); the other members are vendor allocations our driver has not */
static u32 t33_dn_rd(u32 reg)
{
	return isp_rd(reg);
}

static void t33_dn_wr(u32 reg, u32 val)
{
	isp_wr(reg, val);
}

static bool t33_dn_stop_to;	/* tisp_all_reset ended at the stop timeout (no core reset) */

static u32 t33_dn_log(u32 level, u32 line)
{
	if (line == 289) {
		t33_dn_stop_to = true;
		return isp_printf(level, "tx-isp-t33: isp stop timeout\n");
	}
	if (line == 301)
		return isp_printf(level, "tx-isp-t33: isp all reset timeout\n");
	return isp_printf(level, "tx-isp-t33: isp all reset\n");
}

static const struct t33_deinit t33_dn = { .rd = t33_dn_rd, .wr = t33_dn_wr, .log = t33_dn_log };

/*
 * lib_tisp_deinit, register part (pipe 0): at the last close, after the stream
 * off and before the clocks go off, as the vendor: tisp_all_reset, then
 * tisp_ldc_t33_deinit's LDC interrupt mask.  The block lists (BLC 4, MSCA 23,
 * LDC 25, ...) stay built until rmmod (t33_mod_free): our lists persist over
 * sessions, the next open resets and reprograms the core (CPM soft reset,
 * S_INPUT -> t32_hw_input_setup -> tisp_all_reset).
 * The MSCA attribute block (mirror / flip, OSD, mask, draw windows and the draw colour list) is dropped here:
 * the vendor's next lib_tisp_init runs tisp_msca_t33_init on a fresh block, so after an ISP close / open its
 * getters read the defaults (QEMU vendor run, openimp #577); ours is set up again at the next stream-on.
 */
void t33_hw_lib_deinit(void)
{
	t33_msca_attr_exit();	/* openimp #577 (core.lock -> osd, as at stream-on) */
	if (!isp_base)
		return;
	t33_all_reset(&t33_dn);
	isp_wr(T33_LDC_INT_MASK, 0xffffffffU);
}
#endif

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
#ifdef TX_ISP_T33
	if (t33_rc_write_all()) {	/* the vendor's order and start mask (RcfgModule), then list 33 */
		lo = t33_rcfg_module;
		hi = 1U << (T33_MOD_TOP - 32);
		goto wait;
	}
	t33_rc_errors++;		/* a list of the vendor's set is missing: start the built ones */
#endif
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
#ifdef TX_ISP_T33
wait:
#endif
#ifdef TX_ISP_T33
	/*
	 * SDK 2.1.0 PRJ008 lib_tisp_stream_on: up to 1001 rounds of {read 0x22010 and
	 * 0x22014; both 0: done; else write 0x22028 = 0 and 0x2202c = 0, msleep(5)}
	 * (about 5 s), no ISP start (0x1040) on timeout (SDK 2.0.0: unbounded).
	 * Process context (stream-on holds no lock) so the wait may sleep (#927).
	 */
	for (i = 0; i < 1001; i++) {
		if (!isp_rd(T33_RCFG_TRIG) && !isp_rd(T33_RCFG_TRIG_HI))
			break;
		isp_wr(T33_RCFG_CLR28, 0);
		isp_wr(T32_RDMA_IRQ_CLR, 0);
		msleep(5);
	}
	if (i == 1001) {
		t33_rcfg_timeouts++;
		t33_rcfg_failed = true;
		isp_printf(2, "tx-isp-t33: register lists did not finish (%#x %#x), ISP not started\n",
			   isp_rd(T33_RCFG_TRIG), isp_rd(T33_RCFG_TRIG_HI));
	}
#else
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
#endif
	t33_rcfg_runs++;
#ifdef TX_ISP_T33
	/*
	 * The init's tisp*Refresh flags: the vendor runs these lists once more
	 * at the first frame end, list 29 at the first AWB interrupt (QEMU trace).
	 */
	t33_mod_mark(lo & T33_INIT_REFRESH);
	t33_awb_refresh = !!(lo & (1U << T33_MOD_AWB));
	((u8 *)t33_swg_tmo_info)[T33_TMOA_FLAG] = 0;	/* tmoInfo configured flag (tisp_tmo_init) */
	t33_first_fe = true;
	t33_mdns_init_flag = 1;	/* mdnsInit (tisp_mdns_init) */
#endif
}


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
		ae_buf = t32_dma_zpages(T32_AE_BUF_ORDER);
	if (!awb_buf)
		awb_buf = t32_dma_zpages(T32_AWB_BUF_ORDER);
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
#ifdef TX_ISP_T33
#include "tx_isp_t33_csc.h"
#include "tx_isp_t33_cscQ.h"
#include "tx_isp_t33_clmP.h"
#include "tx_isp_t33_ccmsm.h"
/*
 * CSC list 13 as the vendor's tisp_csc_init: section copy with bytes 98 / 99 = 0, tisp_csc_para_refresh_by_mode,
 * tisp_csc_update_para (working block = csc_cp), tisp_t33_csc_write_reg into the slots (the same words the
 * list was built from), tispCscRefresh for tisp_csc_fra_down_interrupt (system irq 48, t33_csc_fra_down).
 * csc_para[0] (vendor layout, group 13d-1): the three blocks { section copy (tparamsP + 0x4570), csc_cp working
 * block, csc_ap API block }; tisp_csc_get (proven port t33_csc_get, block ccmsm) on it feeds tisp_clm_init /
 * tisp_bcsh_init and the CLM CT / switch paths; tisp_csc_fra_down_interrupt = the proven port of block cscQ.
 */
static u8 t33_csc_sec[T32_CSC_BYTES], t33_csc_full[0x28], t33_csc_api[T32_CSC_API_BYTES];
static u8 t33_csc_refresh, t33_csc_cfg_mode = 1;	/* tispCscRefresh, cscCfgMode */
static const struct t33_csc_ops t33_csc_hw = { tisp_reg_write_t33, tisp_rcfg_get_dma_state };
static u8 *t33_csc_blk[3] = { t33_csc_sec, t33_csc_full, t33_csc_api };
static u8 *csc_para[4];

/*
 * tisp_csc_para_refresh_by_mode / _update_para / _sync_api_para: the proven ports of block cscP on csc_para
 * (group 14e-1; before: the T32 forms t32_csc_refresh_by_mode / t32_csc_update_para, which leave the section's
 * second matrix and range bytes, +40 / +92, at the bin's values).  The vendor's six built-in tables (.data
 * CSC_BT601_FULL .. CSC_BT2020_LIMITED of tx-isp-t33.ko 3028d4ac; CSWTCH.19 = the same six, mode 6 none;
 * CSWTCH.20 = 1 for modes 0..5): refresh 1 copies the whole 92 bytes into the API block, 0 the mode's fields into
 * the section.  tisp_round_int64(x, 6): the vendor's rounding shift.
 */
static const u8 t33_csc_tab[6][92] = {
	{	/* CSC_BT601_FULL */
		0x00, 0x00, 0x00, 0x00, 0x8b, 0x4c, 0x00, 0x00, 0x46, 0x96, 0x00, 0x00, 0x2f, 0x1d, 0x00, 0x00,
		0xce, 0xd4, 0xff, 0xff, 0x32, 0xab, 0xff, 0xff, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
		0xd1, 0x94, 0xff, 0xff, 0x2f, 0xeb, 0xff, 0xff, 0x00, 0x80, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe9, 0x66, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
		0xe7, 0xa7, 0xff, 0xff, 0x2e, 0x49, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0xa2, 0xc5, 0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00,
	},
	{	/* CSC_BT601_LIMITED */
		0x01, 0x00, 0x00, 0x00, 0xbd, 0x41, 0x00, 0x00, 0x0f, 0x81, 0x00, 0x00, 0x10, 0x19, 0x00, 0x00,
		0x0e, 0xda, 0xff, 0xff, 0x81, 0xb5, 0xff, 0xff, 0x70, 0x70, 0x00, 0x00, 0x70, 0x70, 0x00, 0x00,
		0xd9, 0xa1, 0xff, 0xff, 0xb7, 0xed, 0xff, 0xff, 0x10, 0x80, 0x10, 0xeb, 0x10, 0xf0, 0x00, 0x00,
		0x15, 0x2a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x95, 0x98, 0x01, 0x00, 0x15, 0x2a, 0x01, 0x00,
		0xb5, 0x9b, 0xff, 0xff, 0xe1, 0x2f, 0xff, 0xff, 0x15, 0x2a, 0x01, 0x00, 0x69, 0x04, 0x02, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x10, 0xeb, 0x10, 0xf0, 0x00, 0x00,
	},
	{	/* CSC_BT709_FULL */
		0x02, 0x00, 0x00, 0x00, 0x6d, 0x36, 0x00, 0x00, 0x17, 0xb7, 0x00, 0x00, 0x7c, 0x12, 0x00, 0x00,
		0xab, 0xe2, 0xff, 0xff, 0x55, 0x9d, 0xff, 0xff, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
		0xbd, 0x8b, 0xff, 0xff, 0x43, 0xf4, 0xff, 0xff, 0x00, 0x80, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x26, 0x93, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
		0x0c, 0xd0, 0xff, 0xff, 0x29, 0x88, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x09, 0xdb, 0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00,
	},
	{	/* CSC_BT709_LIMITED */
		0x03, 0x00, 0x00, 0x00, 0xbe, 0x2e, 0x00, 0x00, 0x3e, 0x9d, 0x00, 0x00, 0xe0, 0x0f, 0x00, 0x00,
		0x3c, 0xe6, 0xff, 0xff, 0x53, 0xa9, 0xff, 0xff, 0x70, 0x70, 0x00, 0x00, 0x70, 0x70, 0x00, 0x00,
		0xdf, 0x99, 0xff, 0xff, 0xb1, 0xf5, 0xff, 0xff, 0x10, 0x80, 0x10, 0xeb, 0x10, 0xf0, 0x00, 0x00,
		0x15, 0x2a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf1, 0xca, 0x01, 0x00, 0x15, 0x2a, 0x01, 0x00,
		0x69, 0xc9, 0xff, 0xff, 0x93, 0x77, 0xff, 0xff, 0x15, 0x2a, 0x01, 0x00, 0xc6, 0x1c, 0x02, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x10, 0xeb, 0x10, 0xf0, 0x00, 0x00,
	},
	{	/* CSC_BT2020_FULL */
		0x04, 0x00, 0x00, 0x00, 0x40, 0x43, 0x00, 0x00, 0x91, 0xad, 0x00, 0x00, 0x2e, 0x0f, 0x00, 0x00,
		0x41, 0xdc, 0xff, 0xff, 0xbf, 0xa3, 0xff, 0xff, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
		0x4b, 0x8a, 0xff, 0xff, 0xb5, 0xf5, 0xff, 0xff, 0x00, 0x80, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x79, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
		0xe0, 0xd5, 0xff, 0xff, 0xbc, 0x6d, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0xa3, 0xe1, 0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00,
	},
	{	/* CSC_BT2020_LIMITED */
		0x05, 0x00, 0x00, 0x00, 0xc2, 0x39, 0x00, 0x00, 0x10, 0x95, 0x00, 0x00, 0x0a, 0x0d, 0x00, 0x00,
		0x9a, 0xe0, 0xff, 0xff, 0xf6, 0xae, 0xff, 0xff, 0x70, 0x70, 0x00, 0x00, 0x70, 0x70, 0x00, 0x00,
		0x9b, 0x98, 0xff, 0xff, 0xf5, 0xf6, 0xff, 0xff, 0x10, 0x80, 0x10, 0xeb, 0x10, 0xf0, 0x00, 0x00,
		0x15, 0x2a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbe, 0xad, 0x01, 0x00, 0x15, 0x2a, 0x01, 0x00,
		0x0b, 0xd0, 0xff, 0xff, 0x7e, 0x59, 0xff, 0xff, 0x15, 0x2a, 0x01, 0x00, 0x4b, 0x24, 0x02, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x10, 0xeb, 0x10, 0xf0, 0x00, 0x00,
	},
};
static const u8 *const t33_csc_tabs[7] = {
	t33_csc_tab[0], t33_csc_tab[1], t33_csc_tab[2], t33_csc_tab[3], t33_csc_tab[4], t33_csc_tab[5], NULL
};
static const u8 t33_csc_tab_on[7] = { 1, 1, 1, 1, 1, 1, 0 };

static long long t33_csc_round(long long x, int sh, int unused)
{
	(void)unused;
	return (unsigned int)sh - 1U < 0x3eU ? (x >> sh) + ((x >> (sh - 1)) & 1) : x;
}
#define tisp_round_int64	t33_csc_round
#include "tx_isp_t33_cscP.h"
#undef tisp_round_int64

static void t33_csc_rbm(u32 mode, u32 refresh, u8 *dst)
{
	t33_csc_para_refresh_by_mode(mode, refresh, dst, t33_csc_tabs, t33_csc_tab_on, t33_csc_tabs);
}

static void t33_csc_sync(u32 ch, u32 dir)
{
	t33_csc_sync_api_para(ch, dir, (struct t33_cscp *const *)csc_para);
}

static void t33_csc_upd(u32 ch)
{
	t33_csc_update_para(ch, (struct t33_cscp *const *)csc_para);
}

/* the vendor's .data templates csc_cp (working block) and csc_ap (API block = CSC_BT601_FULL), copied at each init */
static const u8 t33_csc_cp0[0x28] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x01, 0x59, 0x02, 0x75, 0x00,
	0xad, 0x00, 0x53, 0x01, 0x00, 0x02, 0x00, 0x02, 0xad, 0x01, 0x53, 0x00, 0x00, 0x80, 0x00, 0xff,
	0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void t33_csc_init_slots(const u8 *sec)
{
	memcpy(t33_csc_full, t33_csc_cp0, sizeof(t33_csc_cp0));
	memcpy(t33_csc_api, t33_csc_tab[0], T32_CSC_API_BYTES);
	memcpy(t33_csc_sec, sec, T32_CSC_BYTES);
	t33_csc_sec[98] = t33_csc_sec[99] = 0;
	csc_para[0] = (u8 *)t33_csc_blk;
	t33_csc_rbm(t33_csc_sec[91], 0, t33_csc_sec);
	t33_csc_upd(0);
	t33_csc_write_reg(0, t33_csc_full, t33_csc_cfg_mode, &t33_csc_refresh, &t33_csc_hw);
}

/* tisp_csc_get(ch, out): the 92-byte CSC set (mode word, matrices, ranges) from csc_para[ch]; -1 without one */
static int tisp_csc_get(u32 ch, u8 *out)
{
	u8 *const *e = (u8 *const *)csc_para[ch & 3];

	if (!e)
		return -1;
	return t33_csc_get(e[0], e[2], out);
}

static void t33_csc_trig(u32 ch, u32 mod)
{
	tisp_rcfg_module_cfg_trig(ch, mod);
}

static int t33_csc_wr(u32 ch)
{
	return t33_csc_write_reg(ch, t33_csc_full, t33_csc_cfg_mode, &t33_csc_refresh, &t33_csc_hw);
}

/* tisp_csc_fra_down_interrupt (system irq 48, block cscQ): refresh 1 starts list 13, 2 writes the slots again */
static void t33_csc_fra_down(const u32 *init)
{
	t33_csc_fra_down_interrupt(0, init, &t33_csc_refresh, t33_csc_trig, t33_csc_wr);
}

/*
 * tisp_csccr_bin_par_update at tisp_csccr_init (sw 0: bytes 13 / 14 = 0) and tisp_csccr_switch_para (sw 1:
 * byte 14 = 1, byte 13 from the bin): after tisp_csccr_para_refresh_by_mode the ranges of the section, or with
 * byte 13 above 1 the API ranges (synced from the section when byte 15 is 1, else csccr_ap); the vendor only
 * logs the result.
 */
static void t33_csccr_check(const u8 *sec, u32 sw)
{
	static const u8 ap[8] = { 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff };	/* csccr_ap + 4 */
	u8 p[T32_CSCCR_BYTES];

	memcpy(p, sec, sizeof(p));
	if (!sw)
		p[0xd] = 0;
	p[0xe] = sw;
	t32_csccr_para_refresh_by_mode(p[4], p + 5);
	t33_csccr_bin_par_update(p, p[15] == 1 ? p + 5 : ap);
}

/*
 * csccrInfo[0] (vendor layout): u16 word 0 / 1 the C / Y gain (0x14050 / 0x14040), bytes 8 / 9 / 10 the Y output
 * minimum, input minimum and output range (0x14044), word 3 the section, word 4 + bytes 20..27 the API mode and
 * ranges.  tisp_csccr_init sets the gains (tisp_csccr_switch_para only writes them again), here from the init
 * writes of t32_csccr_init.
 */
static u32 t33_swg_csccr_blk[8] = { 0, 0, 0, 0, 0, 0xff00ff00U, 0xff00ff00U };	/* csccr_ap */

static void t33_csccr_info_init(const struct t32_front_write *w, int r)
{
	int i;

	for (i = 0; i < r; i++)
		if (w[i].reg == 0x14050)
			t33_swg_csccr_blk[0] = w[i].val & 0xffff;
		else if (w[i].reg == 0x14040)
			t33_swg_csccr_blk[1] = w[i].val & 0xffff;
		else if (w[i].reg == 0x14044)
			t33_swg_csccr_blk[2] = ((w[i].val >> 16) & 0xff) | (w[i].val & 0xff) << 8 |
					       ((w[i].val >> 8) & 0xff) << 16;
}
#endif

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
#ifdef TX_ISP_T33
	t33_tisp_set_par(width, height, bayer & 0x1f, (u32)bayer >> 16, t32_ivdc_direct(0),
			 t33_core_sensor_dbus());	/* /proc/tisp level 0 / 4 (#589) */
#endif
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_CSC, &len);
#ifdef TX_ISP_T33
	csc_para[0] = NULL;	/* set by t33_csc_init_slots (tisp_csc_init) */
#endif
	if (off && T32_IQ_SEC_OK(len, T32_CSC_BYTES)) {
		n = t32_csc_init(bin + off, w);
		if (T32_SOC_LIST_IRQ)
			t33_mod_set(T33_MOD_CSC, w, n);
#ifdef TX_ISP_T33
		if (n)
			t33_csc_init_slots(bin + off);
#endif
		for (i = 0; !T32_SOC_LIST_IRQ && i < n; i++)
			isp_wr(w[i].reg, w[i].val);
	}
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_CSCCR, &len);
	if (off && T32_IQ_SEC_OK(len, T32_CSCCR_BYTES)) {
#ifdef TX_ISP_T33
		r = t33_csccr_cp_init(w);
		t33_csccr_check(bin + off, 0);
#else
		r = t32_csccr_init(bin + off, w);
#endif
#ifdef TX_ISP_T33
		t33_csccr_info_init(w, r);
#endif
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

		/* 0x100 = 5 for IVDC direct mode (vendor tisp_top_init: init par byte 0x2b == 1, sensor 0), else 4 (#680) */
		t33_top_init(bin + off, width, height, (u32)bayer, t32_ivdc_direct(0), w, dw);
		t33_tisp_top_save(bin + off);
		for (i = 0; i < T33_TOP_DIRECT_WRITES; i++)
			isp_wr(dw[i].reg, dw[i].val);
		if (t32_ldc_top_hldc())	/* tisp_top_ldc_bypass_refresh: HLDC takes the LDC out of bypass */
			w[0].val &= ~0x800000U;
		t33_mod_set(T33_MOD_TOP, w, T33_TOP_LIST_WRITES);
	}
	return;
#endif
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_TOP, &len);
	if (off && T32_IQ_SEC_OK(len, T32_TOP_BYTES)) {
		/* 0x100 = 5 in IVDC direct mode (vendor tisp_top_init: sensor info byte 0x27 = direct_mode, #687) */
		n = t32_top_init(bin + off, width, height, (u32)bayer, 0, (u32)t32_ivdc_direct(0), w);
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
		lce_work = t32_dma_zpages(T32_LCE_WORK_ORDER);
	if (!lce_curve)
		lce_curve = t32_dma_zpages(T32_LCE_CURVE_ORDER);
	if (!lce_hist)
		lce_hist = t32_dma_zpages(T32_LCE_HIST_ORDER);
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
#include "tx_isp_t33_ccmv.h"	/* CCM on the vendor ccm_para block, tispCcmRefresh (group 13d-1) */
static u32 t33_g14d_ccm_api;	/* CCM attribute sets that reached tisp_ccm_api_set (group 14d-1) */
static struct t33_lsc t33_lsc_m;
/*
 * BCSH block: t33_bcsh_st = the vendor's bcsh_para[0] block (0x290 B, word 0 the section = t33_bcsh_sec, the
 * tparamsP + 0x8b78 copy of the bin's section 19; group 13d-2), u32 aligned
 */
static u32 t33_bcsh_sec32[(T33_BCSH_SEC_BYTES + 3) / 4], t33_bcsh_st32[T33_BCSH_ST_BYTES / 4];
#define t33_bcsh_sec	((u8 *)t33_bcsh_sec32)
#define t33_bcsh_st	((u8 *)t33_bcsh_st32)
static void *bcsh_para[4];	/* the vendor's bcsh_para (tisp_bcsh_init, group 14c) */
#define T33_BCSH_INIT_EV	0x4000U		/* stIspInitPar +0x398: fstframe_defaultp EV (u64 low word) */
#define T33_GAMMA_INIT_IP	(T33_BCSH_INIT_EV >> 10)	/* fstframe_defaultp EV word >> 10, the vendor's init EV words */
/*
 * gamma on the vendor block gamma_para (tx_isp_t33_gammav.h, wiring group 13d-2): t33_gamma_v = the six
 * words { section (tparamsP + 0x5b80 = t33_gamma_sec, written: 0x42 / 0x43 / 0x45), cp, ip, sp, curve, ap };
 * stLutInfo[0] + 0x20 = t33_gamma_tab, the bin's curves copied at the init and at every bin switch (the vendor's
 * API-mode copy with sec[0x45] == 1 writes into it).  tispGammaRefresh: set by the EV update (1 list idle, else 2)
 * and the API set (1); the irq 46 frame done starts list 11.
 */
/*
 * curve records kept: sec[0x46] is a u8 and the vendor's tisp_bin_data_parse sizes its stLutInfo copy from the bin
 * (no curve limit), so every count a bin can carry is taken; before 16, a bin with more curves left gamma
 * unattached (openimp #628)
 */
#define T33_GAMMA_TAB_MAX	255U
static u32 t33_gamma_sec32[T33_GAMMA_SEC_BYTES / 4];
#define t33_gamma_sec	((u8 *)t33_gamma_sec32)
static u32 t33_gamma_cp32[T33_GV_CP_BYTES / 4], t33_gamma_ip32[2], t33_gamma_sp32[T33_GV_SP_BYTES / 4];
static u32 t33_gamma_cur32[T33_GV_SP_BYTES / 4], t33_gamma_ap32[T33_GV_AP_BYTES / 4];
static u32 t33_gamma_tab32[(T33_GAMMA_TAB_MAX * T33_GAMMA_CURVE_BYTES + 3) / 4];
#define t33_gamma_tab	((u8 *)t33_gamma_tab32)
static unsigned long t33_gamma_v[T33_GV_BLK_WORDS], gamma_para[4];
static u8 tispGammaRefresh;
static u32 gamma_count[4];
static u8 gammaCfgMode = 1;		/* slot numbers, as the other blocks */
u32 t33_gamma_runs[5];			/* attaches, EV writes, API sets, switches, frame dones with a refresh */

/* the section 13 and its curves into the block copies; 0 when usable (sec[0x46] curves behind it) */
static int t33_gamma_load(const u8 *sec, const u8 *tab, u32 tabbytes)
{
	u32 n = sec[0x46];

	if (!n || n > T33_GAMMA_TAB_MAX || tabbytes < n * T33_GAMMA_CURVE_BYTES)
		return -1;
	memcpy(t33_gamma_sec, sec, T33_GAMMA_SEC_BYTES);
	memcpy(t33_gamma_tab, tab, n * T33_GAMMA_CURVE_BYTES);
	return 0;
}

/*
 * tisp_gamma_init's block state (channel 0, process context): defaults gamma_cp / ip / sp / ap, ip = the init EV
 * words, the section with 0x42 / 0x43 = 0, interp_by_ev (forced), update_curve, strength_transform.  The list
 * itself is t33_gamma_init's (the same write_lut_rgb words at this EV).
 */
static void t33_gamma_attach(const u8 *sec, const u8 *tab, u32 tabbytes)
{
	unsigned long flags;
	u32 *ip = t33_gamma_ip32;

	local_irq_save(flags);
	gamma_para[0] = 0;
	tispGammaRefresh = 0;
	local_irq_restore(flags);
	if (t33_gamma_load(sec, tab, tabbytes))
		return;
	t33_gamma_v[0] = (unsigned long)t33_gamma_sec;
	t33_gamma_v[1] = (unsigned long)t33_gamma_cp32;
	t33_gamma_v[2] = (unsigned long)t33_gamma_ip32;
	t33_gamma_v[3] = (unsigned long)t33_gamma_sp32;
	t33_gamma_v[4] = (unsigned long)t33_gamma_cur32;
	t33_gamma_v[5] = (unsigned long)t33_gamma_ap32;
	t33_gv_defaults(t33_gamma_v);
	memcpy(t33_gamma_cur32, t33_gamma_ap_def, T33_GV_CURVE_BYTES);
	ip[0] = T33_GAMMA_INIT_IP;
	ip[1] = 0;
	t33_gamma_sec[0x42] = 0;
	t33_gamma_sec[0x43] = 0;
	t33_gv_interp(t33_gamma_v, ip[0], ip[1], 1);
	t33_gv_update_curve(t33_gamma_v, t33_gamma_tab, ip[0], ip[1]);
	t33_gv_transform(t33_gamma_v, t33_gamma_tab);
	gamma_para[0] = (unsigned long)t33_gamma_v;
	t33_gamma_runs[0]++;
}

static u32 t33_mdns_v[T33_MDNS_WORDS];
#define tispTopRefresh t33_top_refresh	/* the vendor name of the TOP refresh state (tx_isp_t33_top.h) */
#include "tx_isp_t33_bst.h"

static int t33_tmo_fd_wr(u32 ch, u32 blk, u32 reg, u32 a3)
{
	return tisp_reg_write_t33(ch, blk, reg, a3);
}

/* tisp_rcfg_module_cfg_trig: run the list at this frame end (the dirty refresh below, no list runs here) */
static int t33_tmo_fd_trig(u32 ch, u32 n)
{
	(void)ch;
	t33_mod_mark(1U << n);
	return 0;
}

static int t33_tmo_fd_nop(u32 idx)
{
	(void)idx;	/* tisp_tmo_ramV_vi23_reg_ctrl / _refresh: sensor modes >= 2 only (not supported) */
	return 0;
}

static const struct t33_tmo_fd_ops t33_tmo_fd_ops = { t33_tmo_fd_wr, t33_tmo_fd_trig, t33_tmo_fd_nop, t33_tmo_fd_nop };

static void t33_first_frame_words(void)
{
	u8 *info[4] = { (u8 *)t33_swg_tmo_info }, *tp[4] = { NULL };
	/* tisp_tmo_t33_frame_done_interrupt (oracle block tmoA): start word 0x70f0 (slot 32) = 0, tispTmoRefresh */
	if ((t33_mod_built & (1ULL << T33_MOD_TMO)) && t33_mod_buf(T33_MOD_TMO)) {
		t33_tmo_fd_refresh = !!(t33_mod_dirty & (1U << T33_MOD_TMO));
		t33_tmo_frame_done(&t33_tmo_fd_ops, info, tp, t33_st_isp_init_par_mode[226], t33_bst_cfg_mode,
				   &t33_tmo_fd_mulsensor, &t33_tmo_fd_refresh, 0);
	}
}

/* system irq 50: tisp_mdns_t33_frame_done_interrupt (oracle block mdnsA): mdnsInit / g_mdnsStart 0xd018 = 0, tispMdnsRefresh */
static void t33_mdns_fd(void)
{
	if (t33_mod_built & (1ULL << T33_MOD_MDNS))
		t33_mdns_frame_done(0, t33_st_isp_init_par_mode[226], &t33_mdns_init_flag, &t33_bst_refresh_mdns,
				    &t33_mdns_started, t33_bst_cfg_mode, tisp_reg_write_t33, tisp_rcfg_module_cfg_trig);
}
#include "tx_isp_t33_swglue.h"
#include "tx_isp_t33_g14glue.h"	/* group 14: vendor-form block init / refresh ports */
#include "tx_isp_t33_mdnsglue.h"	/* group 16: MDNS init / refresh on the vendor layout */
#include "tx_isp_t33_tmo1s.h"

/*
 * TMO one-shot (tx_isp_t33_tmo1s.h, oracle block tmo1s): the running mode
 * switch (IMP_ISP_OneShotDisable / Enable / Tick = core ioctls 0x100000e /
 * 0x100000f / 0x1000010) runs tisp_tmo_oneshot_disable / _enable / _tick
 * under the core lock, as the vendor.  Single sensor (stIspInitPar 904 = 1):
 * TMO slot 0x58 (0x70dc) off / on and list 8 restarted.  No list yet (before
 * the first stream-on): nothing to write, the trigger is skipped too (the
 * vendor builds its lists at AddSensor).  The AOV wake-up and multi-sensor
 * V-ram paths of the header are not reached in this driver (info = NULL).
 */
static u8 t33_tmo_aov_tik, t33_tmo_mulsensor;
static u16 t33_tmo_reg_num = T33_TMO1S_LIST_SHORT;	/* declared with the rcfg core */
static u32 t33_tmo1s_runs[3];

static u32 *t33_tmo1s_list(u32 ch)
{
	static u32 dummy[2];
	u32 *l = (t33_mod_built & (1ULL << T33_MOD_TMO)) ? t33_mod_buf(T33_MOD_TMO) : NULL;

	return l ? l : dummy;	/* no list: the vendor's word 0 store has no target */
}

static void t33_tmo1s_trig(u32 ch, u32 mod)
{
	if (t33_mod_built & (1ULL << (mod & 0xff)))
		tisp_rcfg_module_cfg_trig(ch, mod);
}

static const struct t33_tmo1s t33_tmo1s_ctx = {
	.cfg_mode = &t33_bst_cfg_mode, .aov_tik = &t33_tmo_aov_tik, .mulsensor = &t33_tmo_mulsensor,
	.reg_num = &t33_tmo_reg_num, .initpar = (const u8 *)t33_st_isp_init_par_mode, .info = NULL,
	.list = t33_tmo1s_list, .wr = t33_bst_wr_v, .trig = t33_tmo1s_trig, .vi0_refresh = NULL,
};

void t33_tmo_oneshot(u32 op)
{
	unsigned long flags;

	local_irq_save(flags);
	if (op == 0)
		t33_tmo1s_disable(&t33_tmo1s_ctx);	/* lib_tisp_tmo_oneshot_disable */
	else if (op == 1)
		t33_tmo1s_enable(&t33_tmo1s_ctx);	/* lib_tisp_tmo_oneshot_enable */
	else
		t33_tmo1s_tick(&t33_tmo1s_ctx);		/* lib_tisp_tmo_oneshot_tick */
	local_irq_restore(flags);
	t33_tmo1s_runs[op > 2 ? 2 : op]++;
}

/*
 * LDC ioctls of ldc_core_ops_ioctl (T33): the HLDC bypass state (tisp_ldc_set_bypass_state ->
 * tisp_top_ldc_bypass_refresh) and the LUT refresh (tisp_online_ldc_reflesh_lut), as the oracle-proven ports
 * tx_isp_t33_ldcpar.h (block ldc) and tx_isp_t33_aeldc.h (block aeldc).  The vendor's module data objects used by
 * the aeldc port are these shims (ldcCfgMode: slot indexes like the other blocks; stIspInitPar word 226 = 1).
 */
u8 ldcCfgMode = 1;
extern u8 stIspInitPar[4032] __attribute__((alias("t33_st_isp_init_par_mode")));
u32 g_ae_contexts[4];
static int t33_ldc_sysw(u32 reg, u32 val)
{
	isp_wr(reg, val);
	return 0;
}
#define system_reg_write t33_ldc_sysw
#define normal_ae_dn_par_refresh t33_aec_dn_par_refresh	/* tx_isp_t33_aec.c (tx_isp_t33_misc1.h) */
#include "tx_isp_t33_aeldc.h"
#include "tx_isp_t33_ldcpar.h"
#undef system_reg_write
#undef normal_ae_dn_par_refresh

/* tisp_top_ldc_bypass_refresh: LDC bypass bit (0x800000) of the TOP bypass word from block[idx], TOP list rerun */
static void t33_ldc_bypass_refresh(u32 idx, const u8 *blk)
{
	u32 sel = ipcCfgMode ? 1 : 64, v;

	idx &= 0xff;
	v = tisp_reg_read_t33(idx, 33, sel);
	v = blk[idx] == 1 ? v & ~0x800000U : v | 0x800000U;
	tisp_reg_write_t33(idx, 33, sel, v);
	tispTopRefresh = tisp_rcfg_get_dma_state(33) ? 2 : 1;
}

/* IOC 0xc00454bc: {idx, state}; vendor core command 0x5000009 */
void t33_ldc_set_bypass_state(u32 idx, u32 state)
{
	u8 blk[4];

	if (t33_ldc_bypass_buf(blk, idx, state) >= 4)
		return;		/* the vendor writes past its block here (BEYOND_VENDOR row) */
	t33_ldc_bypass_refresh(idx & 0xff, blk);
}

/* IOC 0xc00454bd: {idx, base, offset}; vendor core command 0x500000a */
int t33_ldc_reflesh_lut(u32 idx, u32 addr)
{
	return t33_online_ldc_refresh(idx, addr);
}

/*
 * tisp_s_module_control / tisp_g_module_control (oracle-proven tx_isp_t33_top.h, block top; the side effects
 * tx_isp_t33_mcside.h, block mcside): the module control word is the TOP bypass word (bit set = module bypassed),
 * low 22 bits the user's.  A changed bit 12 starts MDNS before the new word (tisp_mdns_start) and triggers it after
 * (tisp_mdns_t33_reg_trig); changed bits 5 / 11 (0x820) switch the LCE curve source (tisp_lce_top_change_state).
 */
#include "tx_isp_t33_mcside.h"

static struct t33_lce_pf *t33_lcepf;	/* the LCE block (defined below, with the LCE / LSF code) */

static void t33_lce_curve_default_cb(u32 ch)
{
	if (t33_lcepf)
		t33_lcepf_curve_default(t33_lcepf);
}

void t33_module_control_set(u32 ch, u32 v)
{
	u32 cur = t33_top_get_bypass(ch), chg = v ^ cur;

	if (chg & 0x1000)
		t33_mdns_start(ch, t33_bst_cfg_mode, &t33_mdns_started);
	if (chg & 0x820)
		t33_lce_top_change_state(t33_lce_sec, ch, t33_lce_curve_default_cb);
	t33_top_set_bypass(ch, (v & 0x3fffffU) | (cur & 0xffc00000U));
	if (chg & 0x1000)	/* tisp_mdns_t33_reg_trig */
		t33_mdns_reg_trig(ch, t33_bst_cfg_mode, t33_mdns_init_flag, &t33_bst_refresh_mdns);
}

u32 t33_module_control_get(u32 ch)
{
	return t33_top_get_bypass(ch) & 0x3fffffU;
}

#include "tx_isp_t33_intp2.h"
#include "tx_isp_t33_r13A.h"
#include "tx_isp_t33_tmoR.h"
#include "tx_isp_t33_tmoSW.h"

/*
 * g16-1, TMO init: lib_tisp_init's tisp_tmo_init (oracle block tmoR) on the vendor-layout tmoInfo[0]
 * (t33_swg_tmo_info), section = tparamsP + 10140 (t33_swg_tmo_sec, the bin's section copy), par = stIspInitPar
 * of channel 0 with the sensor size (u16 width / height at 12 / 14, EV words 920 / 924 = fstframe_defaultp).
 * Callees: tisp_tmo_tmo_size_update / _modechange_stat_refresh (tmoP), func_bin_para_update /
 * func_tmo_ic_para_update / tisp_tmo_s2s3_ddrV_init (tmoA), tisp_{ev,bv}_intp_{uint8,int8,uint16}_t (blk, intp,
 * intp2), tisp_tmo_tmo_lumcurve_ev_interp / _satcurve_ev_interp / _ramY_vi0_reg_refresh / _ramV_vi0_reg_refresh /
 * _rcfg_reg_num_disV (r13A).  tisp_tmo_drc_ctrl (not ported) returns at once while the u16 at 10840 is not 1
 * (vendor e055c; the init zeroes it, DRC never on in this driver): counted when it is.  The V table buffer
 * (kmalloc 6000) = t33_tmo_pages (KSEG0, + 0x80000000 = its physical address as the vendor computes it).
 * tisp_t33_tmo_rcfg_reg_num: the vendor sets 1701 before the register and its reg_init writes it as the
 * length; the driver's list page holds 1024 words, so the register keeps the short layout and disV sets the
 * vendor's final 196 (the list run is the same); slot stores before disV go to the page unchecked as in the
 * vendor's tisp_reg_write_t33.  irqs 9 / 42 / 11: the driver's fixed set-up (counted when they differ).
 * Before: the driver-form list builder t33_tmo_init (tx_isp_t33_tmo.h) on a separate state.
 */
static const u16 t33_tmo_curve_x[201] = {	/* static_tmo_curve_x (vendor .data +36544, 402 bytes) */
	0, 508, 1018, 1528, 2038, 2548, 3057, 3566, 4075, 4584, 4839, 5094, 5350, 5478, 5606, 5733,
	5861, 5925, 5989, 6053, 6117, 6180, 6244, 6308, 6372, 6404, 6436, 6468, 6500, 6532, 6564, 6596,
	6628, 6660, 6692, 6724, 6756, 6788, 6820, 6852, 6884, 6900, 6917, 6933, 6949, 6965, 6981, 6997,
	7013, 7029, 7045, 7061, 7077, 7093, 7110, 7126, 7142, 7158, 7174, 7190, 7206, 7222, 7239, 7255,
	7271, 7287, 7303, 7319, 7336, 7352, 7368, 7384, 7400, 7409, 7417, 7425, 7433, 7441, 7449, 7457,
	7465, 7473, 7482, 7490, 7498, 7506, 7514, 7522, 7530, 7538, 7547, 7555, 7563, 7571, 7579, 7587,
	7595, 7603, 7612, 7620, 7628, 7636, 7644, 7652, 7661, 7669, 7677, 7685, 7693, 7701, 7710, 7718,
	7726, 7734, 7742, 7751, 7759, 7767, 7775, 7784, 7792, 7800, 7808, 7817, 7825, 7833, 7841, 7850,
	7858, 7866, 7875, 7883, 7891, 7900, 7908, 7916, 7924, 7929, 7933, 7937, 7941, 7945, 7949, 7954,
	7958, 7962, 7966, 7970, 7974, 7979, 7983, 7987, 7991, 7995, 7999, 8004, 8008, 8012, 8016, 8020,
	8024, 8028, 8033, 8037, 8041, 8045, 8049, 8053, 8058, 8062, 8066, 8070, 8074, 8078, 8083, 8087,
	8091, 8095, 8099, 8103, 8108, 8112, 8116, 8120, 8124, 8128, 8132, 8137, 8141, 8145, 8149, 8153,
	8157, 8161, 8166, 8170, 8174, 8178, 8182, 8186, 8191,
};
static u8 t33_swg_tmo_sec[T33_TMO_SEC_BYTES], t33_swg_tmo_refresh;	/* tparamsP + 10140, tispTmoRefresh */
static u8 *t33_g16_tmo_info[4] = { (u8 *)t33_swg_tmo_info }, *t33_g16_tmo_tp[4];
static u8 t33_g16_tmo_par[928];		/* stIspInitPar of channel 0 + the sensor size */
static u16 t33_g16_tmo_regnum;		/* tisp_t33_tmo_rcfg_reg_num as tisp_tmo_init sets it (1701) */
u32 t33_g16_runs[4];			/* tmo init, irq mismatches, DRC on (drc_ctrl not ported), bad slots */

static const u8 *t33_g16_curve_x(void)
{
	return (const u8 *)t33_tmo_curve_x;	/* LE u16 like the vendor's .data (MIPS el) */
}

/* tisp_tmo_drc_ctrl: returns at once with the u16 at 10840 != 1 (vendor e055c) */
static void t33_g16_tmo_drc(u32 ch)
{
	t33_g16_runs[2] += ((u16 *)t33_swg_tmo_info)[10840 / 2] == 1;
	(void)ch;
}

/* tisp_reg_write_t33 on list 8 at init: the slot of the page (no length check, as the vendor) or the register */
static void t33_g16_tmo_wr(u32 ch, u32 blk, u32 reg, u32 val)
{
	u32 *l;

	(void)ch;
	if (!t33_bst_cfg_mode) {
		isp_wr(reg, val);
		return;
	}
	l = (blk == T33_MOD_TMO && (t33_mod_built & (1ULL << T33_MOD_TMO))) ? t33_mod_buf(T33_MOD_TMO) : NULL;
	if (l && reg + 1 < T33_SLOT_WORDS)
		l[reg + 1] = val;
	else
		t33_g16_runs[3]++;
}

static void t33_g16_tmo_sysw(u32 reg, u32 val)
{
	isp_wr(reg, val);
}

static void t33_g16_tmo_v23(u32 ch)
{
	(void)ch;	/* tisp_tmo_ramV_vi23_reg_ctrl / _refresh: sensor modes >= 3 only (not supported) */
}

static const struct t33_r13a_ops t33_g16_r13a_init = {
	t33_g16_tmo_drc, t33_g16_tmo_wr, t33_g16_tmo_sysw, t33_g16_tmo_v23,
};

static int t33_g16_tmo_reg(u32 ch, u32 id)
{
	return t33_g14_reg(ch, id, NULL);
}

static void t33_g16_tmo_regfail(void)
{
	t33_g14_reg_fail(16, -1);
}

static void *t33_g16_tmo_kmalloc(u32 size, u32 flags)
{
	(void)flags;
	if (size == 10848) {
		memset(t33_swg_tmo_info, 0, sizeof(t33_swg_tmo_info));
		return t33_swg_tmo_info;
	}
	if (size == T33_TMO_V_BYTES && t33_tmo_pages)
		return (void *)t33_tmo_pages;
	return NULL;
}

static void t33_g16_tmo_allocfail(void)
{
	t33_g14_alloc_fail(16);
}

static void t33_g16_tmo_size(u32 ch)
{
	u32 *v = (u32 *)t33_g16_tmo_info[ch & 3];

	t33_tmop_size_update(v, (u8 *)(unsigned long)v[T33_TMOA_SEC_PTR / 4], t33_g16_tmo_par);
}

static u32 t33_g16_tmop_rd(u32 a)
{
	return isp_rd(a);
}

static void t33_g16_tmop_soft(u32 ch)
{
	(void)ch;
}

static void t33_g16_tmop_sync(u32 a0, void *p, u32 n, u32 d)
{
	(void)a0;
	(void)d;
	dma_cache_wback_inv((unsigned long)p, n);
}

static void t33_g16_tmop_copy(void *d, const void *s, u32 n)
{
	memcpy(d, s, n);
}

static const struct t33_tmop_ops t33_g16_tmop_ops = {
	t33_g16_tmop_rd, t33_g16_tmo_sysw, t33_g16_tmop_soft, t33_g16_tmop_sync, t33_g16_tmop_copy,
};

static void t33_g16_tmo_mode(u32 ch)
{
	t33_tmop_mode_refresh((u32 *)t33_g16_tmo_info[ch & 3], t33_g16_curve_x(), &t33_g16_tmop_ops);
}

static void t33_g16_tmo_bin(u32 ch)
{
	u32 *v = (u32 *)t33_g16_tmo_info[ch & 3];

	t33_tmo_bin_para(v, (const u8 *)(unsigned long)v[T33_TMOA_SEC_PTR / 4]);
}

static u32 t33_g16_ev_u8(u32 x, const u8 *e, const u8 *t)
{
	return t33_ev_intp_u8(x, e, t);
}

static u32 t33_g16_ev_s8(u32 x, const u8 *e, const u8 *t)
{
	return (u32)t33_ev_intp_s8(x, e, t);
}

static u32 t33_g16_ev_u16(u32 x, const u8 *e, const u8 *t)
{
	return t33_ev_intp_u16(x, e, t);
}

static u32 t33_g16_bv_u8(u32 x, const u8 *e, const u8 *t)
{
	return t33_bv_intp_u8(x, e, t);
}

static u32 t33_g16_bv_s8(u32 x, const u8 *e, const u8 *t)
{
	return (u32)t33_bv_intp_s8(x, e, t);
}

static u32 t33_g16_bv_u16(u32 x, const u8 *e, const u8 *t)
{
	return t33_bv_intp_u16(x, e, t);
}

static void t33_g16_tmo_lum(u32 ch)
{
	t33_tmo_lumcurve_interp(t33_g16_tmo_info[ch & 3], t33_g16_curve_x());
}

static void t33_g16_tmo_sat(u32 ch)
{
	t33_tmo_satcurve_interp(t33_g16_tmo_info[ch & 3]);
}

static void t33_g16_tmo_ic(u32 ch)
{
	t33_tmo_ic_para((u32 *)t33_g16_tmo_info[ch & 3]);
}

static void t33_g16_tmo_ramy(u32 ch)
{
	t33_tmo_ramy_refresh(&t33_g16_r13a_init, t33_g16_tmo_info, t33_bst_cfg_mode, ch & 3);
}

/* tisp_tmo_s2s3_ddrV_init: the packed V table into the buffer, private_dma_cache_sync (0, buf, 6000, 1) */
static void t33_g16_tmo_ddrv(u32 ch)
{
	const u8 *info = t33_g16_tmo_info[ch & 3];
	u32 *out = (u32 *)(unsigned long)t33_le32(info + T33_TMOA_DDRV_PTR);

	if (!out)
		return;
	t33_tmo_ddrv_pack(info, out);
	dma_cache_wback_inv((unsigned long)out, T33_TMO_V_BYTES);
}

/* tisp_tmo_rcfg_reg_num_disV: stModuleAttr's list page pointer = the driver's list 8 page */
static void t33_g16_tmo_disv(u32 ch)
{
	static u8 attr[236] __aligned(4);	/* stModuleAttr of channel 0 up to the list page pointer */
	u32 *l = t33_mod_buf(T33_MOD_TMO);

	if (!l || (ch & 0xff))
		return;
	*(u32 *)(attr + 232) = (u32)(unsigned long)l;
	t33_tmo_rcfg_reg_num_disV(attr, &t33_tmo_reg_num, 0);
}

static void t33_g16_tmo_vi0(u32 ch)
{
	t33_tmo_ramv_refresh(&t33_g16_r13a_init, t33_g16_tmo_info, t33_le32((const u8 *)t33_st_isp_init_par_mode + 904),
			     t33_bst_cfg_mode, ch & 3);
}

static void t33_g16_tmo_irq(u32 ch, u32 irq)
{
	(void)ch;
	t33_g16_runs[1] += irq != 9U && irq != 42U && irq != 11U;
}

static const struct t33_tmoR_ops t33_g16_tmoR_ops = {
	.module_register = t33_g16_tmo_reg, .warn_register = t33_g16_tmo_regfail, .kmalloc = t33_g16_tmo_kmalloc,
	.warn_alloc = t33_g16_tmo_allocfail, .reg_write = t33_g16_tmo_wr, .sys_write = t33_g16_tmo_sysw,
	.size_update = t33_g16_tmo_size, .modechange = t33_g16_tmo_mode, .bin_update = t33_g16_tmo_bin,
	.intp = { { t33_g16_ev_u8, t33_g16_ev_s8, t33_g16_ev_u16 }, { t33_g16_bv_u8, t33_g16_bv_s8, t33_g16_bv_u16 } },
	.lumcurve = t33_g16_tmo_lum, .satcurve = t33_g16_tmo_sat, .ic_para = t33_g16_tmo_ic, .ramy = t33_g16_tmo_ramy,
	.ddrv_init = t33_g16_tmo_ddrv, .vi23_ctrl = t33_g16_tmo_v23, .vi23_refresh = t33_g16_tmo_v23,
	.disv = t33_g16_tmo_disv, .vi0_refresh = t33_g16_tmo_vi0, .irq_set = t33_g16_tmo_irq,
};

/* lib_tisp_init's tisp_tmo_init (ch 0); s the bin's TMO section */
static noinline_for_stack void t33_g16_tmo_init(const u8 *s, u32 width, u32 height)
{
	u32 iadam;

	memcpy(t33_swg_tmo_sec, s, sizeof(t33_swg_tmo_sec));
	memcpy(t33_g16_tmo_par, t33_st_isp_init_par_mode, sizeof(t33_g16_tmo_par));
	t33_defog_put16(t33_g16_tmo_par + 12, (u16)width);
	t33_defog_put16(t33_g16_tmo_par + 14, (u16)height);
	t33_g16_tmo_tp[0] = t33_swg_tmo_sec - T33_TMOSW_SEC_OFF;
	t33_tmoR_init(&t33_g16_tmoR_ops, 0, t33_g16_tmo_par, t33_g16_tmo_info, t33_g16_tmo_tp, &t33_bst_cfg_mode,
		      &t33_swg_tmo_refresh, (const u8 *)t33_st_isp_init_par_mode, &iadam, &t33_g16_tmo_regnum);
	t33_g16_runs[0]++;
}

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
		if (!t33_tmo_pages)
			t33_tmo_pages = t32_dma_zpages(T33_TMO_ORDER);
		/* tisp_tmo_init (group 16): the V table buffer, then the vendor's init incl. its direct writes */
		if (t33_tmo_pages)
			t33_g16_tmo_init(bin + off, width, height);
	}
	if (!t32_core_sensor_size(&width, &height)) {
		u32 *buf = t33_mod_buf(T33_MOD_MSCA), i;

		/* MSCA init list (single sensor: per-channel 0xf0048 blocks at 0xf06b4) */
		off = t32_iq_section(bin, bytes, T33_SEC_MSCA, &len);
		if (buf) {
			t33_osd_lock();	/* no tuning OSD / flip call between the list build and the rebuilt words (#578) */
			for (i = 0; i < 3; i++)
				isp_wr(0xf0128 + 0x100 * i, 0);
			t33_mod_done(T33_MOD_MSCA, t33_msca_init(width, height, off && len > 0x30 ? bin + off : NULL,
								 buf, T33_SLOT_WORDS));
			t33_msca_attr_stream_on(width, height);	/* stMscaAttr[0]: tisp_msca_t33_init (tx_isp_t33_osd.c) */
			t33_osd_list_rebuilt();	/* OSD / mask set before a stream-off survive in the new list */
			t33_osd_unlock();
		}
	}
	off = t32_iq_section(bin, bytes, T33_SEC_CLM, &len);
	if (off && len == T33_CLM_SEC_BYTES) {
		u32 *buf = t33_mod_buf(T33_MOD_CLM);

		/* CT 5000 K as the CCM; tisp_csc_get on csc_para for the csc set */
		if (buf)
			t33_mod_done(T33_MOD_CLM, t33_clm_init(bin + off, bin + off + len,
							       t32_iq_payload(bin + off, bytes - off, T33_SEC_CLM),
							       tisp_csc_get, T33_CCM_INIT_CT, buf, T33_SLOT_WORDS));
	}
	off = t32_iq_section(bin, bytes, T33_SEC_BCSH, &len);
	if (off && len == T33_BCSH_SEC_BYTES) {
		/*
		 * tisp_bcsh_init (group 14c, port bcshN) on stIspInitPar's first-frame inputs (fstframe_defaultp, no
		 * RISC-V): CT +0x3d0 = 5000 K, EV +0x398 = 0x4000 (state word 8 = EV >> 10), words +0x3d8 / +0x3dc;
		 * tisp_csc_get on csc_para for the csc set
		 */
		t33_g14c_bcsh_init(bin + off, bin + off + len, t32_iq_payload(bin + off, bytes - off, T33_SEC_BCSH));
	} else {
		bcsh_para[0] = NULL;
	}
}

/*
 * MDNS: tisp_mdns_init on the vendor layout (tx_isp_t33_mdnsglue.h, group 16: init gain, the driver's memopt as
 * the zip ratio, 0xd084 = 2 direct); then tisp_mdns_t33_set_mem_cfg on the ISP working memory libimp handed over
 * (SET_ISP_MEM_INFO, 0xd080 direct, single sensor) and the decode words from the YSP block state.
 */
static noinline_for_stack void t33_tisp_init_mdns(const u8 *bin, u32 bytes)
{
	u32 off, len, width, height, pa, r = t32_core_memopt(), d080 = 0, *buf;

	off = t32_iq_section(bin, bytes, T33_SEC_MDNS, &len);
	if (!off || len != T33_MDNS_SEC_BYTES || t32_core_sensor_size(&width, &height) || t32_core_isp_mem(&pa))
		return;
	if (t33_mdnsg_init(bin + off, width, height, r) || !(t33_mod_built & (1ULL << T33_MOD_MDNS)))
		return;
	buf = t33_mod_buf(T33_MOD_MDNS);
	if (!buf)
		return;
	t33_mdns_mem(width, height, r < 1001 ? r : r / 100 * 100, pa, t33_mdns_v, buf, &d080);
	isp_wr(0xd080, d080);
	tisp_rcfg_module_cfg_trig(0, 15);	/* tisp_mdns_t33_set_mem_cfg ends with trig(0, 15) (0x8b6f0, #755) */
	if (t33_mod_built & (1ULL << T33_MOD_YSP)) {
		t33_mdns_decode(t33_ysp_v, buf);
		t33_bst_refresh_mdns = 1;	/* tisp_mdns_decode_reg_write: tispMdnsRefresh = 1 (0x97dd4 / 0x97e0c, #755) */
	}
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
		t33_ae_stat_pages = t32_dma_zpages(get_order(T33_AE_STAT_BYTES));
	if (!t33_ae_hist_pages)
		t33_ae_hist_pages = t32_dma_zpages(get_order(T33_AE_HIST_BYTES));
	/* tisp_ae_weight_mean's grid: rows 0xad2, columns 0xad6 (15 a row in the buffer) */
	t33_ae_rows = min_t(u32, t32_tp_le16(bin + off + 0xad2), T33_AE_GRID);
	t33_ae_cols = min_t(u32, t32_tp_le16(bin + off + 0xad6), T33_AE_GRID);
	buf = t33_mod_buf(T33_MOD_AE);
	if (buf && t33_ae_stat_pages && t33_ae_hist_pages) {
		int n = t33_ae_init(bin + off, width, height, (u32)virt_to_phys((void *)t33_ae_stat_pages),
				    (u32)virt_to_phys((void *)t33_ae_hist_pages), buf, T33_SLOT_WORDS);

		/* tisp_ae_malloc_info: the AE hardware parameter block (g_ae_contexts[0]) and list 30 slots 7..14 */
		if (n > 0 && t33_aec_malloc_info(buf, t33_ae_stat_pages, t33_ae_hist_pages))
			n = -ENOMEM;
		t33_mod_done(T33_MOD_AE, n);
	}
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
#endif /* TX_ISP_T33 */

/*
 * Sensor mirror / flip (openimp #669): tx_isp_core_shvflip (vendor tx-isp-t33.ko 0x102e0c) as the
 * vendor's ispcore_irq_fd_work0 runs it for the pending frame-done entry 5 that 0x73 queues
 * (tx_isp_core_ops_s_ctrl, the IMPISPHVFLIPAttr sensor_mode): after the next frame end, in process
 * context (the sensor writes over i2c), VIC control 0x4444 on both VICs (lib_tisp_vic_ctrl:
 * 0x801a8 / 0x811a8: the VICs hold the frames), the sensor ioctl 0x2000010 { ch, &msg } with
 * msg[0] = the mode, the entry cleared, then the core event 0x1000007 { max(msg[1], 1), 0x1000009 }:
 * ispcore_core_ops_ioctl passes it to the subdevs, vic_core_ops_ioctl arms a frame count
 * (vic +2384 / +2400 / +2404), and isp_vic_interrupt_service_routine counts VIC frame starts
 * (0x1e0 bit 4) down and then writes 0x4440 (linear) back to both VICs (t33_vic_hold_tick) and
 * notifies 0x1000009.  That notify (ispcore_core_ops_ioctl 0x1000009: mbus_to_bayer_write for an
 * input whose synced video_in +48 flag is set) rewrites the bayer of the unchanged mbus code: the
 * T33 sensors keep the code and move the bayer phase themselves (cv2005 register 0x3028), so it is
 * not run here.  Without the count the VICs stay on 0x4444 and the ISP gets no frame (device, #669).
 * T32 (openimp #689): the same flow in tx-isp-t32.ko (tx_isp_core_shvflip 0xc4ee8, ispcore_irq_fd_work0
 * entry 5 at core +0x540 / +0x544, vic_core_ops_ioctl 0x1000007, isp_vic_interrupt_service_routine);
 * its release writes 0x4410 instead of 0x4440 when the sensor attribute's data type (+0x94) is 2.  The
 * T32 sensors keep the mbus code on a flip as well (gc4653: register 0x0101 only).
 */
static DEFINE_SPINLOCK(t33_flip_lock);
static u32 t33_flip_pend, t33_flip_mode;
static int t33_vic_hold;		/* VIC frame starts left before 0x4440 (vic +2384) */
#ifndef TX_ISP_T33
static u32 t32_flip_vic_rel = 0x4440;	/* T32 release value: 0x4410 for sensor data type 2 */
#endif
static struct {
	u32 requests, applied, mode, holds, releases;
	int ret;
	u32 deferred, stop_releases;	/* #779: work runs without a streaming sensor, holds dropped at stop */
#ifndef TX_ISP_T33
	u32 sns_frames;			/* #898: the sensor's last drop_frame answer (msg[1]; 0 = no answer) */
#endif
} t33_flip_st;

/* isp_vic_interrupt_service_routine, frame start (0x1e0 bit 4) with the hold armed: count down, release */
static void t33_vic_hold_tick(u32 sta)
{
	unsigned long fl;
	bool rel = false;

	if (!ACCESS_ONCE(t33_vic_hold) || !(sta & 0x10))
		return;
	spin_lock_irqsave(&t33_flip_lock, fl);
	if (t33_vic_hold && --t33_vic_hold <= 0) {
		t33_vic_hold = 0;
		t33_flip_st.releases++;
		rel = true;
	}
	spin_unlock_irqrestore(&t33_flip_lock, fl);
	if (rel) {
#ifdef TX_ISP_T33
		t32_vic_wr(0, 0x1a8, 0x4440);
		t32_vic_wr(1, 0x1a8, 0x4440);
#else
		t32_vic_wr(0, 0x1a8, t32_flip_vic_rel);
		t32_vic_wr(1, 0x1a8, t32_flip_vic_rel);
#endif
	}
}

/* #659: the sensor flip mode the last SetHVFLIP applied (bit 0 mirror, bit 1 flip) */
u32 t33_hw_flip_mode(void)
{
	return t33_flip_st.mode;
}

static void t33_flip_work(struct work_struct *w)
{
	unsigned long fl;
	u32 mode, frames = 0;
	int ret;

	(void)w;
	if (!ACCESS_ONCE(t33_flip_pend) || !isp_base)
		return;
	/*
	 * #779: under core.lock, rechecked there: stream-off, RELEASE_SENSOR (frees the sensor subdev) and
	 * the last close (clock gate) hold it, so the sensor and the VICs stay valid until the hold is armed.
	 * Not streaming: the request stays pending for the next stream's first frame end (the vendor entry
	 * +1340 is cleared only by ispcore_irq_fd_work0).
	 */
	t32_core_lock();
	spin_lock_irqsave(&t33_flip_lock, fl);
	mode = t33_flip_mode;
	ret = t33_flip_pend;
	spin_unlock_irqrestore(&t33_flip_lock, fl);
	if (!ret || !t32_core_flip_ready()) {
		if (ret)
			t33_flip_st.deferred++;
		t32_core_unlock();
		return;
	}
#ifndef TX_ISP_T33
	t32_flip_vic_rel = t32_core_sensor_data_type() == 2 ? 0x4410 : 0x4440;
#endif
	t32_vic_wr(0, 0x1a8, 0x4444);
	t32_vic_wr(1, 0x1a8, 0x4444);
	ret = t32_core_sensor_flip(0, mode, &frames);
	spin_lock_irqsave(&t33_flip_lock, fl);
	t33_flip_pend = 0;
	t33_vic_hold = (int)frames > 0 ? (int)frames : 1;	/* core event 0x1000007 { max(msg[1], 1), .. } */
	t33_flip_st.holds++;
	t33_flip_st.applied++;
	t33_flip_st.mode = mode;
	t33_flip_st.ret = ret;
#ifndef TX_ISP_T33
	t33_flip_st.sns_frames = frames;
#endif
	spin_unlock_irqrestore(&t33_flip_lock, fl);
	t32_core_unlock();
}

/*
 * #779 (beyond vendor, the vendor keeps vic +2384 armed): under core.lock at stream-off and at the last
 * close.  A VIC hold that is still armed (the flip / bin switch held the VICs on 0x4444 and no frame start
 * counted it down) is dropped, and while the clocks run the VICs get their release value back now instead
 * of at the next stream's first frame start (whose stream-on sequence rewrites 0x1a8 anyway).  A pending
 * flip request stays (vendor: applied at the next frame end).
 */
void t33_hw_flip_stop(void)
{
	unsigned long fl;
	bool rel;

	spin_lock_irqsave(&t33_flip_lock, fl);
	rel = t33_vic_hold != 0;
	t33_vic_hold = 0;
	if (rel)
		t33_flip_st.stop_releases++;
	spin_unlock_irqrestore(&t33_flip_lock, fl);
	if (!rel || !isp_base || !t32_core_clk_active())
		return;
#ifdef TX_ISP_T33
	t32_vic_wr(0, 0x1a8, 0x4440);
	t32_vic_wr(1, 0x1a8, 0x4440);
#else
	t32_vic_wr(0, 0x1a8, t32_flip_vic_rel);
	t32_vic_wr(1, 0x1a8, t32_flip_vic_rel);
#endif
}
static DECLARE_WORK(t33_flip_w, t33_flip_work);

/* 0x73 (tx_isp_t32_tunctl.c): the vendor's +1340 / +1344 entry of input ch; one input on T33 */
void t33_core_flip_request(u32 ch, u32 mode)
{
	unsigned long fl;

	if (ch)
		return;
	spin_lock_irqsave(&t33_flip_lock, fl);
	t33_flip_pend = 1;
	t33_flip_mode = mode;
	t33_flip_st.requests++;
	spin_unlock_irqrestore(&t33_flip_lock, fl);
}

/* frame end (the vendor queues ispcore_irq_fd_work0 there) */
static void t33_flip_kick(void)
{
	if (ACCESS_ONCE(t33_flip_pend))
		schedule_work(&t33_flip_w);
}

#ifdef TX_ISP_T33
/*
 * SET_TIZIANO_PAR of a streaming sensor (openimp #692): the vendor ispcore_core_ops_ioctl 0x100001e
 * (tx-isp-t33.ko 3028d4ac 0x103d8c..0x103eac, tx_isp_t33_binsw.h): VICs on 0x4444, wait until the
 * IPC output position (0x30200) is 0 (2 ms steps, 101 tries), lib_tisp_refresh_para +
 * lib_tisp_set_csccr, then the core event 0x1000007 { 1 } arms one VIC frame start, after which the
 * VIC interrupt writes 0x4440 back (t33_vic_hold_tick).  One sensor (priv +1192 == 1).  Not streaming
 * the vendor leaves the VICs on 0x4444 until the stream-on sequence rewrites 0x1a8 and refreshes
 * nothing (ispInitFinish): here nothing is done then (the same picture, no VIC write before the
 * clocks run).
 */
static struct {
	u32 calls, holds, ipc_waits, ipc_timeouts;
} t33_binsw_st;

static void t33_binsw_vic_ctrl(u32 vic, u32 val)
{
	t32_vic_wr((int)vic, 0x1a8, val);
}

static u32 t33_binsw_ipc_pos(void)
{
	u32 v = isp_rd(0x30200);	/* lib_tisp_get_ipc_output_pos: system_reg_read(0x30200) */

	if (v)
		t33_binsw_st.ipc_waits++;
	return v;
}

static void t33_binsw_msleep(u32 ms)
{
	msleep(ms);
}

static void t33_binsw_print(u32 level, u32 line)
{
	t33_binsw_st.ipc_timeouts++;
	isp_printf(level, "ispcore_core_ops_ioctl[%u]: IPC output position not 0 before the bin switch\n", line);
}

static void t33_binsw_refresh(const u8 *req)
{
	t33_refresh_para(req[12]);
}

static int t33_binsw_event(u32 sd, u32 cmd, const u32 *ev)
{
	unsigned long fl;

	(void)sd;
	(void)cmd;
	spin_lock_irqsave(&t33_flip_lock, fl);
	t33_vic_hold = (int)ev[0] > 0 ? (int)ev[0] : 1;	/* vic_core_ops_ioctl 0x1000007: max(ev[0], 1) frames */
	t33_flip_st.holds++;
	t33_binsw_st.holds++;
	spin_unlock_irqrestore(&t33_flip_lock, fl);
	return 0;
}

static const struct t33_binsw_ops t33_binsw_ops = {
	.vic_ctrl = t33_binsw_vic_ctrl,
	.ipc_pos = t33_binsw_ipc_pos,
	.msleep = t33_binsw_msleep,
	.print = t33_binsw_print,
	.refresh_para = t33_binsw_refresh,
	.set_csccr = t33_set_csccr,
	.event = t33_binsw_event,
};

/* tx_isp_t32_core.c t32_set_tiziano_par: req = the 20-byte request, vinum 0 while streaming */
void t33_core_bin_switch(const void *req)
{
	if (!isp_base || !t32_core_clk_active())
		return;
	t33_binsw_st.calls++;
	t33_core_binsw(&t33_binsw_ops, 1, 1, 1, req);
}
#endif

#ifndef TX_ISP_T33
/*
 * SET_TIZIANO_PAR of a streaming sensor on the T32 (openimp #534): vendor tx-isp-t32.ko
 * ispcore_core_ops_ioctl 0x100001e (tx_isp_t33_binsw.h t32_core_binsw): VICs 0x4444, IPC output position
 * wait, lib_tisp_refresh_para (here t32_pf_dn_refresh, tx_isp_t32_tisp.c: the block refreshes on the live
 * per-frame contexts), the core event 0x1000007 { 1 } (one VIC frame start, then 0x4440 back,
 * t33_vic_hold_tick).  lib_tisp_set_csccr is not wired on the T32 yet: its night write (0x14054 = 0) has
 * no day restore without the vendor's frame-done CSCCR path, so the UV clip stays as it is (counted).
 */
static struct {
	u32 calls, holds, ipc_waits, ipc_timeouts, refreshes, blocks, csccr_skipped, csccr[2];
} t32_binsw_st;

static void t32_binsw_vic_ctrl(u32 vic, u32 val)
{
	t32_vic_wr((int)vic, 0x1a8, val);
}

static u32 t32_binsw_ipc_pos(void)
{
	u32 v = isp_rd(0x30200);	/* lib_tisp_get_ipc_output_pos: system_reg_read(0x30200) */

	if (v)
		t32_binsw_st.ipc_waits++;
	return v;
}

static void t32_binsw_msleep(u32 ms)
{
	msleep(ms);
}

static void t32_binsw_print(u32 level, u32 line)
{
	t32_binsw_st.ipc_timeouts++;
	isp_printf(level, "[%s %u] IPC Read timeout !!!\n", "tx_isp_tisp_refresh_para", line);
}

static void t32_binsw_refresh(const u8 *req)
{
	const u8 *bin;
	u32 bytes;

	(void)req;	/* tisp_bin_data_parse: the bin t32_set_tiziano_par recorded */
	if (t32_core_iq_bin(&bin, &bytes))
		return;
	t32_binsw_st.refreshes++;
	t32_binsw_st.blocks = (u32)t32_pf_dn_refresh(bin, bytes, req[12]);
}

/*
 * lib_tisp_set_csccr (vendor tx-isp-t32.ko 0xb043c) for the day/night bin switch (openimp #853):
 * night (mode 1) writes the CSCCR UV clip 0x14054 = 0 and 0x14010 = 0xffffffff at once (grey picture);
 * day (mode 0) sets frame marks, and tisp_top_frame_count (0xafbc4..0xafbf4) then writes
 * 0x14054 = 0x007fff80 and 0x14010 = 0xffffffff one / two frames later.  The T32 per-frame top frame
 * count is not wired, so the day values are written here at once, inside the VIC hold of the switch
 * (same values, one to two frames earlier than the vendor).  Other modes: vendor -1, nothing written.
 */
static void t32_binsw_set_csccr(u32 ch, u32 mode)
{
	(void)ch;
	if (mode > 1) {
		t32_binsw_st.csccr_skipped++;
		return;
	}
	isp_wr(0x14054, mode ? 0 : 0x007fff80);
	isp_wr(0x14010, 0xffffffff);
	t32_binsw_st.csccr[mode]++;
}

static int t32_binsw_event(u32 sd, u32 cmd, const u32 *ev)
{
	unsigned long fl;

	(void)sd;
	(void)cmd;
	spin_lock_irqsave(&t33_flip_lock, fl);
	t33_vic_hold = (int)ev[0] > 0 ? (int)ev[0] : 1;	/* vic_core_ops_ioctl 0x1000007: max(ev[0], 1) frames */
	t32_flip_vic_rel = t32_core_sensor_data_type() == 2 ? 0x4410 : 0x4440;
	t33_flip_st.holds++;
	t32_binsw_st.holds++;
	spin_unlock_irqrestore(&t33_flip_lock, fl);
	return 0;
}

static const struct t33_binsw_ops t32_binsw_ops = {
	.vic_ctrl = t32_binsw_vic_ctrl,
	.ipc_pos = t32_binsw_ipc_pos,
	.msleep = t32_binsw_msleep,
	.print = t32_binsw_print,
	.refresh_para = t32_binsw_refresh,
	.set_csccr = t32_binsw_set_csccr,
	.event = t32_binsw_event,
};

/* tx_isp_t32_core.c t32_set_tiziano_par: req = the 20-byte request, vinum 0 while streaming */
void t32_core_bin_switch(const void *req)
{
	if (!isp_base || !t32_core_clk_active())
		return;
	t32_binsw_st.calls++;
	t32_core_binsw(&t32_binsw_ops, 1, 1, 1, req);
}
#endif

#ifndef TX_ISP_T33
extern u32 t32_msca_vendor_starts, t32_msca_direct_starts;	/* #730 (below) */

void t32_flip_show(struct seq_file *m)
{
	seq_printf(m, "t32 flip: requests %u applied %u mode %u ret %d pending %u vic holds %u releases %u left %d"
		   " deferred %u stop releases %u sensor frames %u\n",
		   t33_flip_st.requests, t33_flip_st.applied, t33_flip_st.mode, t33_flip_st.ret, t33_flip_pend,
		   t33_flip_st.holds, t33_flip_st.releases, t33_vic_hold, t33_flip_st.deferred,
		   t33_flip_st.stop_releases, t33_flip_st.sns_frames);
	{	/* #897: interrupt status since the last stream-on, VIC0 status bit counts */
		int w, b;

		seq_printf(m, "t32 irq or: isp %08x chdone %08x s2 %08x msca %08x s3 %08x s5 %08x s4 %08x"
			   " vic0 %08x/%08x vic1 %08x/%08x\n", t32_irq_or[0], t32_irq_or[1], t32_irq_or[2],
			   t32_irq_or[3], t32_irq_or[4], t32_irq_or[5], t32_irq_or[6], t32_vic_or[0], t32_vic_or[1],
			   t32_vic_or[2], t32_vic_or[3]);
		seq_printf(m, "t32 vic0 bits (error restarts %u):", t32_vic_err_restarts);
		for (w = 0; w < 2; w++)
			for (b = 0; b < 32; b++)
				if (t32_vic_bits[w][b])
					seq_printf(m, " %s%d=%u", w ? "b" : "a", b, t32_vic_bits[w][b]);
		seq_printf(m, "\n");
	}
	/* #652: frame-channel stream-offs, work-state wait timeouts, length of the last wait */
	seq_printf(m, "t32 msca: soff %u timeout %u last %u ms\n", t33_msca_soff_waits, t33_msca_soff_timeouts,
		   t33_msca_soff_last_ms);
	/* #534: SET_TIZIANO_PAR while streaming */
	seq_printf(m, "t32 binsw: calls %u holds %u ipc waits %u timeouts %u refresh %u blocks %u csccr skipped %u day %u night %u\n",
		   t32_binsw_st.calls, t32_binsw_st.holds, t32_binsw_st.ipc_waits, t32_binsw_st.ipc_timeouts,
		   t32_binsw_st.refreshes, t32_binsw_st.blocks, t32_binsw_st.csccr_skipped,
		   t32_binsw_st.csccr[0], t32_binsw_st.csccr[1]);
	{
		u32 live, inits, starts, stops;

		/* #730: the vendor channel start on the live MSCA state */
		t32_isposd_msca_status(&live, &inits, &starts, &stops);
		seq_printf(m, "t32 msca chx: live %u init %u start %u (direct %u) stop %u\n", live, inits,
			   t32_msca_vendor_starts, t32_msca_direct_starts, stops);
	}
}
#endif

#ifdef TX_ISP_T33

void t33_stats_show(struct seq_file *m)
{
	seq_printf(m, "t33 flip: requests %u applied %u mode %u ret %d pending %u vic holds %u releases %u left %d"
		   " deferred %u stop releases %u\n",
		   t33_flip_st.requests, t33_flip_st.applied, t33_flip_st.mode, t33_flip_st.ret, t33_flip_pend,
		   t33_flip_st.holds, t33_flip_st.releases, t33_vic_hold, t33_flip_st.deferred,
		   t33_flip_st.stop_releases);
	seq_printf(m, "t33 binsw: calls %u holds %u ipc waits %u timeouts %u\n", t33_binsw_st.calls,
		   t33_binsw_st.holds, t33_binsw_st.ipc_waits, t33_binsw_st.ipc_timeouts);
	seq_printf(m, "t33 statistics: awb %u (bad %u) grid %ux%u white %u/%u rg %u bg %u tag %u\n",
		   t33_st.awb_frames, t33_st.awb_bad, t33_awb_cols, t33_awb_rows, t33_st.awb.white,
		   t33_st.awb.zones, t33_st.awb.rg, t33_st.awb.bg, t33_st.awb.tag);
	seq_printf(m, "t33 statistics: ae %u (bad %u) grid %ux%u luma %u lo %u hi %u tag %u, hist %u (bad %u) mean %u/16\n",
		   t33_st.ae_frames, t33_st.ae_bad, t33_ae_cols, t33_ae_rows, t33_st.ae.luma, t33_st.ae.lo,
		   t33_st.ae.hi, t33_st.ae.tag, t33_st.hist_frames, t33_st.hist_bad, t33_st.mean16);
	t33_awbc_show(m);
	t33_aec_show(m);
	t33_calgo_show(m);
	t33_af_show(m);
	seq_printf(m, "t33 lce/lsf: irq8 %u irq30 %u curves %u list16 %u list37 %u ev %u\n", t33_lcepf_irq8s,
		   t33_lcepf_irq30s, t33_lcepf_curves, t33_lcepf_lce_runs, t33_lcepf_lsf_runs, t33_lcepf_evs);
	seq_printf(m, "t33 tmo: init %u irq %u drc %u bad %u regnum %u/%u\n", t33_g16_runs[0], t33_g16_runs[1],
		   t33_g16_runs[2], t33_g16_runs[3], t33_g16_tmo_regnum, t33_tmo_reg_num);
	seq_printf(m, "t33 tmo oneshot: disable %u enable %u tick %u\n", t33_tmo1s_runs[0], t33_tmo1s_runs[1],
		   t33_tmo1s_runs[2]);
#ifdef TX_ISP_T33
	seq_printf(m, "t33 msca: chx start %u direct %u list bad %u soff %u timeout %u last %u ms gated %u\n",
		   t33_msca_vendor_starts, t33_msca_direct_starts, t33_bst_bad, t33_msca_soff_waits,
		   t33_msca_soff_timeouts, t33_msca_soff_last_ms, t33_msca_soff_gated);
#endif
	seq_printf(m, "t33 blc: %s init %u gain %u switch %u fd %u refresh %u\n", blc_para[0] ? "on" : "off",
		   t33_blc_runs[0], t33_blc_runs[1], t33_blc_runs[2], t33_blc_runs[3], t33_blc_refresh);
	seq_printf(m, "t33 lsc: %s init %u switch %u fd %u refresh %u count %u mirror_flip %u mf %u\n",
		   lsc_para[0] ? "on" : "off", t33_lsc_runs[0], t33_lsc_runs[1], t33_lsc_runs[2], t33_lsc_refresh,
		   t33_lsc_count[0], t33_lsc_flips, t33_lsc_mf);
	seq_printf(m, "t33 ccm: %s init %u ct %u ev %u switch %u fd %u refresh %u api %u\n", ccm_para[0] ? "on" : "off",
		   t33_ccm_runs[0], t33_ccm_runs[1], t33_ccm_runs[2], t33_ccm_runs[3], t33_ccm_runs[4], tispCcmRefresh,
		   t33_g14d_ccm_api);
	seq_printf(m, "t33 gamma: %s init %u ev %u api %u switch %u fd %u refresh %u count %u mode %u\n",
		   gamma_para[0] ? "on" : "off", t33_gamma_runs[0], t33_gamma_runs[1], t33_gamma_runs[2],
		   t33_gamma_runs[3], t33_gamma_runs[4], tispGammaRefresh, gamma_count[0], t33_le32((u8 *)t33_gamma_ap32));
	seq_printf(m, "t33 bcsh: %s sec %s init %u fd %u/%u api %u ev %u irq %u\n", bcsh_para[0] ? "on" : "off",
		   bcsh_para[0] && t33_bcsh_st32[0] == (u32)(unsigned long)t33_bcsh_sec ? "ok" : "-", t33_g14c_runs[0],
		   t33_g14c_runs[1], t33_g14c_runs[2], t33_g14c_runs[3], t33_g14c_runs[4], t33_g14c_runs[5]);
	seq_printf(m, "t33 dns: init %u %u %u gain %u %u %u irq %u\n", t33_g14_runs[0], t33_g14_runs[1],
		   t33_g14_runs[2], t33_g14_runs[3], t33_g14_runs[4], t33_g14_runs[5], t33_g14_runs[6]);
	seq_printf(m, "t33 dmsc: init %u gain %u\n", t33_g14_runs[7], t33_g14_runs[8]);
	seq_printf(m, "t33 rdns: init %u gain %u ysp: init %u gain %u\n", t33_g14_runs[9], t33_g14_runs[10],
		   t33_g14_runs[11], t33_g14_runs[12]);
	seq_printf(m, "t33 dpc: %s init %u gain %u switch %u refresh %u\n", t33_dpc_v[T33_DPC_SEC_WORD] ? "on" : "off",
		   t33_g14_runs[13], t33_dpc_gains, t33_swg_runs[2], t33_bst_refresh[T33_BST_DPC]);
	seq_printf(m, "t33 defog: %s init %u bad %u alloc %u per-frame %s irqs %u pars %u evs %u full %u fd %u "
		   "irq_cnt %u ev %#x\n", t33_swg_defog_blk[3] ? "on" : "off", t33_g15_runs[0],
		   t33_g15_runs[1], t33_g15_runs[2], t33_dpf_active() ? "on" : "off", t33_dpf_runs[0],
		   t33_dpf_runs[1], t33_dpf_runs[2], t33_dpf_runs[3], t33_dpf_runs[4],
		   t33_swg_defog_blk[T33_DPF_I_IRQ], t33_swg_defog_blk[T33_DPF_I_EV]);
	seq_printf(m, "t33 mdns: %s init %u refresh %u/%u switch %u gain %#x zip %u bad %u\n",
		   t33_mdnsg_info[0] ? "on" : "off", t33_mdnsg_runs[0], t33_mdnsg_runs[2], t33_mdnsg_runs[1],
		   t33_swg_runs[8], t33_mdns_v[2181], t33_mdns_v[2190], t33_mdnsg_runs[3]);
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
	t33_mod_mark(1U << T33_MOD_AWBG);
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
		t33_msca_refresh = 1;
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

static DEFINE_SPINLOCK(t33_af_lock);	/* t33_af_sec: attribute copy-back vs the AF algorithm (#437) */

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
	{
		unsigned long flags;

		spin_lock_irqsave(&t33_af_lock, flags);
		memcpy(t33_af_sec, tmp, sizeof(tmp));
		spin_unlock_irqrestore(&t33_af_lock, flags);
	}
	t33_mod_mark(1U << T33_MOD_AF);
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

/*
 * AF per frame (#437 slice 2): system irq 6 -> event 15 -> tisp_af_calc_process -> tisp_af_alogrithm
 * (tx_isp_t33_afalgo.h) on the statistics bank the engine names in 0x1811c, in process context as the
 * vendor's event thread.  af_info's ring / mode words stay zero as in the vendor (nothing sets them).
 */
static u8 t33_af_info[T33_AFA_INFO_BYTES];
static u32 t33_af_fv_old, t33_af_runs;

static void t33_af_work(struct work_struct *w)
{
	unsigned long flags;
	const u8 *bank;

	if (!t33_af_ready())
		return;
	bank = (const u8 *)t33_af_pages + (isp_rd(0x1811c) & 3) * T33_AFA_BANK_BYTES;
#ifdef CONFIG_MIPS
	dma_cache_sync(NULL, (void *)bank, T33_AFA_BANK_BYTES, DMA_FROM_DEVICE);	/* engine-written bank */
#endif
	spin_lock_irqsave(&t33_af_lock, flags);
	t33_af_alogrithm(t33_af_info, t33_af_sec, bank, &t33_af_fv_old);
	spin_unlock_irqrestore(&t33_af_lock, flags);
	t33_af_runs++;
	t32_afg_stats.af_events++;	/* isp-m0 "af / gsm" line: algorithm runs */
}
static DECLARE_WORK(t33_af_w, t33_af_work);

static void t33_af_show(struct seq_file *m)
{
	seq_printf(m, "t33 af: runs %u fv %u metrics %u %u (section +0x44 / +0x40) ready %d\n", t33_af_runs,
		   t33_af_fv_old, t33_afa_rd32(t33_af_sec + 0x44), t33_afa_rd32(t33_af_sec + 0x40), t33_af_ready());
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
		t33_awb_stat_pages = t32_dma_zpages(get_order(T33_AWB_STAT_BYTES));
	buf = t33_mod_buf(T33_MOD_AWB);
	if (buf && t33_awb_stat_pages) {
		int n = t33_awb_init(bin + off, width, height, T33_AWB_INIT_TGAIN,
				     (u32)virt_to_phys((void *)t33_awb_stat_pages), buf, T33_SLOT_WORDS);

		/* tisp_awb_malloc_info: awbInfo / stAwbStatBuff and list 29 slots 11..14 */
		if (n > 0 && t33_awbc_malloc_info(buf, t33_awb_stat_pages))
			n = -ENOMEM;
		t33_mod_done(T33_MOD_AWB, n);
	}
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
static u32 t33_clm_ct_last = T33_CCM_INIT_CT;	/* tisp_clm_ct_interp state */
static int t33_clm_seg_last = -1;
static u8 t33_clm_seg_b;
static int t33_ct_last[2];	/* tisp_ct_update: the vendor's ct_last of channel 0 */
static DEFINE_MUTEX(t33_gr_lock);

/*
 * #659: lib_tisp_g_blc -> tisp_blc_api_get_blc (vendor 0x4cca0): -1 without blc_para[ch], else the 12 bytes at
 * blc_para[ch] +0x10 (the ap block: u16 flag, five u16 levels R / GR / GB / B / IR).
 */
int t33_hw_info_gib(u16 *lv)
{
	int r = -1;

	mutex_lock(&t33_gr_lock);
	if (blc_para[0] && blc_para[0]->ap) {
		memcpy(lv, blc_para[0]->ap, 12);
		r = 0;
	}
	mutex_unlock(&t33_gr_lock);
	return r;
}
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
	t33_ct_last[0] = t33_ct_last[1] = 0;
	t33_bst_reset();
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
	unsigned int i, tries;

	if (len <= 0 || !(t33_mod_built & (1ULL << id)) || !(buf = t33_mod_buf(id)))
		return;
	/*
	 * the core reads the page while the list runs: copy with the interrupts off and the list seen idle in that
	 * window (a frame-end start between the wait and the copy would read a half-copied page; openimp #579).
	 * The vendor writes list slots without any wait; this only removes the mixed-page frame.
	 */
	for (tries = 0;; tries++) {
		t32_wait_clear(T33_RCFG_TRIG, 1U << id, 2000);
		local_irq_save(flags);
		if (!(isp_rd(T33_RCFG_TRIG) & (1U << id)) || tries >= 3)
			break;
		local_irq_restore(flags);
	}
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

/*
 * skip: modules whose switch_para already ran (t33_refresh_para).
 * tisp_long_tgain_update (tx_isp_t33_tgB.h, oracle block tgB) skips every block whose TOP bypass bit is set
 * (tisp_top_get_bypass): DPC bit 0, BLC 1, DMSC 6, MDNS 12, YDNS 13, YSP 16, SDNS 17, CDNS 18, RDNS 20.  The
 * PRJ008 night TOP (0x00e4c098) sets MDNS and CDNS: the vendor keeps list 21 (0x12070) at night (#507).
 * fanout: called from the tgain update (t33_tgain_refresh); 0 from the bin switch.
 */
#define T33_GR_BYP(byp, bit, mod)	(((byp) >> (bit)) & 1 ? 1ULL << (mod) : 0)

static void t33_gr_refresh(u32 gain, u64 skip, int fanout)
{
	const u8 *bin;
	u32 bytes, byp;

	ACCESS_ONCE(t33_sw_gain) = gain;
	if (t32_core_iq_bin(&bin, &bytes) || !tisp_w)
		return;
	mutex_lock(&t33_gr_lock);
	/* the tgain fan-out only: the bin switch's block refresh (lib_tisp_refresh_para) is not gated */
	byp = fanout && (t33_mod_built & (1ULL << T33_MOD_TOP)) ? t33_top_get_bypass(0) : 0;
	skip |= T33_GR_BYP(byp, 0, T33_MOD_DPC) | T33_GR_BYP(byp, 1, T33_MOD_BLC) | T33_GR_BYP(byp, 6, T33_MOD_DMSC) |
		T33_GR_BYP(byp, 12, T33_MOD_MDNS) | T33_GR_BYP(byp, 13, T33_MOD_YDNS) | T33_GR_BYP(byp, 16, T33_MOD_YSP) |
		T33_GR_BYP(byp, 17, T33_MOD_SDNS) | T33_GR_BYP(byp, 18, T33_MOD_CDNS) | T33_GR_BYP(byp, 20, T33_MOD_RDNS);
	if (!t33_gr_tmp)
		t33_gr_tmp = kmalloc(T33_SLOT_WORDS * 4, GFP_KERNEL);
	if (!t33_gr_tmp)
		goto out;
	/* tisp_blc_tgain_update at every update (tx_isp_t33_bst.h: blc_para, list 4 slots, tispBlcRefresh) */
	if (!(skip & (1ULL << T33_MOD_BLC)))
		t33_blc_gain(gain);
	/*
	 * tisp_dpc_l_gain_update: every change, the proven port on dpcInfo and its section copy (group 13c:
	 * bin / dpc / ctr / cac para updates, the list 2 slots, tispDpcRefresh; the DPC frame done starts the list)
	 */
	if (!(skip & (1ULL << T33_MOD_DPC)) && (t33_mod_built & (1ULL << T33_MOD_DPC)) && t33_dpc_v[T33_DPC_SEC_WORD]) {
		unsigned long flags;

		t33_gr_gate(T33_MOD_DPC, gain);
		t33_bst_hold(T33_BST_DPC);
		local_irq_save(flags);
		t33_dpc_l_gain_update(0, gain);
		local_irq_restore(flags);
		t33_dpc_gains++;
		t33_gr_runs[T33_MOD_DPC]++;
	}
	/*
	 * The other info-struct blocks (tx_isp_t33_bst.h): the info struct at the new gain (the init builders fill
	 * the dmsc / ysp / rdns word arrays, the scratch list is not used), then the par_refresh tail.
	 */
	if (!(skip & (1ULL << T33_MOD_DMSC)))
		t33_g14_dns_refresh(T33_BST_DMSC, gain);	/* tisp_dmsc_refresh / par_refresh (group 14b-2) */
	if (!(skip & (1ULL << T33_MOD_YSP)))
		t33_g14_dns_refresh(T33_BST_YSP, gain);	/* tisp_ysp_refresh / par_refresh (group 14-1) */
	/* tisp_ydns_refresh / tisp_sdns_refresh: the vendor par_refresh ports and their own gain gate (group 14b) */
	if (!(skip & (1ULL << T33_MOD_YDNS)))
		t33_g14_dns_refresh(T33_BST_YDNS, gain);
	if (!(skip & (1ULL << T33_MOD_SDNS)))
		t33_g14_dns_refresh(T33_BST_SDNS, gain);
	if (!(skip & (1ULL << T33_MOD_RDNS)))
		t33_g14_dns_refresh(T33_BST_RDNS, gain);	/* tisp_rdns_refresh / par_refresh (group 14-1) */
	if (!(skip & (1ULL << T33_MOD_MDNS)))
		t33_mdnsg_gain(gain);	/* tisp_mdns_refresh -> tisp_mdns_par_refresh, own gain gate (group 16) */
	if (!(skip & (1ULL << T33_MOD_CDNS)))
		t33_g14_dns_refresh(T33_BST_CDNS, gain);	/* tisp_cdns_refresh (group 14b) */
out:
	mutex_unlock(&t33_gr_lock);
}

void t33_tgain_refresh(u32 gain)
{
	t33_gr_refresh(gain, 0, 1);
}

/* gamma glue on gamma_para (tx_isp_t33_gammav.h, group 13d-2) */

/* the switch's list writer: tisp_reg_write_t33 into the rebuilt list t33_gr_tmp (committed by the caller) */
static int t33_gamma_tmp_wr(u32 ch, u32 mod, u32 reg, u32 val)
{
	(void)ch;
	(void)mod;
	if (reg + 1 < T33_SLOT_WORDS)
		T33_SW(t33_gr_tmp, reg, val);
	return 0;
}

/* tisp_gamma_ev_update (t33_gr_lock held): the slots of list 11 directly, as the vendor, and tispGammaRefresh */
static void t33_gamma_ev_upd(u32 lo, u32 hi)
{
	unsigned long flags;

	if (!gamma_para[0])
		return;
	local_irq_save(flags);
	if (!t33_gv_ev_update(0, t33_gamma_v, t33_gamma_tab, lo, hi, gammaCfgMode, tisp_reg_write_t33,
			      tisp_rcfg_get_dma_state, &tispGammaRefresh))
		t33_gamma_runs[1]++;
	local_irq_restore(flags);
}

/* tisp_bcsh_ev_update (t33_gr_lock held): the slots of list 18 directly, as the vendor, and tispBcshRefresh */
static void t33_bcsh_ev_upd(u32 lo, u32 hi, u32 bv, u32 bvh)
{
	unsigned long flags;
	u32 kept[4];

	local_irq_save(flags);
	memcpy(kept, t33_bcsh_st + 8, sizeof(kept));
	t33_bcshk_ev_update(0, 0, lo, hi, bv, bvh);
	if (memcmp(kept, t33_bcsh_st + 8, sizeof(kept)))
		t33_g14c_runs[4]++;
	local_irq_restore(flags);
}

/*
 * tisp_set_brightness / _contrast / _saturation / _hue (tx_isp_core_ops_s_ctrl, group 14c): the byte by pointer
 * to tisp_bcsh_api_set_* (blocks bcshK, bcshH); which 0 brightness, 1 contrast, 2 saturation, 3 hue.  Before
 * the ISP init (no bcsh_para: the vendor oopses) nothing happens.
 */
void t33_tisp_set_bcsh(u32 ch, u32 which, u32 v)
{
	unsigned long flags;
	u8 b = (u8)v;

	if (ch || !bcsh_para[0])
		return;
	mutex_lock(&t33_gr_lock);
	local_irq_save(flags);
	if (which == 0)
		t33_bcsh_set_brightness(0, &b);
	else if (which == 1)
		t33_bcsh_set_contrast(0, &b);
	else if (which == 2)
		t33_bcsh_set_saturation(0, &b);
	else
		t33_bcsh_set_hue(0, &b);
	t33_g14c_runs[3]++;
	local_irq_restore(flags);
	mutex_unlock(&t33_gr_lock);
}

/*
 * tisp_set_sharpness (tx_isp_core_ops_s_ctrl, openimp #707): DMSC sharpness byte + refresh, the YSP strength
 * tables from the bin copy, YSP refresh and register stores (tx_isp_t33_sharp.h).  Needs the YSP / DMSC
 * inits of the running bin (section pointers set).
 */
void t33_tisp_set_sharpness(u32 ch, u32 v)
{
	unsigned long flags;

	if (ch || !t33_ysp_v[2172 / 4] || !t33_g14_yspp[0] || !t33_dmsc_v[670])
		return;
	mutex_lock(&t33_gr_lock);
	local_irq_save(flags);
	t33_set_sharpness(0, v);
	local_irq_restore(flags);
	mutex_unlock(&t33_gr_lock);
}

/* tisp_gamma_irq_frame_done (system irq 46, interrupt context) */
static void t33_gamma_fd(void)
{
	if (!gamma_para[0])
		return;
	if (tispGammaRefresh)
		t33_gamma_runs[4]++;
	t33_gv_frame_done(0, t33_gamma_v, gamma_count, &tispGammaRefresh, gammaCfgMode, tisp_reg_write_t33,
			  tisp_rcfg_get_dma_state, tisp_rcfg_module_cfg_trig);
}

/*
 * g14d-1, CCM attribute (IMP_ISP_Tuning_SetCCMAttr / GetCCMAttr, s_ctrl 0x80 / g_ctrl): tisp_s_ccm_attr (port-loop
 * block r11E) -> tisp_ccm_api_set (block ccmP: the 40 bytes into ccm_para + 612, forced CT / EV interpolation,
 * no_interp, calc, the list 10 slots, tispCcmRefresh = 1: irq 45 starts the list); -1 as the vendor unless exactly
 * one of the top bypass bits 0x4000 (CCM) / 0x80 (BCSH CCM) is set (PRJ008 day 0x00e4c098: both set, the set runs
 * and the caller prints "Both BCSH and CCM ..." like the vendor).  tisp_g_ccm_attr -> tisp_ccm_api_get (vendor
 * decompile wire14c/ccm.c): the same bypass gate, ccm_para + 612 (40 bytes; -1 without ccm_para).  Before: both
 * only stored / returned a copy in the tuning-control state.
 */
static int t33_g14d_ccm_api_set(u32 ch, const u8 *buf)
{
	if ((ch & 0xff) || !ccm_para[0])
		return -1;
	t33_ccm_api_set_glue(0, buf);
	t33_g14d_ccm_api++;
	return 0;
}

static int t33_g14d_sysw(u32 reg, u32 val)
{
	system_reg_write(reg, val);
	return 0;
}

#define tisp_top_get_bypass	t33_top_get_bypass
#define tisp_ccm_api_set	t33_g14d_ccm_api_set
#define isp_printf	t33_g14c_printf
#define system_reg_write	t33_g14d_sysw
#include "tx_isp_t33_r11E.h"
#undef tisp_top_get_bypass
#undef tisp_ccm_api_set
#undef isp_printf
#undef system_reg_write

int t33_tisp_s_ccm_attr(u32 ch, const void *buf)
{
	unsigned long flags;
	int r;

	if (ch || !ccm_para[0])
		return -1;
	mutex_lock(&t33_gr_lock);
	local_irq_save(flags);
	r = t33_s_ccm_attr(0, buf);
	local_irq_restore(flags);
	mutex_unlock(&t33_gr_lock);
	return r;
}

/* tisp_g_ccm_attr: tisp_ccm_api_get behind the same bypass gate as the set */
int t33_tisp_g_ccm_attr(u32 ch, void *buf)
{
	u32 v;

	if (ch || !ccm_para[0])
		return -1;
	v = t33_top_get_bypass(0);
	if (!(v & 0x4000) && (v & 0x80))
		return 0;
	memcpy(buf, (const u8 *)(unsigned long)ccm_para[0] + 612, 40);
	return ((v & 0x4000) && !(v & 0x80)) ? 0 : -1;
}

/*
 * g14e-1, CSC attribute (IMP_ISP_Tuning_SetISPCSCAttr / GetISPCSCAttr, s_ctrl / g_ctrl 0x96): tisp_set_csc_attr
 * (block clmP) -> tisp_csc_api_set (block cscQ: section byte 99 = 2, the 92 bytes into the API block, refresh_by_mode
 * 1 on it, sync_api_para 0, update_para, tisp_t33_csc_write_reg: list 13 slots + tispCscRefresh, irq 48 starts the
 * list), tisp_bcsh_refresh_by_csc (tisp_csc_get into bcsh_para + 0x1f8, forced EV / CT interpolation,
 * calc_para_refresh 0: the BCSH state only, no list word, as the vendor) and tisp_clm_refresh_by_csc (tisp_csc_get,
 * calc_para_refresh, tisp_clm_t33_write_reg: our CLM builder t33_clm_init at the block's last CT on the current bin,
 * after the interrupts are on again: t33_gr_commit waits for the list).  tisp_get_csc_attr -> tisp_csc_api_get: the
 * API block.  Before: both only stored / returned a copy in the tuning-control state.  -1 before the ISP init (no
 * csc_para: the vendor oopses).
 */
static u32 t33_g14e_csc_api, t33_g14e_clm_pend;
static u8 t33_g14e_clm_scr[4 + T33_CSCQ_BYTES];	/* clm_info[0] + 4: the csc set of tisp_clm_refresh_by_csc */
static u8 *t33_g14e_clm_info[4] = { t33_g14e_clm_scr };

static int t33_g14e_csc_api_set(u32 ch, u32 a1)
{
	t33_csc_api_set(csc_para, ch, (const u8 *)(unsigned long)a1, t33_csc_rbm, t33_csc_sync, t33_csc_upd, t33_csc_wr);
	t33_g14e_csc_api++;
	return 0;
}

static int t33_g14e_csc_api_get(u32 ch, u32 a1)
{
	return t33_csc_api_get(csc_para, ch, (u8 *)(unsigned long)a1);
}

static int t33_g14e_clm_calc(u32 ch)
{
	(void)ch;	/* part of t33_clm_init */
	return 0;
}

static int t33_g14e_clm_write(u32 ch)
{
	(void)ch;
	t33_g14e_clm_pend = 1;	/* t33_g14e_clm_build after local_irq_restore */
	return 0;
}

static const struct t33_clmp_ops t33_g14e_clmp = {
	tisp_csc_get, t33_g14e_clm_calc, t33_g14e_clm_write, tisp_bcsh_interp_by_ev, tisp_bcsh_interp_by_ct,
	tisp_bcsh_calc_para_refresh, t33_g14e_csc_api_get, t33_g14e_csc_api_set,
};

static void t33_g14e_clm_build(const u8 *bin, u32 bytes)
{
	u32 off, len;

	off = t32_iq_section(bin, bytes, T33_SEC_CLM, &len);
	if (!off || len != T33_CLM_SEC_BYTES || !t33_gr_tmp)
		return;
	memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
	t33_gr_commit(T33_MOD_CLM, t33_clm_init(bin + off, bin + off + len, t32_iq_payload(bin + off, bytes - off, T33_SEC_CLM),
						tisp_csc_get, (int)t33_clm_ct_last, t33_gr_tmp, T33_SLOT_WORDS));
}

int t33_tisp_set_csc_attr(u32 ch, void *buf)
{
	unsigned long flags;
	const u8 *bin;
	u32 bytes, off, len;

	if (ch || !csc_para[0] || !bcsh_para[0] || t32_core_iq_bin(&bin, &bytes))
		return -1;
	mutex_lock(&t33_gr_lock);
	if (!t33_gr_tmp)
		t33_gr_tmp = kmalloc(T33_SLOT_WORDS * 4, GFP_KERNEL);
	off = t32_iq_section(bin, bytes, T33_SEC_BCSH, &len);
	local_irq_save(flags);
	t33_g14c_lut = off && len == T33_BCSH_SEC_BYTES ? bin + off + len : NULL;	/* the forced interp_by_ct */
	t33_g14e_clm_pend = 0;
	t33_set_csc_attr(&t33_g14e_clmp, (u8 *const *)bcsh_para, t33_g14e_clm_info, 0, (u32)(unsigned long)buf);
	t33_g14c_lut = NULL;
	local_irq_restore(flags);
	if (t33_g14e_clm_pend)
		t33_g14e_clm_build(bin, bytes);
	mutex_unlock(&t33_gr_lock);
	return 0;
}

int t33_tisp_get_csc_attr(u32 ch, void *buf)
{
	unsigned long flags;
	int r;

	if (ch || !csc_para[0])
		return -1;
	local_irq_save(flags);
	r = t33_get_csc_attr(&t33_g14e_clmp, 0, (u32)(unsigned long)buf);
	local_irq_restore(flags);
	return r;
}

/*
 * tisp_s_Gamma -> tisp_gamma_api_set (IMP_ISP_Tuning_SetGamma, #608): mode / curve into ap, the curve into list 11
 * and tispGammaRefresh = 1 (the next irq 46 starts the list).  Returns 0 like the vendor tisp_s_Gamma (an unknown
 * mode is stored in ap word 0 and nothing else changes); -1 before the ISP init (no gamma_para: the vendor oopses).
 */
int t33_tisp_s_gamma(u32 ch, void *buf)
{
	unsigned long flags;

	if (ch || !gamma_para[0])
		return -1;
	mutex_lock(&t33_gr_lock);
	local_irq_save(flags);
	if (!t33_gv_api_set(0, t33_gamma_v, t33_gamma_tab, buf, gammaCfgMode, tisp_reg_write_t33, &tispGammaRefresh))
		t33_gamma_runs[2]++;
	local_irq_restore(flags);
	mutex_unlock(&t33_gr_lock);
	return 0;
}

/* tisp_g_Gamma -> tisp_gamma_api_get: the API block ap (264 B) */
int t33_tisp_g_gamma(u32 ch, void *buf)
{
	unsigned long flags;

	if (ch || !gamma_para[0])
		return -1;
	local_irq_save(flags);
	t33_gv_api_get(t33_gamma_v, buf);
	local_irq_restore(flags);
	return 0;
}

/*
 * tisp_long_ev_update's block part (AE event 6, on an EV change): BCSH
 * (tisp_bcsh_ev_update) and CCM (tisp_ccm_ev_update: the CT matrix of the
 * last CT update reaches the list words here, list 10 reruns every time),
 * LCE (tisp_lce_ev_update, top bypass bit 11 clear: lists 16 / 37 words
 * from the EV / BV interpolations, picked up by the next irq 8 start;
 * openimp #500), gamma (tisp_gamma_ev_update, bit 8: list 11 on a new
 * strength; openimp #507).  RDNS runs the proven tisp_rdns_ev_update port
 * (tx_isp_t33_swglue.h: slots + refresh 1, or refresh 2 with the list busy).
 * The vendor's defog / TMO EV updates leave their words unchanged on PRJ008
 * (flat EV tables, QEMU trace) and set their refresh flag every time
 * (tisp_defog_ev_update, tisp_tmo_ev_update): lists 12 and 8 rerun.
 * bv / bvh: the AE event's brightness words (the vendor's x / y arguments).
 */
void t33_ev_refresh(u32 lo, u32 hi, u32 bv, u32 bvh)
{
	u32 byp;

	mutex_lock(&t33_gr_lock);
	t33_sw_ev_lo = lo;
	t33_sw_ev_hi = hi;
	t33_sw_bv = bv;
	t33_gr_ev = hi << 22 | lo >> 10;
	if (!t33_gr_tmp)
		t33_gr_tmp = kmalloc(T33_SLOT_WORDS * 4, GFP_KERNEL);
	/*
	 * tisp_long_ev_update runs each block's EV update only when its top bypass bit (tisp_top_get_bypass,
	 * the top list's 0x40 word) is clear: BCSH 0x4000, CCM 0x80, RDNS bit 20, defog 0x200, LCE 0x800,
	 * TMO 0x20 (PRJ008 0x00e4c098: BCSH and CCM skipped; openimp #507).  Read once from the TOP list like
	 * the vendor (tx_isp_t33_top.h, block top).
	 */
	byp = t33_top_get_bypass(0);
	/* tisp_bcsh_ev_update (block bcshK) on bcsh_para: the list 18 slots and tispBcshRefresh, irq 52 starts it */
	if (!(byp & 0x4000) && bcsh_para[0])
		t33_bcsh_ev_upd(lo, hi, bv, bvh);
	/* tisp_ccm_ev_update (block ccmP) on ccm_para: the slots and tispCcmRefresh, irq 45 starts list 10 */
	if (!(byp & 0x80))
		t33_ccm_ev_upd(lo, hi);
	/*
	 * tisp_gamma_ev_update (top bypass bit 8 clear) on gamma_para: interp_by_ev / update_curve every time; on a
	 * new strength the list 11 slots and tispGammaRefresh, irq 46 starts the list (blocks gammaev, gammav)
	 */
	if (!(byp & 0x100))
		t33_gamma_ev_upd(lo, hi);
	if (!(byp & 0x800))
		t33_lcepf_ev(lo, hi, bv);
	if (!(byp & 0x100000))
		t33_swg_rdns_ev(t33_gr_ev);	/* tisp_rdns_ev_update (tx_isp_t33_swglue.h) */
	/*
	 * tisp_defog_ev_update (#878): EV words and tispDefogRefresh, irq 47 starts list 12; without the
	 * per-frame path (t33_defog_pf = 0) the list reruns at the frame end as before
	 */
	if (!(byp & 0x200) && !t33_dpf_ev(lo, hi, bv, bvh))
		t33_gr_rerun(T33_MOD_DEFOG);
	if (!(byp & 0x20))
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
/* tisp_ccm_ct_update on ccm_para (tx_isp_t33_ccmv.h): interp_by_ct not forced, write_reg, tispCcmRefresh */
static void t33_ctup_ccm(const u8 *bin, u32 bytes, u32 ct)
{
	(void)bin; (void)bytes;	/* the section and knots of the last bin parse (tparamsP / stLutInfo) */
	t33_ccm_ct_update(ct);
}

/*
 * tisp_bcsh_ct_update on bcsh_para (group 14c, the vendor's order): interp_by_ct not forced at ct, the frame
 * counter st + 0x29, on a new matrix calc_para_refresh(2), params_transform and the list 18 slots (list idle,
 * tispBcshRefresh 1) or tispBcshRefresh 2; irq 52 starts the list, one run per frame with the EV update (the
 * driver-form t33_bcsh_ct_update of block bcshct is the same computation; before: a list copy + t33_gr_commit,
 * a second list run in frames with an EV update).
 */
static void t33_ctup_bcsh(const u8 *bin, u32 bytes, u32 ct)
{
	unsigned long flags;
	u32 off, len;
	u8 *st;
	int r;

	off = t32_iq_section(bin, bytes, T33_SEC_BCSH, &len);
	if (!off || len != T33_BCSH_SEC_BYTES || !bcsh_para[0])
		return;
	local_irq_save(flags);
	st = bcsh_para[0];
	t33_g14c_lut = bin + off + len;
	r = tisp_bcsh_interp_by_ct(0, 0, ct, 0, 0);
	st[0x29] = st[0x29] == 0xff ? 0 : st[0x29] + 1;
	if (!r) {
		tisp_bcsh_calc_para_refresh(0, 2);
		tisp_params_transform(0);
		if (tisp_rcfg_get_dma_state(T33_MOD_BCSH)) {
			tispBcshRefresh = 2;
		} else {
			t33_bcshk_write_all(0);
			tispBcshRefresh = 1;
		}
	}
	t33_g14c_lut = NULL;
	local_irq_restore(flags);
}

/* tisp_clm_ct_update (block clmct): tisp_clm_ct_interp not forced (hysteresis 1), then write_reg */
static void t33_ctup_clm(const u8 *bin, u32 bytes, u32 ct)
{
	u32 off, len;

	off = t32_iq_section(bin, bytes, T33_SEC_CLM, &len);
	if (off && len == T33_CLM_SEC_BYTES) {
		const u8 *sec = bin + off, *lut = bin + off + len;
		u32 lb = t32_iq_payload(sec, bytes - off, T33_SEC_CLM);

		if (t33_clm_seg_last < 0)
			t33_clm_seg_last = t33_clm_seg(sec, lut, (int)t33_clm_ct_last);
		t33_clm_seg_b = (u8)t33_clm_seg_last;
		if (!t33_clm_ct_gate(sec, lut, (int)ct, &t33_clm_ct_last, &t33_clm_seg_b, 1)) {
			t33_clm_seg_last = t33_clm_seg_b;
			memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4);
			t33_gr_commit(T33_MOD_CLM, t33_clm_init(sec, lut, lb, tisp_csc_get, (int)ct, t33_gr_tmp,
								T33_SLOT_WORDS));
		}
	}
}

static const u8 *t33_ctup_bin;
static u32 t33_ctup_bytes;

#define T33_CTUP_HOOKS
#define T33_CTUP_BYPASS(ch)		t33_top_get_bypass(ch)
#define T33_CTUP_CCM(ch, x, ct, c2)	t33_ctup_ccm(t33_ctup_bin, t33_ctup_bytes, (u32)(ct))
#define T33_CTUP_LSC(ch, x, ct, c2)	do { } while (0)	/* LSC CT update not ported (words unchanged on PRJ008) */
#define T33_CTUP_BCSH(ch, x, ct, c2)	t33_ctup_bcsh(t33_ctup_bin, t33_ctup_bytes, (u32)(ct))
#define T33_CTUP_CLM(ch, x, ct, c2)	t33_ctup_clm(t33_ctup_bin, t33_ctup_bytes, (u32)(ct))
#include "tx_isp_t33_ctup.h"

/*
 * tisp_ct_update (AWB event 11, block ctup): each CT update runs only when
 * its top bypass bit is clear (CCM 0x80, LSC 8, BCSH 0x4000, CLM 0x8000;
 * PRJ008 day and night 0x00e4c098 skip all four, openimp #507).
 */
void t33_ct_refresh(u32 ct)
{
	const u8 *bin;
	u32 bytes;

	ACCESS_ONCE(t33_sw_ct) = ct;
	if (t32_core_iq_bin(&bin, &bytes))
		return;
	mutex_lock(&t33_gr_lock);
	if (!t33_gr_tmp)
		t33_gr_tmp = kmalloc(T33_SLOT_WORDS * 4, GFP_KERNEL);
	if (t33_gr_tmp) {
		t33_ctup_bin = bin;
		t33_ctup_bytes = bytes;
		t33_ct_update(0, 0, (int)ct, 0, t33_ct_last);
	}
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
		t33_lsf_pages = t32_dma_zpages(T33_LSF_ORDER);
	memcpy(t33_lsf_sec, sec, sizeof(t33_lsf_sec));
	/* tisp_lce_malloc_cfg: the histogram buffer always (irq 8 stores it into slot 5) */
	if (!t33_lsf_hist_pages) {
		t33_lsf_hist_pages = t32_dma_zpages(T33_LSF_HIST_ORDER);
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
	if (t33_rc_ok(id)) {
		t33_rcfg_module_cfg_trig(&t33_rcc, 0, id);	/* tisp_rcfg_module_cfg_trig */
		t33_rcfg_runs++;
		return;
	}
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

	t33_bin_append(bin, bytes);
	t33_gref_reset();
	t33_tisp_init_ae(bin, bytes);
	t33_tisp_init_awb(bin, bytes);
	off = t32_iq_section(bin, bytes, T33_SEC_BLC, &len);
	if (off && len == T33_BLC_SEC_BYTES && !t32_core_sensor_mbus(&code) && t32_mbus_to_bayer(code) >= 0)
	{
		t33_mod_set_tmpl(T33_MOD_BLC, T33_TMPL(t33_blc_tmpl), tisp_w,
				 t33_blc_init(bin + off, (u32)t32_mbus_to_bayer(code), T33_INIT_GAIN, tisp_w));
		if (t33_mod_built & (1ULL << T33_MOD_BLC))
			t33_blc_attach(bin + off, (u32)t32_mbus_to_bayer(code), T33_INIT_GAIN);	/* tisp_blc_init */
	}
	/* tisp_ydns_init / tisp_sdns_init (tx_isp_t33_g14glue.h, group 14a) */
	off = t32_iq_section(bin, bytes, T33_SEC_YDNS, &len);
	if (off && len == T33_YDNS_SEC_BYTES)
		t33_g14_dns_init(T33_BST_YDNS, bin + off);
	off = t32_iq_section(bin, bytes, T33_SEC_SDNS, &len);
	if (off && len == T33_SDNS_SEC_BYTES)
		t33_g14_dns_init(T33_BST_SDNS, bin + off);
	off = t32_iq_section(bin, bytes, T33_SEC_DPC, &len);	/* tisp_dpc_init (tx_isp_t33_g14glue.h, group 14-2a) */
	if (off && len == T33_DPC_SEC_BYTES)
		t33_g14_dpc_init(bin + off);
	off = t32_iq_section(bin, bytes, T33_SEC_DMSC, &len);	/* tisp_dmsc_init (tx_isp_t33_g14glue.h, group 14b-2) */
	if (off && len == T33_DMSC_SEC_BYTES)
		t33_g14_dmsc_init(bin + off);
	off = t32_iq_section(bin, bytes, T33_SEC_YSP, &len);	/* tisp_ysp_init (tx_isp_t33_g14glue.h, group 14-1) */
	if (off && len == T33_YSP_SEC_BYTES)
		t33_g14_ysp_init(bin + off);
	/* tisp_rdns_init (group 14-1): func_rdns_para_update reads the BLC / DG list word 4 (tisp_reg_read_t33) */
	off = t32_iq_section(bin, bytes, T33_SEC_RDNS, &len);
	if (off && len == T33_RDNS_SEC_BYTES)
		t33_g14_rdns_init(bin + off);
	{
		u32 *buf;

		/* CCM: section 12 and its knots behind it, CT 5000 K, EV 0 (the vendor's init values) */
		off = t32_iq_section(bin, bytes, T33_SEC_CCM, &len);
		if (off && len == T33_CCM_SEC_BYTES && (buf = t33_mod_buf(T33_MOD_CCM)))
			t33_mod_done(T33_MOD_CCM, t33_ccm_attach(bin + off, bin + off + len,
								 t32_iq_payload(bin + off, bytes - off, T33_SEC_CCM),
								 T33_CCM_INIT_CT, T33_CCM_INIT_EV, buf, T33_SLOT_WORDS));
	}
	off = t32_iq_section(bin, bytes, T33_SEC_GAMMA, &len);
	if (off && len == T33_GAMMA_SEC_BYTES) {
		u32 *buf = t33_mod_buf(T33_MOD_GAMMA);

		if (buf)
			t33_mod_done(T33_MOD_GAMMA,
				     t33_gamma_init(bin + off, bin + off + len,
						    t32_iq_payload(bin + off, bytes - off, T33_SEC_GAMMA),
						    T33_CCM_INIT_EV, 0, buf, T33_SLOT_WORDS));
		t33_gamma_attach(bin + off, bin + off + len, t32_iq_payload(bin + off, bytes - off, T33_SEC_GAMMA));
	}
	off = t32_iq_section(bin, bytes, T33_SEC_DEFOG, &len);
	if (off && len == T33_DEFOG_SEC_BYTES && !t32_core_sensor_size(&width, &height)) {
		if (!t33_defog_pages)
			t33_defog_pages = t32_dma_zpages(T33_DEFOG_ORDER);
		/* tisp_defog_init (group 15): on a copy, weights for another size / zoom are recomputed into it */
		memcpy(t33_defog_sec, bin + off, sizeof(t33_defog_sec));
		if (t33_defog_pages)
			t33_g15_defog_init(width, height);
	}
	off = t32_iq_section(bin, bytes, T33_SEC_AF, &len);
	if (off && len == T33_AF_SEC_BYTES && !t32_core_sensor_size(&width, &height)) {
		u32 *buf = t33_mod_buf(T33_MOD_AF);

		if (!t33_af_pages)
			t33_af_pages = t32_dma_zpages(T33_AF_ORDER);
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
			t33_lce_curve_pages = t32_dma_zpages(T33_LCE_CURVE_ORDER);
		if (!t33_lce_stat_pages)
			t33_lce_stat_pages = t32_dma_zpages(T33_LCE_STAT_ORDER);
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
	tisp_rcfg_module_cfg_trig(0, 22);	/* lib_tisp_init: trig(0, 22) after tisp_core_tuning_init (0x26014, #755) */
	t33_tisp_init_mdns(bin, bytes);
	off = t32_iq_section(bin, bytes, T33_SEC_LSC, &len);
	if (off && len == T33_LSC_SEC_BYTES && !t32_core_sensor_size(&width, &height)) {
		u32 *buf = t33_mod_buf(T33_MOD_LSC), tb;
		const u8 *tab, *sec = t33_lsc_src(bin, bytes, off, len, &tab, &tb);

		if (!t33_lsc_pages)
			t33_lsc_pages = t32_dma_zpages(T33_LSC_ORDER);
		/* CT 5000 K as the CCM; the table written uncached */
		if (buf && t33_lsc_pages)
			t33_mod_done(T33_MOD_LSC,
				     t33_lsc_init(sec, tab, tb, height, width,
						  T33_INIT_GAIN, T33_CCM_INIT_CT, t33_lsc_v, &t33_lsc_m,
						  (u8 *)CKSEG1ADDR(virt_to_phys((void *)t33_lsc_pages)),
						  (u32)virt_to_phys((void *)t33_lsc_pages), buf, T33_SLOT_WORDS));
		if (t33_mod_built & (1ULL << T33_MOD_LSC))	/* tisp_lsc_init on lsc_para (tx_isp_t33_bst.h) */
			t33_lsc_attach(sec, tab, tb, height, width, T33_INIT_GAIN, T33_CCM_INIT_CT);
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
 * lce, rdns, af, csccr; dpc, dmsc, ysp, mdns, ydns, sdns, cdns and rdns
 * run the vendor switch_para ports (tx_isp_t33_swglue.h) at their own
 * last gain / EV; the AE runs tisp_ae_switch_para (new AE section, list
 * 30 from the AE hardware parameter block, normal_ae_dn_par_refresh: the
 * deflicker table and the parameter update at the next AE frame).
 * Not rebuilt (open, #507): the AWB list 29 / AWB algorithm parameters
 * (tisp_awb_dn_params_refresh), the night byte 0x1f of tparamsP and the TOP
 * direct writes.
 */
#define T33_SW_SEC(BLK)	((off = t32_iq_section(bin, bytes, T33_SEC_##BLK, &len)) && len == T33_##BLK##_SEC_BYTES)
#define T33_SW_TMP()	memset(t33_gr_tmp, 0, T33_SLOT_WORDS * 4)

/*
 * tisp_af / clm / top_switch_para (oracle block swsm2, tx_isp_t33_swsm2.h) on the driver's state: af_info[0] /
 * clm_info[0] point at blocks that carry the vendor words the entry points touch (af byte 0x4e8: section; clm word 0:
 * section, word 0x18: last CT), tparamsP is set per call such that tparamsP + vendor offset = our section.  The
 * vendor-named callees are the driver's builders: tisp_af_par_init = the section copy, all_reg_write = t33_af_init
 * into list 31; tisp_clm_ct_interp = t33_clm_seg at the CT, calc_para_refresh + t33_write_reg = t33_clm_init
 * into list 19; tisp_top_reg_write = the refresh state above (list 33 built by t33_sw_top).
 */
#define T33_TP_OFF_AF	0x9fecU
#define T33_TP_OFF_CLM	0x903cU
static u32 t33_swg_tp32[4], t33_swg_toppar[4], t33_swg_af_blk[0x4ec / 4], t33_swg_clm_blk[25];
static u32 t33_swg_af_info[4] __maybe_unused, t33_swg_clm_info[4] __maybe_unused;
static struct {
	const u8 *bin;
	u32 bytes, len, w, h;
} t33_swg_sm2;

static void tisp_af_par_init(u32 ch);
static void tisp_af_t33_all_reg_write(u32 ch, u32 *p);
static void tisp_clm_ct_interp(u32 ch, u32 ct, u32 forced);
static void tisp_clm_calc_para_refresh(u32 ch);
static void tisp_clm_t33_write_reg(u32 ch);
#include "tx_isp_t33_swsm2.h"

static void tisp_af_par_init(u32 ch)
{
	(void)ch;
	memcpy(t33_af_sec, (const u8 *)(unsigned long)t33_swg_af_blk[0x4e8 / 4], sizeof(t33_af_sec));
}

static void tisp_af_t33_all_reg_write(u32 ch, u32 *p)
{
	(void)ch;
	(void)p;
	T33_SW_TMP();
	t33_gr_commit(T33_MOD_AF, t33_af_init(t33_af_sec, t33_swg_sm2.w, t33_swg_sm2.h,
					      (u32)virt_to_phys((void *)t33_af_pages), t33_gr_tmp, T33_SLOT_WORDS));
}

static void tisp_clm_ct_interp(u32 ch, u32 ct, u32 forced)
{
	const u8 *sec = (const u8 *)(unsigned long)t33_swg_clm_blk[0];

	(void)ch;
	(void)forced;
	t33_clm_seg_last = t33_clm_seg(sec, sec + t33_swg_sm2.len, ct);
}

static void tisp_clm_calc_para_refresh(u32 ch)
{
	(void)ch;	/* part of t33_clm_init (tisp_clm_t33_write_reg) */
}

static void tisp_clm_t33_write_reg(u32 ch)
{
	const u8 *sec = (const u8 *)(unsigned long)t33_swg_clm_blk[0], *bin = t33_swg_sm2.bin;

	(void)ch;
	T33_SW_TMP();
	t33_gr_commit(T33_MOD_CLM, t33_clm_init(sec, sec + t33_swg_sm2.len,
						t32_iq_payload(sec, t33_swg_sm2.bytes - (u32)(sec - bin), T33_SEC_CLM),
						tisp_csc_get, t33_swg_clm_blk[24], t33_gr_tmp, T33_SLOT_WORDS));
}

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
	t33_top_init(bin + off, width, height, (u32)bayer, t32_ivdc_direct(0), tisp_w, dw);
	t33_tisp_top_save(bin + off);
	if (t32_ldc_top_hldc())
		tisp_w[0].val &= ~0x800000U;
	T33_SW_TMP();
	n = t33_rcfg_build(&l, t33_gr_tmp, T33_SLOT_WORDS, tisp_w, T33_TOP_LIST_WRITES);
	if (n <= 0)
		return;
	t32_wait_clear(T33_RCFG_TRIG_HI, 1U << (T33_MOD_TOP - 32), 2000);
	local_irq_save(flags);
	memcpy(buf, t33_gr_tmp, T33_SLOT_WORDS * 4);
	t33_swg_tp32[0] = (u32)(unsigned long)(bin + off);	/* TOP is section 0: stTopPar = tparamsP */
	t33_top_switch_para(0, t33_swg_toppar, t33_swg_tp32);	/* tispTopRefresh */
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
/*
 * tisp_ccm / csccr / defog / lsc_switch_para (oracle block swmid, tx_isp_t33_swmid.h) on the driver's state.
 * tparamsP: the vendor's own parameter copy, so its entry points write into it (CCM byte 0x75b, CSCCR byte 14
 * and the mode ranges, defog bytes 4 / 0xbe / 0xc0 and the weights, LSC cell size 0x58 / 0x59); here tparamsP is
 * set per call such that tparamsP + vendor offset = a private copy of the section, never the IQ bin libimp
 * handed in (its buffer is read again at the next switch and at stream-on).
 *
 * - ccm_para[0]: the vendor-layout CCM block of tx_isp_t33_ccmv.h (group 13d-1): the new section copied into
 *   its tparamsP slot, interp_by_ct (forced) at the block's own CT, interp_by_ev (forced) at its own EV words,
 *   no_interp, calc_para_refresh, write_reg into the list 10 slots, tispCcmRefresh = 1: irq 45 starts the list.
 * - csccrInfo[0] (t33_swg_csccr_blk, set at init): the entry point writes the init gains into the list 22 slots
 *   3 / 4 / 6 / 1 (csccrCfgMode = t33_bst_cfg_mode) and sets tispCsccrRefresh: the CSCCR frame done (irq 57)
 *   starts the list; slot 7 (the UV clip of the day / night glue) stays.
 * - defog_info[0]: word 3 the section copy (t33_defog_sec), 4 the para3 table block, 6 the trans-k strength,
 *   8 / 9 the sensor height / width; the weights are recomputed only when the section's size differs (as the
 *   vendor's switch: the zoom byte is not compared); write_reg_para1..3 = t33_defog_init into list 12.
 * - lsc_para[0]: the vendor-layout LSC block of tx_isp_t33_bst.h (group 13b): tisp_lsc_switch_para on the new
 *   section (copied into its tparamsP slot), the block's own gain / CT (they move only through
 *   tisp_lsc_tgain_update / ct_update, which the top bypass bit 0x08 skips on PRJ008, openimp #537); its
 *   write_reg writes the list 6 slots and tispLscRefresh, the LSC frame done (irq 36) starts the list.
 */
#define T33_TP_OFF_CSCCR	17880U
#define T33_TP_OFF_LSC		17488U
static u8 *t33_swg_tp8[4];
static u8 t33_swg_csccr_sec[T32_CSCCR_BYTES];
static u32 *t33_swg_csccr_info[4] = { t33_swg_csccr_blk };
/* defog_info word 4 (tisp_defog_init: 3948 B; para3 tables 2458..3177) with Tiziano_defog_soft's two table pointers */
static struct t32_defog_par t33_dpf_p;
#define t33_swg_defog_tab t33_dpf_p.b
static u32 t33_swg_defog_blk[14], *t33_swg_defog_info[4] = { t33_swg_defog_blk };
/* tisp_ccm_interp_by_ct / _by_ev / no_interp / calc_para_refresh / write_reg: tx_isp_t33_ccmv.h */

/* tisp_csccr_para_refresh_by_mode(mode, 0, section): the mode's range table into the section */
static int tisp_csccr_para_refresh_by_mode(u32 mode, u32 zero)
{
	(void)zero;
	t32_csccr_para_refresh_by_mode(mode, (u8 *)(unsigned long)t33_swg_csccr_blk[3] + 5);
	return 0;
}

/* tisp_csccr_sync_api_para(idx, 1): section byte 15 = 1 copies the mode and ranges to the API block */
static int tisp_csccr_sync_api_para(u32 idx, u32 one)
{
	const u8 *sec = (const u8 *)(unsigned long)t33_swg_csccr_blk[3];

	(void)idx;
	if (one == 1 && sec[15] == 1) {
		t33_swg_csccr_blk[4] = sec[4];
		memcpy((u8 *)t33_swg_csccr_blk + 20, sec + 5, 8);
	}
	return 0;
}

static int tisp_csccr_bin_par_update(u32 idx)
{
	(void)idx;
	return t33_csccr_bin_par_update((const u8 *)(unsigned long)t33_swg_csccr_blk[3],
					(const u8 *)t33_swg_csccr_blk + 20);
}

/* defog_count_weight35abc(idx, height, width, zoom, weight tables of the section) */
static int defog_count_weight35abc(u32 idx, u32 a, u32 b, u32 c, u8 *p0, u8 *p1, u8 *p2, u8 *p3, u8 *p4, u8 *p5,
				   u8 *p6)
{
	u8 *m = vzalloc(t33_defog_scratch_bytes(b, a));

	(void)idx; (void)c; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)p6;
	if (m)
		t33_defog_count_weight(p0 - 0x50, b, a, m);
	vfree(m);
	return 0;
}

static int tisp_defog_write_reg_para1(u32 idx)
{
	(void)idx;	/* part of t33_defog_init (tisp_defog_write_reg_para3) */
	return 0;
}

static int tisp_defog_interp_trans_k(u32 idx, u32 k, u8 *p0, u8 *p1, u8 *p2, u8 *p3, u8 *p4, u8 *p5, u8 *p6)
{
	(void)idx;
	/* feeds the per-frame soft path only (sec + 260), not list 12; p0 = sec + 8 (#878) */
	t33_dpf_interp_trans_k(p0 - 8, k, t33_swg_defog_blk[T33_DPF_I_EV2], p0, p1, p2, p3, p4, p5, p6);
	return 0;
}

static int tisp_defog_update_reg_para2(u32 idx)
{
	(void)idx;	/* the list words are t33_defog_para2; here the parameter block part (#878) */
	t33_dpf_update_reg_para2(t33_defog_sec, &t33_dpf_p);
	return 0;
}

static int tisp_defog_write_reg_para2(u32 idx)
{
	(void)idx;
	return 0;
}

static int tisp_defog_write_reg_para3(u32 idx, u32 zero)
{
	(void)idx; (void)zero;
	T33_SW_TMP();
	t33_gr_commit(T33_MOD_DEFOG, t33_defog_init((const u8 *)(unsigned long)t33_swg_defog_blk[3],
						    t33_swg_defog_blk[9], t33_swg_defog_blk[8],
						    (u32)virt_to_phys((void *)t33_defog_pages), t33_gr_tmp,
						    T33_SLOT_WORDS));
	return 0;
}

#include "tx_isp_t33_i9F.h"
/*
 * g15, DEFOG (list 12): lib_tisp_init's tisp_defog_init (port-loop block i9F, tx_isp_t33_i9F.h) on the vendor
 * blocks: defog_info[0] = t33_swg_defog_blk (56 B: section, DMA buffer, its physical address, strength, the
 * 3948 B table block t33_swg_defog_tab, sizes), the 8 KiB statistics buffer = t33_defog_pages (KSEG0, + 0x80000000
 * = physical), defog_weight[0] (vmalloc), section = tparamsP + 23496 (t33_defog_sec copy).  The vendor's slot
 * stores go through tisp_reg_write_t33 into the registered list 12; its callees: defog_count_weight35abc =
 * t33_defog_count_weight (scratch = defog_weight), write_reg_para1 / write_reg_para2 / write_reg_para3 = the
 * swmid-proven parts t33_defog_para1 / para2 (update_reg_para2 + write_reg_para2) / para3; interp_trans_k feeds
 * the soft path only.  irq 10 / 47 and event 3: the driver's fixed set-up (counted when the numbers differ).
 * Before: the init builder t33_defog_init into the list after a weight count on a vzalloc scratch.
 */
static u8 t33_g15_defog_b220[220];
static u8 *t33_g15_defog_tp[4], *t33_g15_defog_info[4], *t33_g15_defog_wt[4];
static u32 t33_g15_defog_max[4];
static u8 t33_g15_defog_refresh;	/* tispDefogRefresh (list 12 starts with the init refresh set) */
u32 t33_g15_runs[3];		/* defog init, irq / event number mismatches, allocation failures */

static void *t33_g15_defog_kmalloc(u32 size, u32 flags)
{
	(void)flags;
	switch (size) {
	case sizeof(t33_swg_defog_blk):
		memset(t33_swg_defog_blk, 0, sizeof(t33_swg_defog_blk));
		return t33_swg_defog_blk;
	case T33_DEFOG_DMA_BYTES:
		return (void *)t33_defog_pages;
	case sizeof(t33_swg_defog_tab):
		return t33_swg_defog_tab;
	case sizeof(t33_g15_defog_b220):
		return t33_g15_defog_b220;
	}
	return NULL;
}

static void *t33_g15_defog_vmalloc(u32 size)
{
	vfree(t33_defog_weight);
	t33_defog_weight = vmalloc(size);
	return t33_defog_weight;
}

static void t33_g15_fill(void *p, u32 c, u32 n)
{
	if (p)
		memset(p, (int)c, n);
}

static void t33_g15_copy(void *d, const void *s, u32 n)
{
	if (d)
		memcpy(d, s, n);
}

static void t33_g15_defog_hook(u32 id, u32 a, u32 b, u32 c, u32 d)
{
	u32 *l = (t33_mod_built & (1ULL << T33_MOD_DEFOG)) ? t33_mod_buf(T33_MOD_DEFOG) : NULL;

	switch (id) {
	case T33_I9_RCFG:
		if (t33_g14_reg(a, b, NULL))
			t33_g14_reg_fail(15, -1);
		break;
	case T33_I9_ERR:
		t33_g15_runs[2]++;
		t33_g14_alloc_fail(15);
		break;
	case T33_I9_REGW:
		tisp_reg_write_t33(a, b, c, d);
		break;
	case T33_I9_CNT_WEIGHT:	/* (ch, height, width, zoom): the section's weights for this size */
		if (t33_defog_weight && t33_g15_defog_max[0] >= t33_defog_scratch_bytes(c, b)) {
			memset(t33_defog_weight, 0, t33_g15_defog_max[0]);
			t33_defog_count_weight(t33_defog_sec, c, b, t33_defog_weight);
		}
		break;
	case T33_I9_PARA1:
		if (l)
			t33_defog_para1(t33_defog_sec, l);
		break;
	case T33_I9_PARA2_WR:
		if (l)
			t33_defog_para2(t33_defog_sec, l);
		break;
	case T33_I9_PARA3:
		if (l)
			t33_defog_para3(l);
		break;
	case T33_I9_IRQ:
		t33_g15_runs[1] += b != 10U && b != 47U;
		break;
	case T33_I9_EVENT:
		t33_g15_runs[1] += b != 3U;
		break;
	case T33_I9_INTERP_K:	/* (ch, EV, sec + 8, sec + 614): the per-frame soft path's sec + 260 (#878) */
		t33_dpf_interp_trans_k(t33_defog_sec, b, t33_swg_defog_blk[T33_DPF_I_EV2], t33_defog_sec + 8,
				       t33_defog_sec + 614, t33_defog_sec + 623, t33_defog_sec + 632,
				       t33_defog_sec + 641, t33_defog_sec + 650, t33_defog_sec + 260);
		break;
	case T33_I9_PARA2_UPD:	/* the parameter block part; the list words are t33_defog_para2 (#878) */
		t33_dpf_update_reg_para2(t33_defog_sec, &t33_dpf_p);
		break;
	default:
		break;
	}
}

static const struct t33_i9_ops t33_g15_defog_ops = {
	t33_g15_defog_kmalloc, t33_g15_defog_vmalloc, t33_g15_fill, t33_g15_copy, t33_g15_defog_hook,
};

/*
 * Per-frame defog (openimp #878, tx_isp_t33_defogpf.h): what the vendor's tisp_defog_init registers.
 * - system irq 10 (0x30028 bit 10): tisp_defog_interrupt_static in the hard interrupt (statistics of the
 *   bank 0xa230 names into the parameter block, interrupt count) and event 3;
 * - event 3: tisp_defog_par in a work item, once per event (bounded like the T32 one, tx_isp_t32_defogd.c):
 *   update_trans_k, Tiziano_defog_soft, write_reg_para3 into the list 12 slots, tispDefogRefresh = 1;
 * - system irq 47 (msca bit 15): tisp_defog_t33_frame_done_interrupt: with stIspInitPar word 226 == 1 and
 *   tispDefogRefresh == 1 list 12 starts (tisp_rcfg_module_cfg_trig) and the flag clears;
 * - AE event 6: tisp_defog_ev_update in t33_ev_refresh (EV words, tispDefogRefresh).
 * Module parameter t33_defog_pf (default 1); 0 = the init-only path of before (device A/B).
 */
#define T33_DPF_PEND_MAX	4
static bool t33_defog_pf = true;
module_param(t33_defog_pf, bool, 0644);
MODULE_PARM_DESC(t33_defog_pf, "T33 per-frame defog (irq 10 / event 3 / irq 47 as the vendor, openimp #878); 0 = init only");
static DEFINE_SPINLOCK(t33_dpf_lock);
static bool t33_dpf_on;
static u32 t33_dpf_pend;
static void t33_dpf_work_fn(struct work_struct *w);
static DECLARE_WORK(t33_dpf_work, t33_dpf_work_fn);

static void t33_dpf_sync(const u8 *b, u32 len)
{
	dma_cache_wback_inv((unsigned long)b, len);	/* private_dma_cache_sync of the bank the core wrote */
}

static bool t33_dpf_active(void)
{
	return READ_ONCE(t33_dpf_on) && READ_ONCE(t33_defog_pf);
}

/* system irq 10: tisp_defog_interrupt_static, event 3 */
static void t33_dpf_isr(void)
{
	spin_lock(&t33_dpf_lock);
	t33_dpf_runs[0]++;
	if (t33_dpf_active() && t33_defog_pages) {
		t33_dpf_interrupt_static(t33_swg_defog_blk, &t33_dpf_p, isp_rd(T33_DPF_BANK_REG),
					 (const u8 *)t33_defog_pages, t33_dpf_sync);
		if (t33_dpf_pend < T33_DPF_PEND_MAX)
			t33_dpf_pend++;
		else
			t33_dpf_runs[3]++;
		schedule_work(&t33_dpf_work);
	}
	spin_unlock(&t33_dpf_lock);
}

/* system irq 47: tisp_defog_t33_frame_done_interrupt */
static void t33_dpf_fd(void)
{
	if (!t33_dpf_active() || t33_st_isp_init_par_mode[226] != 1)
		return;
	if (t33_g15_defog_refresh == 1) {
		tisp_rcfg_module_cfg_trig(0, T33_MOD_DEFOG);
		t33_g15_defog_refresh = 0;
		t33_dpf_runs[4]++;
	}
}

static void t33_dpf_slot(u32 slot, u32 val)
{
	tisp_reg_write_t33(0, T33_MOD_DEFOG, slot, val);
}

/* event 3: tisp_defog_par, once per queued event */
static void t33_dpf_work_fn(struct work_struct *w)
{
	unsigned long f;

	(void)w;
	mutex_lock(&t33_gr_lock);
	for (;;) {
		spin_lock_irqsave(&t33_dpf_lock, f);
		if (!t33_dpf_on || !t33_dpf_pend) {
			t33_dpf_pend = 0;
			spin_unlock_irqrestore(&t33_dpf_lock, f);
			break;
		}
		t33_dpf_pend--;
		spin_unlock_irqrestore(&t33_dpf_lock, f);
		if (!READ_ONCE(t33_defog_pf))
			continue;
		t33_dpf_par(t33_defog_sec, t33_swg_defog_blk, &t33_dpf_p);
		if (t33_bst_cfg_mode)		/* slot indexes (the open lists always use them) */
			t33_dpf_write_reg_para3(&t33_dpf_p, t33_dpf_slot);
		t33_g15_defog_refresh = 1;	/* tispDefogRefresh */
		t33_dpf_runs[1]++;
	}
	mutex_unlock(&t33_gr_lock);
}

/* before tisp_defog_init runs again and before its buffers go: no interrupt / event works on them */
static void t33_dpf_stop(void)
{
	unsigned long f;

	spin_lock_irqsave(&t33_dpf_lock, f);
	t33_dpf_on = false;
	t33_dpf_pend = 0;
	spin_unlock_irqrestore(&t33_dpf_lock, f);
	cancel_work_sync(&t33_dpf_work);
}

/* AE event 6 (t33_ev_refresh, t33_gr_lock held): tisp_defog_ev_update */
static bool t33_dpf_ev(u32 lo, u32 hi, u32 x, u32 y)
{
	if (!t33_dpf_active())
		return false;
	t33_dpf_ev_update(t33_defog_sec, t33_swg_defog_blk, &t33_dpf_p, lo, hi, x, y);
	t33_g15_defog_refresh = 1;	/* tispDefogRefresh */
	t33_dpf_runs[2]++;
	return true;
}

static void t33_g15_defog_init(u32 width, u32 height)
{
	unsigned long f;
	int r;
	u8 sz[928];

	/* stIspInitPar of channel 0 with the sensor size (u16 width / height at 12 / 14) */
	memcpy(sz, t33_st_isp_init_par_mode, sizeof(sz));
	t33_defog_put16(sz + 12, (u16)width);
	t33_defog_put16(sz + 14, (u16)height);
	t33_g15_defog_tp[0] = t33_defog_sec - 23496;
	t33_dpf_stop();
	memset(&t33_dpf_p, 0, sizeof(t33_dpf_p));
	r = t33_defog_init_ctx(0, sz, t33_g15_defog_max, t33_g15_defog_wt, t33_g15_defog_info, t33_g15_defog_tp,
			       &t33_bst_cfg_mode, &t33_g15_defog_refresh, &t33_g15_defog_ops);
	t33_g15_runs[0]++;
	/* system_irq_func_set(10 / 47), tisp_event_set_cb(3) of the init: the per-frame path (#878) */
	if (!r && t33_swg_defog_blk[T33_DPF_I_BUF]) {	/* list 12 slots: tisp_reg_write_t33 checks the list */
		spin_lock_irqsave(&t33_dpf_lock, f);
		t33_dpf_on = true;
		spin_unlock_irqrestore(&t33_dpf_lock, f);
	}
}

/*
 * tisp_lsc_switch_para's callees on the vendor-layout lsc_para (tx_isp_t33_bst.h, group 13b): the proven
 * lut_valid_check / interp_by_tgain / func_lsc_para_update / write_reg ports, interp_by_ct = t33_lsc_ct_info
 */
static int tisp_lsc_lut_valid_check(u32 h, u32 w, u32 mh, u32 mw, u8 *p0, u8 *p1, u8 *p2, u8 *p3, u8 *p4, u8 *p5,
				    u8 *p6)
{
	t33_lsc_mesh_check((int)h, (int)w, (int)mh, (int)mw, p0, p1, p2, p3, (u32 *)p4, p5, (u32 *)p6);
	return 0;
}

static int tisp_lsc_interp_by_tgain(u32 idx, u32 gain, u32 force)
{
	(void)force;
	t33_lsc_interp_by_tgain_v(lsc_para[idx & 3], gain);
	return 0;
}

static int tisp_lsc_interp_by_ct(u32 idx, u32 ct, u32 force)
{
	(void)force;
	if (lsc_para[idx & 3])
		t33_lsc_ct_info(lsc_para[idx & 3], &t33_lsc_m, t33_lsc_tab, ct);
	return 0;
}

static int func_lsc_para_update(u32 idx)
{
	t33_lsc_para_update_info(lsc_para[idx & 3]);
	return 0;
}

static int tisp_lsc_write_reg(u32 idx, u32 mode)
{
	t33_lsc_write_reg(idx, mode);
	return 0;
}

/*
 * tisp_lsc_hvflip (vendor 0xf9c18, tuning id 0x73 after the sensor mirror / flip, openimp #727) ->
 * tisp_lsc_mirror_flip (0x3feb0, tx_isp_t33_lscmf.h, oracle block t33-lscmf) on the live lsc_para[0] and its CT
 * tables (t33_lsc_tab = stLutInfo[0] + 0x14: the driver's IQ bin copy or the appended-table copy, both kept until
 * the next bin): the tables flipped in place, then interp_by_ct / func_lsc_para_update / write_reg (list 6 slots,
 * tispLscRefresh: the LSC frame done starts the list) and the vendor's message.  The vendor takes no lock; here
 * the interrupts are off around it as around tisp_lsc_switch_para (the frame done reads lsc_para).  Always 0.
 */
static const struct t33_lscmf_ops t33_lscmf_ops = { tisp_lsc_interp_by_ct, func_lsc_para_update, tisp_lsc_write_reg };

/*
 * Beyond vendor (openimp #728): only the change against the orientation the live CT tables carry (t33_lsc_mf,
 * tx_isp_t33_bst.h; 0 whenever a bin load / switch installs a fresh copy) is exchanged (t33_lsc_mf_delta).
 */


int t33_lsc_hvflip(u32 ch, u32 flip, u32 mirror)
{
	unsigned long flags;
	const u8 *cnt;
	u32 df, dm;

	if ((ch & 3) || !lsc_para[0] || !t33_lsc_tab)
		return 0;
	local_irq_save(flags);
	t33_lsc_mf_delta(&t33_lsc_mf, flip, mirror, &df, &dm);
	t33_lsc_mirror_flip(ch, lsc_para[0], (u8 *)t33_lsc_tab, df, dm, &t33_lscmf_ops);
	local_irq_restore(flags);
	t33_lsc_flips++;
	cnt = (const u8 *)(unsigned long)lsc_para[0][3];
	isp_printf(0, "\n  [func is %s, line is %d] lsc filp times is %d, mirror times is %d\n", "tisp_lsc_mirror_flip",
		   0x5c0, cnt[8], cnt[9]);
	return 0;
}

#include "tx_isp_t33_swmid.h"

/*
 * tisp_tmo_switch_para (oracle block tmoSW) on the vendor-layout tmoInfo[0] (t33_swg_tmo_info, 10880 bytes) that
 * tisp_tmo_init built (group 16): 2732 the section pointer (a private copy of the new section, tparamsP + 10140),
 * 10808 / 10812 / 10816 the EV and BV of tisp_tmo_ev_update (ev >> 10, ev >> 42, BV 0 as at init).
 * Callees: func_bin_para_update / func_tmo_ic_para_update = the oracle-proven tmoA ports;
 * tisp_tmo_tmo_lumcurve_ev_interp / _satcurve_ev_interp / _ramY_vi0_reg_refresh = the r13A ports (drc_ctrl: DRC
 * off, see group 16); tisp_reg_write_t33 = the slot into a copy of list 8, committed after the call
 * (t33_gr_commit); system_reg_write = the direct register write.
 */
#define T33_TI(o_)	t33_swg_tmo_info[(o_) / 4]
#define T33_TI16(o_)	((u16 *)t33_swg_tmo_info)[(o_) / 2]

static void t33_swg_tmo_bin(u32 ch)
{
	(void)ch;
	t33_tmo_bin_para(t33_swg_tmo_info, (const u8 *)(unsigned long)T33_TI(T33_TMOA_SEC_PTR));
}

static void t33_swg_tmo_lum(u32 ch)
{
	t33_g16_tmo_lum(ch);	/* tisp_tmo_tmo_lumcurve_ev_interp (r13A) */
}

static void t33_swg_tmo_sat(u32 ch)
{
	t33_g16_tmo_sat(ch);	/* tisp_tmo_tmo_satcurve_ev_interp (r13A) */
}

static void t33_swg_tmo_ic(u32 ch)
{
	(void)ch;
	t33_tmo_ic_para(t33_swg_tmo_info);
}

static void t33_swg_tmo_wr(u32 ch, u32 blk, u32 reg, u32 val)
{
	if (!t33_bst_cfg_mode)
		tisp_reg_write_t33(ch, blk, reg, val);
	else if (reg + 1 <= t33_gr_tmp[0] && reg + 1 < T33_SLOT_WORDS)	/* bounds of tisp_reg_write_t33 */
		T33_SW(t33_gr_tmp, reg, val);
	else
		t33_bst_bad++;
}

static void t33_swg_tmo_sysw(u32 reg, u32 val)
{
	isp_wr(reg, val);
}

/* tisp_tmo_ramY_vi0_reg_refresh (r13A) with its slot stores into the copy of list 8 */
static const struct t33_r13a_ops t33_swg_r13a_ops = {
	t33_g16_tmo_drc, t33_swg_tmo_wr, t33_swg_tmo_sysw, t33_g16_tmo_v23,
};

static void t33_swg_tmo_y(u32 ch)
{
	t33_tmo_ramy_refresh(&t33_swg_r13a_ops, t33_g16_tmo_info, t33_bst_cfg_mode, ch & 3);
}

static const struct t33_tmosw_ops t33_swg_tmo_ops = {
	t33_swg_tmo_bin, t33_swg_tmo_lum, t33_swg_tmo_sat, t33_swg_tmo_ic, t33_swg_tmo_y, t33_swg_tmo_wr,
	t33_swg_tmo_sysw,
};

static noinline_for_stack void t33_swg_tmo(const u8 *sec, unsigned long long ev)
{
	u32 *buf;

	if (!(t33_mod_built & (1ULL << T33_MOD_TMO)) || !(buf = t33_mod_buf(T33_MOD_TMO)))
		return;
	memcpy(t33_swg_tmo_sec, sec, sizeof(t33_swg_tmo_sec));
	T33_TI(10808) = (u32)(ev >> 10);
	T33_TI(10812) = (u32)(ev >> 42);
	T33_TI(10816) = 0;
	memcpy(t33_gr_tmp, buf, T33_SLOT_WORDS * 4);
	t33_tmo_switch_para(&t33_swg_tmo_ops, 0, t33_swg_tmo_sec - T33_TMOSW_SEC_OFF, t33_swg_tmo_info,
			    t33_bst_cfg_mode, &t33_swg_tmo_refresh);
	t33_gr_commit(T33_MOD_TMO, 1);
}
#undef T33_TI
#undef T33_TI16

static noinline_for_stack void t33_sw_tone(const u8 *bin, u32 bytes, u32 width, u32 height, u32 gain, u32 ct, u32 lo,
					   u32 hi)
{
	unsigned long long ev = (unsigned long long)hi << 32 | lo;
	u32 off, len, tb, *buf;

	/*
	 * tisp_lsc_switch_para: interp_by_tgain / _by_ct (forced) at the block's OWN gain (info + 0x14)
	 * and CT (info + 8), not the AE / AWB values of the other blocks: both move only through
	 * tisp_lsc_tgain_update / tisp_lsc_ct_update, which the top bypass bit 0x08 skips (PRJ008) and
	 * which are not ported, so they stay at the init values (openimp #537)
	 */
	if (T33_SW_SEC(LSC) && lsc_para[0]) {
		const u8 *tab, *sec = t33_lsc_src(bin, bytes, off, len, &tab, &tb);
		unsigned long flags;

		if (sec[0x94] && sec[0x94] <= 8 && tb >= sec[0x94] * T33_LSC_TABLE_BYTES) {
			local_irq_save(flags);
			memcpy(t33_lsc_sec, sec, sizeof(t33_lsc_sec));	/* the bin parse into tparamsP */
			t33_lsc_tab = tab;
			t33_lsc_mf = 0;		/* fresh tables (#728) */
			t33_swg_tp8[0] = t33_lsc_sec - T33_TP_OFF_LSC;
			t33_lsc_switch_para(0, t33_swg_tp8, lsc_para);
			local_irq_restore(flags);
			t33_lsc_runs[1]++;
			t33_gr_runs[T33_MOD_LSC]++;
		}
	}
	/* tisp_tmo_switch_para (tx_isp_t33_tmoSW.h) on the vendor-layout tmoInfo block */
	if (T33_SW_SEC(TMO) && t33_tmo_pages)
		t33_swg_tmo(bin + off, ev);
	/*
	 * tisp_gamma_switch_para on gamma_para: the new section (raw: its 0x42 / 0x43 may select the API curve, PRJ008
	 * night 0x43 = 2) and curves, interp_by_ev (forced) / update_curve at ip, transform, write_lut_rgb; the slots
	 * go through the rebuilt list and run at the next frame end as before (blocks gammaev, gammav)
	 */
	if (T33_SW_SEC(GAMMA) && gamma_para[0] && (t33_mod_built & (1ULL << T33_MOD_GAMMA)) &&
	    (buf = t33_mod_buf(T33_MOD_GAMMA))) {
		unsigned long flags;
		int ok;

		memcpy(t33_gr_tmp, buf, T33_SLOT_WORDS * 4);
		local_irq_save(flags);
		ok = !t33_gamma_load(bin + off, bin + off + len, t32_iq_payload(bin + off, bytes - off, T33_SEC_GAMMA));
		if (ok)
			t33_gv_switch(0, t33_gamma_v, t33_gamma_sec, t33_gamma_tab, gammaCfgMode, t33_gamma_tmp_wr);
		local_irq_restore(flags);
		if (ok) {
			t33_gr_commit(T33_MOD_GAMMA, 1);
			t33_gamma_runs[3]++;
		}
	}
	/* tisp_ccm_switch_para: interp_by_ct (forced) / _by_ev, calc_para_refresh, write_reg */
	if (T33_SW_SEC(CCM) && !t33_ccm_new_bin(bin + off, bin + off + len,
						 t32_iq_payload(bin + off, bytes - off, T33_SEC_CCM))) {
		unsigned long flags;

		t33_swg_tp8[0] = t33_ccm_sec - T33_TP_OFF_CCM;
		local_irq_save(flags);
		t33_ccm_switch_para(0, t33_swg_tp8, t33_ccm_parap, &tispCcmRefresh);
		local_irq_restore(flags);
		t33_ccm_runs[3]++;
	}
}

/* clm, bcsh */
static noinline_for_stack void t33_sw_color(const u8 *bin, u32 bytes)
{
	u32 off, len, *buf;

	/*
	 * tisp_clm_switch_para: ct_interp (forced) at the block's last CT (clm_info word 0x18), calc_para_refresh,
	 * write_reg (the csc set: tisp_csc_get on csc_para, the init's CSC section; the vendor has no CSC switch)
	 */
	if (T33_SW_SEC(CLM)) {
		t33_swg_sm2.bin = bin;
		t33_swg_sm2.bytes = bytes;
		t33_swg_sm2.len = len;
		t33_swg_clm_blk[24] = t33_clm_ct_last;
		t33_swg_clm_info[0] = (u32)(unsigned long)t33_swg_clm_blk;
		t33_swg_tp32[0] = (u32)(unsigned long)(bin + off) - T33_TP_OFF_CLM;
		t33_clm_switch_para(0, t33_swg_clm_info, t33_swg_tp32);
	}
	/*
	 * tisp_bcsh_switch_para: get_new_para, interp_by_ev / _by_ct at the block's EV / CT, calc_para_refresh
	 * (block bcshsw).  The block state only: the vendor writes no list word here, list 18 keeps the last
	 * EV / CT update's words until the next one (block bcshlist; PRJ008 night gates both, openimp #507).
	 */
	if (T33_SW_SEC(BCSH) && (t33_mod_built & (1ULL << T33_MOD_BCSH)) && (buf = t33_mod_buf(T33_MOD_BCSH))) {
		memcpy(t33_gr_tmp, buf, T33_SLOT_WORDS * 4);
		memcpy(t33_bcsh_sec, bin + off, T33_BCSH_SEC_BYTES);
		if (t33_bcsh_switch_para(t33_bcsh_sec, bin + off + len, t33_bcsh_st, t33_gr_tmp))
			t33_gr_commit(T33_MOD_BCSH, 1);
	}
}

/* defog, af, csccr */
static noinline_for_stack void t33_sw_space(const u8 *bin, u32 bytes, u32 width, u32 height)
{
	u32 off, len;

	/* tisp_defog_switch_para: weights for the size, write_reg_para1..3 */
	if (T33_SW_SEC(DEFOG) && t33_defog_pages) {
		memcpy(t33_defog_sec, bin + off, sizeof(t33_defog_sec));
		t33_swg_defog_blk[3] = (u32)(unsigned long)t33_defog_sec;
		t33_swg_defog_blk[4] = (u32)(unsigned long)t33_swg_defog_tab;
		/* word 6 keeps the EV of tisp_defog_ev_update (vendor info + 0x18; #878) */
		t33_swg_defog_blk[8] = height;
		t33_swg_defog_blk[9] = width;
		t33_defog_switch_para(0, t33_swg_defog_info);
	}
	/* tisp_af_switch_para: af_par_init from the new section, all_reg_write */
	if (T33_SW_SEC(AF) && t33_af_pages) {
		t33_swg_sm2.w = width;
		t33_swg_sm2.h = height;
		t33_swg_af_info[0] = (u32)(unsigned long)t33_swg_af_blk;
		t33_swg_tp32[0] = (u32)(unsigned long)(bin + off) - T33_TP_OFF_AF;
		t33_af_switch_para(0, t33_swg_af_info, t33_swg_tp32);
	}
	/* tisp_csccr_switch_para: para_refresh_by_mode, sync_api_para, bin_par_update, the init gains into list 22 */
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_CSCCR, &len);
	if (off && T32_IQ_SEC_OK(len, T32_CSCCR_BYTES) && (t33_mod_built & (1ULL << T33_MOD_CSCCR))) {
		unsigned long flags;

		memcpy(t33_swg_csccr_sec, bin + off, sizeof(t33_swg_csccr_sec));
		t33_swg_tp8[0] = t33_swg_csccr_sec - T33_TP_OFF_CSCCR;
		local_irq_save(flags);
		t33_csccr_switch_para(0, t33_swg_tp8, t33_swg_csccr_info, &t33_bst_cfg_mode, &t33_csccr_refresh);
		local_irq_restore(flags);
		t33_gr_runs[T33_MOD_CSCCR]++;
	}
}

void t33_refresh_para(u32 mode)
{
	const u8 *bin;
	u32 bytes, off, len, width, height, code, gain, ct, lo, hi;
	unsigned int id;
	int bayer = -1;
	u64 sw;

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
	t33_bin_append(bin, bytes);	/* tisp_bin_data_parse */
	t33_sw_top(bin, bytes, width, height, bayer);
	/* tisp_dpc / ydns / sdns / cdns / dmsc / ysp / mdns_switch_para (tx_isp_t33_swglue.h) */
	sw = t33_swg_small(bin, bytes);
	/* tisp_blc_switch_para: the new section, interp_by_tgain at blc_para's gain, write_reg (tx_isp_t33_bst.h) */
	if (T33_SW_SEC(BLC))
		t33_blc_switch(bin + off);
	t33_sw_tone(bin, bytes, width, height, gain, ct, lo, hi);
	t33_sw_color(bin, bytes);
	t33_sw_space(bin, bytes, width, height);
	t33_sw_lce(bin, bytes);
	sw |= t33_swg_rdns(bin, bytes);		/* tisp_rdns_switch_para */
	/*
	 * a gain block whose section is missing at the current gain, its gate
	 * open; the switched ones keep their own gain and gate as in the vendor
	 */
	for (id = 0; id < T33_MODULES; id++)
		if (!(sw & (1ULL << id)))
			t33_gr_last[id] = 0xffffffffU;
	t33_sw_runs++;
	mutex_unlock(&t33_gr_lock);
	/*
	 * tisp_ae_switch_para (vendor: after blc): the new AE section, list 30 from g_ae_contexts[0],
	 * normal_ae_dn_par_refresh.  Outside t33_gr_lock: the AE work holds the AE lock while its EV / gain
	 * events take t33_gr_lock (t33_ev_refresh / t33_tgain_refresh).
	 */
	if ((off = t32_iq_section(bin, bytes, T33_SEC_AE, &len)) && len >= T33_AE_SEC_MIN && g_ae_contexts[0]) {
		t33_aec_switch_sec(bin + off, len);
		t33_ae_switch_para(0);
	}
	/* tisp_awb_dn_params_refresh (vendor: after the AE): the new AWB section, list 29 at the next AWB irq */
	if ((off = t32_iq_section(bin, bytes, T33_SEC_AWB, &len)) && len >= T33_AWB_SEC_MIN)
		t33_awbc_dn_refresh(bin + off, len);
	t33_gr_refresh(gain, sw, 0);
	isp_printf(0, "tx-isp-t33: IQ bin switch (mode %u): lists rebuilt at gain %#x ct %u ev %#x:%#x, switch_para %#llx\n",
		   mode, gain, ct, hi, lo, (unsigned long long)sw);
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
#ifdef TX_ISP_T33
		t32_tisp_flush("gamma", t32_gamma_init(bin + off, bin + off + len, bytes - off - len, tisp_w));
#else
		/* T32: the state is kept for tisp_s_Gamma / tisp_g_Gamma (tx_isp_t32_tisp.c, openimp #717) */
		t32_tisp_flush("gamma", t32_pf_attach_gamma(bin + off, bin + off + len, bytes - off - len, tisp_w));
#endif
	off = t32_iq_section(bin, bytes, T32_SEC_CDNS, &len);
#ifdef TX_ISP_T33
	if (off && len == T33_CDNS_SEC_BYTES)
		t33_g14_dns_init(T33_BST_CDNS, bin + off);	/* tisp_cdns_init (group 14a) */
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
#ifndef TX_ISP_T33
	/* lib_tisp_init: tisp_defog_init after the LSC init, before tisp_ydns_init (vendor 0x25448, openimp #854) */
	if (!t32_core_sensor_size(&width, &height))
		t32_defog_attach(bin, bytes, width, height);
#endif
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

	if (!rcfg_pages) {
		isp_wr(0x002c, 2);
		return;
	}
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
	/*
	 * Vendor order (openimp #850): tisp_single_lsc_rcfg_cfg_trig writes 0x2c = 2 right
	 * after the trigger (vendor T32 ko 0x1cbb4), then lib_tisp_stream_on polls the whole
	 * 0x22014 for 0 (0x25b40, 20000 polls).  Before #850 0x2c = 2 came after the wait.
	 */
	isp_wr(0x002c, 2);
	if (t32_wait_clear(T32_RDMA_TRIG, 0xffffffffU, 20000)) {
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
#ifdef TX_ISP_T33
	/* lib_tisp_init -> tisp_all_reset (T33: 20000 polls, core state register 0 nonzero) */
	t33_dn_stop_to = false;
	t33_all_reset(&t33_dn);
	/*
	 * openimp #514: on the T33L with the thingino boot chain the ISP sometimes
	 * never acknowledges the stop request (0x204, and the CPM SRBC bit 20
	 * before it); the vendor tisp_all_reset then returns without the core
	 * reset and no register list runs (no frames).  Here the core reset is
	 * still done (the pre-#99 T32-style order, which streamed in that state).
	 */
	if (t33_dn_stop_to) {
		isp_printf(2, "tx-isp-t33: core reset without the stop ack (#514)\n");
		isp_wr(T33_RST_CORE, T33_RST_CORE_VAL);
		for (i = T33_RST_POLLS; i && !isp_rd(T33_RST_STATE); i--)
			;
		if (!i)
			isp_printf(2, "tx-isp-t33: isp all reset timeout\n");
	}
	i = 0;
#else
	isp_wr(T32_RDMA_IRQ_EN, 0xffffffff);
	isp_wr(T32_RDMA_IRQ_CLR, 0xffffffff);
	isp_wr(T32_CORE_IDLE_REQ, 1);
	/*
	 * lib_tisp_all_reset: wait for the idle ack (0x204 = 1) and for the core
	 * reset (0x24 low nibble) before the next write.  On the T33 silicon the
	 * reset takes longer than the following writes: without the waits the
	 * RCFG table, control and masks below were reset again (0x22020 read
	 * 0x18000000, 0x22018 0) and no register list ever ran (openimp #393).
	 */
	for (i = 0; i < 10000 && isp_rd(0x0204) != 1; i++)
		udelay(1);
	if (i == 10000)
		isp_printf(2, "tx-isp-t32: ISP idle ack timeout\n");
	isp_wr(0x0024, 0x40000001);
	for (i = 0; i < 10000 && (isp_rd(0x0024) & 0xf); i++)
		udelay(1);
	if (i == 10000)
		isp_printf(2, "tx-isp-t32: ISP core reset timeout\n");
#endif
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
#ifdef TX_ISP_T33
	/*
	 * lib_tisp_init -> tisp_top_ipc_init (one sensor), after the core reset
	 * as in the vendor trace.  Written before it (as here until #393) the
	 * reset cleared 0x10a4 on the T33 silicon: the IPC start (0x1040)
	 * stayed pending and no frame reached the core.
	 */
	isp_wr(0x1080, 0);
	isp_wr(0x1084, 0);
	isp_wr(0x10a0, 0xffffffff);
	isp_wr(0x10a4, 4);
	isp_wr(0x10c4, w * h / 6);
	isp_wr(0x10c8, w * h / 6);
	isp_wr(0x10cc, 0xffffffff);
	isp_wr(0x10d0, 0xffffffff);
#else
	{
		/*
		 * T32: lib_tisp_init -> tisp_top_ipc_init (vendor T32 ko H20260618a 0x2987c,
		 * one sensor, not direct mode: info +900 = 1, IR bit off; vendor QEMU trace
		 * t32plan/s0/tr10-isp.txt 5632-5644: 0x1080 = 0, 0x1084 = 0, 0x1090 = 1,
		 * 0x10a0 = ~0, 0x10a4 = 4, 0x10a8..0x10b8, 0x10bc / 0x10c0 = ~0,
		 * 0x10c4 / 0x10c8 = w * h / 6).  The T32 layout differs from the T33 one
		 * above (0x1090, 0x10a8..0x10c0, no 0x10cc / 0x10d0).  Without it the IPC
		 * start (0x1040) stayed pending on the T32 silicon and no frame reached the
		 * core (openimp #850).  tisp_top_ipc_set_mem_cfg follows at
		 * SET_ISP_IPC_MEM_INFO (tx_isp_t32_core.c, #759), as in the vendor flow.
		 */
		static u8 info[904];
		struct t32_front_write tw[13];
		unsigned int k, n;

		memset(info, 0, sizeof(info));
		info[0] = w; info[1] = w >> 8; info[2] = w >> 16; info[3] = w >> 24;
		info[4] = h; info[5] = h >> 8; info[6] = h >> 16; info[7] = h >> 24;
		info[900] = 1;
		n = t32_top_ipc_init(info, tw);
		for (k = 0; k < n; k++)
			isp_wr(tw[k].reg, tw[k].val);
	}
	/*
	 * T32 defog statistics DMA area (tisp_defog_init, t32_defog_init in
	 * tx_isp_t32_defog.h: an 8 KiB kmalloc buffer, 0xa210..0xa21c = phys + 0 /
	 * 2048 / 4096 / 6144, 0xa20c = 3).  Before #850 the T32 path never wrote it:
	 * with the core running (IPC init) a non-bypassed defog block wrote its
	 * statistics to physical 0 (openimp #850).
	 */
	if (!t33_defog_pages)
		t33_defog_pages = t32_dma_zpages(T33_DEFOG_ORDER);
	if (t33_defog_pages) {
		u32 pa = (u32)virt_to_phys((void *)t33_defog_pages);

		isp_wr(0xa210, pa);
		isp_wr(0xa214, pa + 2048);
		isp_wr(0xa218, pa + 4096);
		isp_wr(0xa21c, pa + 6144);
		isp_wr(0xa20c, 3);
	}
#endif
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
#ifdef TX_ISP_T33
	/*
	 * After the VIC: the IPC words as the vendor 2.0.2.1 trace (0x80300,
	 * 0x1094 / 0x1098, 0x10c4 / 0x10c8 back to 0).  On the T33 silicon the
	 * IPC start (0x1040) stayed pending and no frame reached the core with
	 * the init values left in 0x10c4 / 0x10c8 (openimp #393).
	 */
	t32_vic_wr(0, 0x300, 0);
	isp_wr(0x1094, 0);
	isp_wr(0x1098, 0x200000);
	isp_wr(0x10c4, 0);
	isp_wr(0x10c8, 0);
#endif
	/* MSCA input size and channel defaults (T33: list 23 carries them) */
	if (!T32_SOC_LIST_IRQ) {
		isp_wr(0xf0404, size);
		isp_wr(0xf0090, 0);
		isp_wr(0xf0094, size);
		isp_wr(T32_MSCA_CH_EN2, 0);
		isp_wr(T32_MSCA_CH_EN, 0);
		isp_wr(T32_MSCA_UPDATE, 1);
	}
	/*
	 * T32 and T33: no write to 0x200 after the reset (T32 until #850 wrote 0 here;
	 * the vendor T32 ko writes 0x200 only with 1: 0x24ee8, 0x25010, 0x258fc, 0x8fd3c).
	 * T33: no write to 0x200 after the reset.  The vendor module writes the idle request only in
	 * tisp_all_reset (1), lib_tisp_func_reset (0x80020001) and tisp_reset (1), never 0 (all three
	 * "li a0,0x200" register writes of tx-isp-t33.ko); the vendor QEMU trace shows no other write.
	 */
	isp_printf(0, "tx-isp-t32: VIC %ux%u fmt %u line %u words, CSI %u lanes settle %#x\n",
		   w, h, t32_attr_u32(attr, T32_ATTR_SC_CSI_FMT), seq.w[1].val,
		   csi_cfg.lanes_m1 + 1, csi_cfg.settle);
	return 0;
}

#ifdef TX_ISP_T33
/*
 * T33: the vendor's mipi_phy_stream_on (tx_isp_t33_mipi.h, oracle block mipiphy), single mode, the sensor on CSI
 * host 0 (t32_csi_cfg_from_attr takes no other): host 0 pulsed at every stream-on, lanes, D-PHY 0 and the five
 * settle words from get_settle_time; the D-PHY word 0x80 and the lane words 0x128 + n * 0x80 keep their reset values.
 */
static void t33_mipi_host(unsigned int ctl, unsigned int reg, unsigned int val)
{
	if (!ctl)
		writel(val, csi_base + reg);
}

static void t33_mipi_phy(unsigned int reg, unsigned int val)
{
	writel(val, dphy_base + reg);
}

static void t33_mipi_sleep(void)
{
	msleep(1);
}

static void t33_mipi_log(int what, unsigned int val)
{
	isp_printf(what >= T33_MIPI_LOG_SETTLE0 ? 0 : 2, "tx-isp-t33: mipi_phy_stream_on: message %d (%u)\n", what, val);
}

static const struct t33_mipi_ops t33_mipi_ops = { t33_mipi_host, t33_mipi_phy, t33_mipi_sleep, t33_mipi_log };

static void t32_csi_start(void)
{
	const struct t33_mipi_sensor s = { T32_DBUS_MIPI, csi_cfg.clk, 0, (unsigned char)(csi_cfg.lanes_m1 + 1) };

	t33_mipi_phy_stream_on(&t33_mipi_ops, 1, 2, 0, &s);
}
#else
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
#endif

static void t32_csi_stop(void)
{
	writel(0, csi_base + 0x10);
}

/*
 * STREAMON in the vendor's order: interrupts on, the stream-on register
 * list, core run bits, VIC interrupt setup, both VICs reset and run, then
 * the CSI host and D-PHY.
 */
#ifndef TX_ISP_T33
/*
 * tisp_mdns_set_mem_cfg at SET_ISP_MEM_INFO (lib_tisp_set_isp_memory_space, vendor trace
 * t32plan/s0/tr10-isp.txt 5682-5687: 0xd040 = base, 0xd080, 0xd050 / 0xd054, 0xd058 / 0xd05c):
 * the MDNS reference / statistics planes in the ISP working memory libimp handed over, the
 * layout of t32_isp_mem_size (GET_ISP_MEM_INFO).  Single sensor, no hold check, no func_en
 * (the vendor trace writes 0xd058 / 0xd05c).  Never written on T32 before #850: the core
 * then wrote its MDNS planes to physical 0.
 */
void t32_hw_mdns_mem_cfg(u32 pa, u32 width, u32 height, u32 memopt)
{
	struct t32_mdns_mem m;
	struct t32_mdns_memchk chk;
	struct t32_front_write w[12];
	int i, n;

	memset(&m, 0, sizeof(m));
	memset(&chk, 0, sizeof(chk));
	m.memopt = (u16)memopt;
	n = t32_mdns_set_mem_cfg(&m, NULL, width, height, pa, 0, &chk, w);
	for (i = 0; i < n && i < (int)ARRAY_SIZE(w); i++)
		isp_wr(w[i].reg, w[i].val);
	isp_printf(0, "tx-isp-t32: MDNS memory %#x..%#x (memopt %u)\n", pa, pa + chk.end, memopt);
}

/*
 * Guard (openimp #850, beyond the vendor: the vendor programs every address before the
 * IPC start): a block the top bypass word (0x40) leaves on but whose DMA address is 0
 * would write to physical 0 once the core runs (silent hang of the T32).  Bypass it and
 * say so instead.  MDNS 0x800 (T32_LTG_BYP_MDNS), defog 0x100 (T32_LEV_BYP_DEFOG).
 */
static void t32_dma_addr_guard(void)
{
	u32 byp = isp_rd(0x0040), add = 0;

	if (!(byp & 0x800) && (!isp_rd(0xd040) || !isp_rd(0xd050)))
		add |= 0x800;
	if (!(byp & 0x100) && !isp_rd(0xa210))
		add |= 0x100;
	if (!add)
		return;
	isp_wr(0x0040, byp | add);
	isp_printf(2, "tx-isp-t32: DMA address 0 for%s%s: bypassed (top %#x -> %#x)\n",
		   (add & 0x800) ? " MDNS" : "", (add & 0x100) ? " defog" : "", byp, byp | add);
}

/*
 * Defog (openimp #852, #854): tisp_defog_init and the per-frame defog run in
 * tx_isp_t32_defogd.c.  When they did not attach (no defog section in the bin,
 * t32_ported_paths bit 20 off, allocation failure) the block would run with its
 * reset grid and draw horizontal streaks over the picture (#852): then it is
 * bypassed (top bit 0x100) as before #854.
 */
static void t32_defog_unwired_bypass(void)
{
	u32 byp = isp_rd(0x0040);

	if (t32_defog_attached() || (byp & 0x100))
		return;
	isp_wr(0x0040, byp | 0x100);
	isp_printf(1, "tx-isp-t32: defog not attached: bypassed (top %#x -> %#x)\n", byp, byp | 0x100);
}

/*
 * The defog statistics buffer of tisp_defog_init (tx_isp_t32_defogd.c, openimp
 * #892): the static area, allocated once and freed only at module exit
 * (t33_mod_free), so a frame the core still finishes after stream-off never
 * writes into freed memory.  Clean (written back and invalidated) before it
 * goes to the hardware: no dirty line of an earlier user overwrites the
 * first statistics.
 */
void *t32_defog_dma_area(u32 size)
{
	if (size > T33_DEFOG_DMA_BYTES)	/* 8 KiB, four 2 KiB banks */
		return NULL;
	if (!t33_defog_pages)
		t33_defog_pages = t32_dma_zpages(T33_DEFOG_ORDER);
	if (!t33_defog_pages)
		return NULL;
	dma_cache_wback_inv(t33_defog_pages, PAGE_SIZE << T33_DEFOG_ORDER);
	return (void *)t33_defog_pages;
}

/* tx_isp_t32_defogd.c, before tisp_defog_deinit: DMA back to the static area (#850) */
void t32_defog_park(void)
{
	u32 pa;

	if (!t33_defog_pages || !t32_core_clk_active())
		return;
	pa = (u32)virt_to_phys((void *)t33_defog_pages);
	isp_wr(0xa210, pa);
	isp_wr(0xa214, pa + 2048);
	isp_wr(0xa218, pa + 4096);
	isp_wr(0xa21c, pa + 6144);
}
#endif

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
#ifndef TX_ISP_T33
	{
		u32 mw, mh;

		/* lib_tisp_init's last block: tisp_msca_init (openimp #730) */
		if (!t32_core_sensor_size(&mw, &mh))
			t32_isposd_msca_init(mw, mh);
	}
#endif
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
			/* stIspInitPar byte 0x2c = LDC on: tisp_rcfg_t33_write_all_reg starts list 25 */
			t33_st_isp_init_par_mode[11] = (t33_st_isp_init_par_mode[11] & ~0xffU) | (ln ? 1 : 0);
			t32_ldc_stream_on();
		}
#endif
#ifdef TX_ISP_T33
		t33_rcfg_failed = false;
#endif
		t33_rcfg_write_all();
		/* lib_tisp_msca_chx_reg_stream_off for the three channels */
		for (ch = 0; ch < 3; ch++)
			isp_wr(T32_MSCA_CH_EN, isp_rd(T32_MSCA_CH_EN) & ~(1U << ch));
	} else {
		t32_pf_start();
		t32_hw_rcfg_run_stream_on();	/* writes 0x2c = 2 (vendor order, #850) */
#ifndef TX_ISP_T33
		t32_dma_addr_guard();
		t32_defog_unwired_bypass();
#endif
	}
#ifdef TX_ISP_T33
	if (!t33_rcfg_failed)
#endif
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
#ifndef TX_ISP_T33
	ACCESS_ONCE(t32_vic_on) = true;	/* #915 */
#endif
	t32_csi_start();
	t32_hw_stats.stream_ons++;
#ifndef TX_ISP_T33
	memset(t32_irq_or, 0, sizeof(t32_irq_or));	/* #897: per stream-on */
	memset(t32_vic_or, 0, sizeof(t32_vic_or));
	memset(t32_vic_bits, 0, sizeof(t32_vic_bits));
#endif
}

void t32_hw_stream_off(void)
{
	t33_hw_flip_stop();	/* #779: the flip / bin-switch VIC hold ends with the stream */
#ifdef TX_ISP_T33
	t33_awbc_detach();
	t33_aec_detach();
#endif
#ifndef TX_ISP_T33
	/* #915: vendor order, the VIC interrupts masked before the VIC stops (no restart after it) */
	ACCESS_ONCE(t32_vic_on) = false;
	t32_vic_wr(0, T32_VIC_INT_MSK, 0xffffffff);
	t32_vic_wr(0, T32_VIC_INT_MSK2, 0xffffffff);
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

#ifdef TX_ISP_T33
/*
 * g15-3 (frame path, step 1): the buffer push is the vendor's tisp_msca_t33_addr_fifo_write (oracle block
 * msca-api6), stIspInitPar word 226 = 1: the Y / UV addresses straight into 0xf0130 / 0xf0150 + ch * 0x100
 * (the same two writes as before; the list-mode branch, mscaCfgMode slots of list 23, is not taken).
 */
#define tisp_reg_write_t33	t33_api6_reg_write
#define tisp_reg_read_t33	t33_api6_reg_read
static void t33_api6_reg_write(u32 id, u32 blk, u32 reg, u32 val);
static u32 t33_api6_reg_read(u32 id, u32 blk, u32 reg);
#include "tx_isp_t33_msca_api6.h"
#include "tx_isp_t33_mscapop.h"
#undef tisp_reg_write_t33
#undef tisp_reg_read_t33
static void t33_api6_reg_write(u32 id, u32 blk, u32 reg, u32 val)
{
	tisp_reg_write_t33(id, blk, reg, val);
}

static u32 t33_api6_reg_read(u32 id, u32 blk, u32 reg)
{
	return tisp_reg_read_t33(id, blk, reg);
}
#endif

void t32_hw_msca_push(int ch, u32 y, u32 uv)
{
#ifdef TX_ISP_T33
	t33_msca_addr_fifo_write(t33_st_isp_init_par_mode[226], 1, 0, (u32)ch, y, uv);
#else
	ch_wr(ch, T32_MSCA_Y_PUSH, y);
	ch_wr(ch, T32_MSCA_UV_PUSH, uv);
	/* the T32 vendor writes 0 to the clears after a push: only bit 0
	 * clears (T33 single sensor: the two addresses only) */
	if (T32_SOC_LIST_IRQ)
		return;
	ch_wr(ch, T32_MSCA_Y_CLR, 0);
	ch_wr(ch, T32_MSCA_UV_CLR, 0);
#endif
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
 * #658 b: a frame channel released after the last /dev/tx-isp close (streamer crash: the kernel closes the fds in
 * order, clocks off in t32_isp_release) must not touch the gated MSCA (lost writes or a bus hang, the rule of the
 * rmmod path) nor poll its BUSY bit (502 x msleep(1) = ~10 s per channel at HZ=100).  The software teardown (list
 * 23 words, stream flags) still runs; the next stream-on rebuilds the block.  T32 unchanged.
 */
static inline bool t32_msca_regs_live(void)
{
	return !T32_SOC_LIST_IRQ || t32_core_clk_active();
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

/*
 * g17-2: the vendor's channel start (ispcore_pad_event_handle 0x3000003, single sensor) on the proven ports in
 * tx_isp_t33_osd.c (t33_msca_chx_start): channel_attr_set with the S_FMT attribute of the stored format
 * (t32_fs_msca_attr), tisp_msca_t33_chx_cfg_load (scaling_algorithm, curve_calc, ch_curve_write,
 * ctrl_reg_write: sizes, steps, curves, 0xf0128, strides, chx_en, the list 23 trigger), chx_stream_on and
 * chx_en.  The direct (T32) way only without list 23 / the attribute block, or when the output the vendor
 * attribute describes is not the buffer's (w x h, stride = the one tisp_msca_t33_ctrl_reg_write writes:
 * 0x1000 for channel 0 while top 0x100 reads 5 (IVDC direct mode), else the 16-aligned width).  #680: in
 * direct mode channel 0 (stride 0x1000) went direct and took channel 1 with it; the next list 23 run (an ISP
 * OSD / mask / draw request) then wrote the list's idle channel words (0xf0040 = 0, channel 1 at the input
 * size) and channel 1 stopped.
 */
int t32_fs_msca_attr(int ch, u32 in_w, u32 in_h, u32 *a);	/* tx_isp_t32_fs.c */
int t33_msca_chx_start(u32 ch, const u32 *a);			/* tx_isp_t33_osd.c */

static bool t33_msca_list_start(int ch, u32 w, u32 h, u32 stride, u32 in_w, u32 in_h)
{
	u32 *l = (t33_mod_built & (1ULL << T33_MOD_MSCA)) ? t33_mod_buf(T33_MOD_MSCA) : NULL, a[13];
	int r;

	if (!l || t33_msca_direct || l[0] < 0x12d || !t32_fs_msca_attr(ch, in_w, in_h, a) || a[6] != w ||
	    a[7] != h || stride != t33_msca_stride((u32)ch, a[6], ch == 0 ? isp_rd(0x100) : 0) ||
	    (r = t33_msca_chx_start((u32)ch, a)) < 0) {
		t33_msca_direct |= 1U << ch;
		t33_msca_direct_starts++;
		return false;
	}
	t33_msca_listed |= 1U << ch;
	t33_msca_vendor_starts++;
	if (r)
		t33_msca_refresh = 1;	/* tispMscaRefresh (chx_stream_on) */
	return true;
}

/*
 * ispcore_frame_channel_streamoff (0xfc67c), in two halves around the vendor's work-state wait (#650, #651):
 * under the frame-channel lock lib_tisp_msca_chx_stream_off (tisp_msca_t33_api_chx_stream_off: the stream flag,
 * chx_en (ch, 0), tispMscaRefresh) and lib_tisp_msca_chx_reg_stream_off (the enable bit of 0xf0040, direct);
 * the lock is released, then (atlas_open == 0) up to 502 x private_msleep(1) while tisp_msca_work_state (0xf0054
 * bit work id = the MSCA output) is set, "CH%d Streamoff Timeout !!!" when it stays set, "CH%d Streamoff
 * Finish !!!" always; then tisp_msca_addr_fifo_clear_byindex (the Y / UV fifo and done clears) under the lock
 * again.  t32_hw_msca_stop_begin / _wait / _end; t32_hw_msca_stop (snapshot pause, rmmod) runs begin + end.
 */
static bool t33_msca_list_stop_begin(int ch)
{
	u32 *l = t33_mod_buf(T33_MOD_MSCA), b = 0x1e * (u32)ch;

	t33_msca_direct &= ~(1U << ch);
	if (!(t33_msca_listed & (1U << ch)) || !l)
		return false;
	t33_msca_listed &= ~(1U << ch);
	if (t33_msca_chx_stream_set((u32)ch, 0)) {
		T33_SW(l, b + 0x2a, 0);
		T33_SW(l, b + 0x32, 0);
		T33_SW(l, 3, l[3 + 1] & ~(1U << ch));
	}
	t33_msca_refresh = 1;	/* tispMscaRefresh (chx_stream_off): at the next frame end */
	if (t32_msca_regs_live())
		isp_wr(T32_MSCA_CH_EN, isp_rd(T32_MSCA_CH_EN) & ~(1U << ch));
	return true;
}

#else /* !TX_ISP_T33 */
/*
 * openimp #730: the vendor T32 channel start (ispcore_pad_event_handle 0x3000002 / 0x3000003 of
 * tx-isp-t32.ko: lib_tisp_msca_channel_attr_set, tisp_msca_channel_start -> tisp_msca_chx_cfg_load,
 * lib_tisp_msca_chx_stream_on, lib_tisp_api_set_msca_dma_stride) on the live MSCA state of
 * tx_isp_t32_isposd.c.  The direct start below only without that state (no section 25 in the bin) or when the
 * output the S_FMT attribute describes is not the buffer's.
 */
int t32_fs_msca_attr(int ch, u32 in_w, u32 in_h, u32 *a);	/* tx_isp_t32_fs.c */
static u32 t32_msca_listed;			/* channels started on the vendor path */
u32 t32_msca_vendor_starts, t32_msca_direct_starts;

static bool t32_msca_live_start(int ch, u32 w, u32 h, u32 stride, u32 in_w, u32 in_h)
{
	u32 a[13];

	if (!t32_fs_msca_attr(ch, in_w, in_h, a) || a[6] != w || a[7] != h ||
	    t32_isposd_msca_chx_start((u32)ch, a, stride)) {
		t32_msca_direct_starts++;
		return false;
	}
	t32_msca_listed |= 1U << ch;
	t32_msca_vendor_starts++;
	return true;
}

static bool t32_msca_live_stop_begin(int ch)
{
	if (!(t32_msca_listed & (1U << ch)))
		return false;
	t32_msca_listed &= ~(1U << ch);
	return !t32_isposd_msca_chx_stop((u32)ch);
}
#endif /* TX_ISP_T33 */

/* tisp_msca_addr_fifo_clear_byindex */
static void t32_msca_fifo_clear(int ch)
{
	ch_wr(ch, T32_MSCA_Y_CLR, 1);
	ch_wr(ch, T32_MSCA_UV_CLR, 1);
	ch_wr(ch, T32_MSCA_Y_DONE_CLR, 1);
	ch_wr(ch, T32_MSCA_UV_DONE_CLR, 1);
}

/*
 * The stream-off half under the frame-channel lock (T33: the list path, or the direct channel stop).  T32 and
 * T33 share the vendor order of ispcore_frame_channel_streamoff (T32 0xb5cd4, recovered C ported in
 * tx_isp_t32_ispsoff.h: stream off, unlock, up to 502 x msleep(1) on tisp_msca_work_state, fifo clear, #652).
 */
void t32_hw_msca_stop_begin(int ch)
{
#ifdef TX_ISP_T33
	if (t33_msca_list_stop_begin(ch))
		return;
#else
	if (t32_msca_live_stop_begin(ch))
		return;
#endif
	curve_geom[ch].on = false;
	if (!t32_msca_regs_live())
		return;
	isp_wr(T32_MSCA_CH_EN, isp_rd(T32_MSCA_CH_EN) & ~(1U << ch));
	isp_wr(T32_MSCA_UPDATE, 1);
}

#define T32_SOFF_WAIT_MS	502	/* x private_msleep(1) */
u32 t33_msca_soff_waits, t33_msca_soff_timeouts, t33_msca_soff_last_ms, t33_msca_soff_gated;

/*
 * The vendor's wait without the lock: process context only; returns 0, or -ETIMEDOUT after 502 sleeps.
 * tisp_msca_work_state(pipe, work) = bit "work" of 0xf0054; the work id (frame channel +120) is the MSCA
 * channel of the output: the same byte is the channel argument of tisp_msca_addr_fifo_write in
 * ispcore_pad_event_handle (0xb8b04: a1 = fc + 120).
 */
int t32_hw_msca_stop_wait(int ch)
{
	unsigned long t0 = jiffies;
	u32 n;

	for (n = T32_SOFF_WAIT_MS; n; n--) {
		if (!t32_msca_regs_live()) {
			t33_msca_soff_gated++;
			return 0;	/* gated (#658 b): no DMA runs, no register read */
		}
		if (!((isp_rd(T32_MSCA_BUSY) >> (ch & 31)) & 1))
			break;
		msleep(1);
	}
	t33_msca_soff_waits++;
	t33_msca_soff_last_ms = jiffies_to_msecs(jiffies - t0);
	if (!n) {
		t33_msca_soff_timeouts++;
		isp_printf(1, "CH%d Streamoff Timeout !!!\n", ch);
	}
	isp_printf(1, "CH%d Streamoff Finish !!!\n", ch);
	return n ? 0 : -ETIMEDOUT;
}

/* the half after the wait, under the lock again */
void t32_hw_msca_stop_end(int ch)
{
	if (t32_msca_regs_live())
		t32_msca_fifo_clear(ch);
}

void t32_hw_msca_start(int ch, u32 w, u32 h, u32 stride, u32 in_w, u32 in_h)
{
	u32 size = t32_msca_size_word(w, h);
	unsigned long fl;

#ifdef TX_ISP_T33
	if (t33_msca_list_start(ch, w, h, stride, in_w, in_h))
		return;
#else
	if (t32_msca_live_start(ch, w, h, stride, in_w, in_h))
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
#ifdef TX_ISP_T33
	ch_wr(ch, T32_MSCA_PARAM2, T32_MSCA_PARAM2_DEFAULT);
#else
	ch_wr(ch, T32_MSCA_PARAM2, in_w);	/* the MSCA input width (vendor tisp_msca_init / init_chx_cfg, #735) */
#endif
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
	t32_hw_msca_stop_begin(ch);
	t32_hw_msca_stop_end(ch);
}

/* pop one done entry: returns 0 and the addresses, or -1 when empty */
int t32_hw_msca_pop_done(int ch, u32 *y, u32 *uv)
{
#ifdef TX_ISP_T33
	/*
	 * g16-2 (#639): the vendor's tisp_msca_t33_addr_fifo_state (block mscapop) and
	 * tisp_msca_t33_addr_fifo_read (msca-api6), stIspInitPar word 226 = 1: the Y done word
	 * 0xf0138 + ch * 0x100 first, then UV 0xf0158, both & ~7 (the low flag bits of #393, e.g.
	 * Y done 0x03975001 for a buffer at 0x03975000).  Before: UV first, both & ~0xf.  The
	 * vendor's ispcore_channel_framedone_process reads one entry per channel done and never
	 * calls fifo_state (unreachable there, INVENTORY); this driver drains the FIFO while the
	 * state says an entry is there.
	 */
	if (!t33_msca_addr_fifo_state(0, (u32)ch))
		return -1;
	t33_msca_addr_fifo_read(t33_st_isp_init_par_mode[226], 1, 0, (u32)ch, y, uv);
#else
	if (ch_rd(ch, T32_MSCA_FIFO_STATE) & T32_MSCA_FIFO_EMPTY)
		return -1;
	*uv = ch_rd(ch, T32_MSCA_UV_DONE);	/* before the Y read pops */
	*y = ch_rd(ch, T32_MSCA_Y_DONE);
	/* vendor T32 tisp_msca_addr_fifo_read: both words & ~7 (oracle block t32-mscafifo) */
	*uv = T32_MSCA_DONE_ADDR(*uv);
	*y = T32_MSCA_DONE_ADDR(*y);
#endif
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
	rcfg_pages = t32_dma_zpages(get_order((T32_RCFG_TABLE_WORDS + T32_RCFG_LIST_WORDS) * 4));
	if (!rcfg_pages) {
		ret = -ENOMEM;
		goto err;
	}
	rcfg_pa = virt_to_phys((void *)rcfg_pages);
#ifdef TX_ISP_T33
	t33_rc_bufinfo[0] = (u32 *)CKSEG1ADDR(rcfg_pa);
	t33_rc_bufinfo[1] = (u32 *)(unsigned long)rcfg_pa;
#endif
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

/* rmmod: one frame still in flight inside the core (15 fps = 67 ms), with margin */
#define T32_HW_EXIT_DRAIN_MS	100

void t32_hw_exit(void)
{
	int i, regs;

#ifdef TX_ISP_T33
	t33_msca_attr_exit();
#endif
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
		/*
		 * #919 L6: the VIC is stopped, but a frame already inside the core still finishes and its
		 * defog / LCE / statistics DMA writes the buffers freed below (the vendor frees them only in
		 * lib_tisp_deinit after tisp_top_deinit, openimp #892).  Let that frame drain.
		 */
		msleep(T32_HW_EXIT_DRAIN_MS);
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
#ifndef TX_ISP_T33
	cancel_work_sync(&t33_flip_w);	/* it writes VIC registers and the sensor (#689) */
	t33_flip_pend = 0;
	t33_vic_hold = 0;
#endif
#ifdef TX_ISP_T33
	cancel_work_sync(&t33_stats_w);	/* it reads registers and the statistics buffers */
	cancel_work_sync(&t33_af_w);	/* it reads the AF statistics buffer */
	cancel_work_sync(&t33_flip_w);	/* it writes VIC registers and the sensor */
	t33_flip_pend = 0;
	t33_vic_hold = 0;
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
