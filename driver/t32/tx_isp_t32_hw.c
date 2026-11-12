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
#include "tx_isp_t33_af.h"
#include "tx_isp_t33_bcsh.h"
#include "tx_isp_t33_tmo.h"
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

static void t32_bringup_ae_kick(void);

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
	if (sta & 1)
		t32_hw_stats.frame_ends++;
	if (sta & T32_INT_AWB)
		t32_hw_stats.awb_irqs++;
	if (sta & T32_INT_AE)
		t32_hw_stats.ae_irqs++;
	if (sta & T32_INT_AE_HIST) {
		t32_hw_stats.ae_hist_irqs++;
		t32_bringup_ae_kick();
	}
	if (done)
		t32_fs_done_irq(done);
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
/* AF statistics buffer (0x18108..0x18114, written by the core) */
static unsigned long t33_af_pages;
static u8 t33_af_sec[T33_AF_SEC_BYTES];
#define T33_AF_ORDER	get_order(T33_AF_DMA_BYTES)
/* TMO V table (0x70e0, read by the core; also written through 0x32260 / 0x32264) */
static unsigned long t33_tmo_pages;
#define T33_TMO_ORDER	get_order(T33_TMO_V_BYTES)

static void t33_mod_free(void)
{
	unsigned int i;

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
	if (t33_af_pages)
		free_pages(t33_af_pages, T33_AF_ORDER);
	t33_af_pages = 0;
	if (t33_tmo_pages)
		free_pages(t33_tmo_pages, T33_TMO_ORDER);
	t33_tmo_pages = 0;

	for (i = 0; i < T33_MODULES; i++) {
		if (t33_mod_page[i])
			free_page(t33_mod_page[i]);
		t33_mod_page[i] = 0;
	}
	t33_mod_built = 0;
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
		if (!(t33_mod_built & (1ULL << i)))
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
}

static unsigned int lsc_ct = 5000;
module_param(lsc_ct, uint, 0644);
MODULE_PARM_DESC(lsc_ct, "colour temperature for the LSC mesh at stream-on (vendor init: 5000)");
static u32 lsc_scratch[T32_LSC_WORDS];

/*
 * AE and AWB statistics engines (tx_isp_t32_stats.h), initialised from the IQ
 * bin as the vendor does at AddSensor; here at stream-on, when the sensor size
 * and the bin are known.  The buffers live as long as the module, and the
 * engines are switched off before they are freed (t32_hw_exit).
 */
#define T32_AE_BUF_ORDER	3	/* 32 KiB >= 0x6000 */
#define T32_AWB_BUF_ORDER	5	/* 128 KiB >= 0x1c000 */
static unsigned long ae_buf, awb_buf;

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
	n = t32_awb_stats_init(bin, virt_to_phys((void *)awb_buf), T32_AWB_INIT_EV, w);
	for (i = 0; i < n && i < T32_STATS_MAX_WRITES; i++)
		isp_wr(w[i].reg, w[i].val);
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
		t33_mod_set(T33_MOD_TOP, w, T33_TOP_LIST_WRITES);
	}
	return;
#endif
	off = t32_iq_section(bin, bytes, T32_IQ_SEC_TOP, &len);
	if (off && T32_IQ_SEC_OK(len, T32_TOP_BYTES)) {
		n = t32_top_init(bin + off, width, height, (u32)bayer, w);
		for (i = 0; i < n; i++)
			isp_wr(w[i].reg, w[i].val);
	}
}

/*
 * Remaining tisp init blocks of the vendor's AddSensor that are derived from
 * the IQ bin (S4 steps 5 and 6), in the vendor's order: DMSC, gamma, CDNS, GIB, DPC, YDNS, CCM, LCE, SDNS, YSP, MDNS, CLM.
 * Each block is one write list; a block whose section is missing or does
 * not have the expected size is skipped (the hardware keeps its reset state).
 */
#define T32_TISP_MAX_WRITES	T32_MDNS_MAX_WRITES
static struct t32_front_write *tisp_w;

/* LCE DMA buffers (work 0x26400 bytes, default curve 0x5a00 bytes), kept until the module is removed */
#define T32_LCE_WORK_ORDER	6	/* 256 KiB >= 0x26400 */
#define T32_LCE_CURVE_ORDER	3	/* 32 KiB >= 0x5a00 */
static unsigned long lce_work, lce_curve;

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
	if (!lce_work || !lce_curve)
		return -ENOMEM;
	t32_lce_curve(t32_uncached(lce_curve));			/* both halves hold the default curve */
	t32_lce_curve((u8 *)t32_uncached(lce_curve) + 0x2d00);
	wmb();
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
static u32 t33_tmo_v[T33_TMO_WORDS];
#define T33_INIT_EV	0xffffffffU	/* the vendor's EV before the first AE run */
#define T33_DG_UNITY	0x04000400U	/* DG list 0x4060 at init (DG 1.0), until #389 builds list 5 */

#define T33_MOD_VB(blk, BLK, v)								\
	do {										\
		u32 *buf_;								\
		off = t32_iq_section(bin, bytes, T33_SEC_##BLK, &len);			\
		if (off && len == T33_##BLK##_SEC_BYTES && (buf_ = t33_mod_buf(T33_MOD_##BLK)))	\
			t33_mod_done(T33_MOD_##BLK, t33_##blk##_init(bin + off, T33_INIT_GAIN, v,	\
								     buf_, T33_SLOT_WORDS));	\
	} while (0)

static void t33_tisp_init(const u8 *bin, u32 bytes)
{
	u32 off, len, code, width, height;

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
		u32 *buf, blc = 0;

		/* tisp_reg_read_t33(BLC list, slot 3): our BLC list word 4 (0x3044) */
		if ((t33_mod_built & (1ULL << T33_MOD_BLC)) && (buf = t33_mod_buf(T33_MOD_BLC)))
			blc = buf[4];
		off = t32_iq_section(bin, bytes, T33_SEC_RDNS, &len);
		if (off && len == T33_RDNS_SEC_BYTES && (buf = t33_mod_buf(T33_MOD_RDNS)))
			t33_mod_done(T33_MOD_RDNS, t33_rdns_init(bin + off, T33_INIT_GAIN, T33_INIT_EV, blc,
								 T33_DG_UNITY, t33_rdns_v, buf, T33_SLOT_WORDS));
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
		if (buf && t33_lce_curve_pages && t33_lce_stat_pages) {
			t33_lce_curve_default((u8 *)CKSEG1ADDR(virt_to_phys((void *)t33_lce_curve_pages)));
			t33_mod_done(T33_MOD_LCE,
				     t33_lce_init(t33_lce_sec, width, height, 0, 0,
						  (u32)virt_to_phys((void *)t33_lce_curve_pages),
						  (u32)virt_to_phys((void *)t33_lce_stat_pages), buf, T33_SLOT_WORDS));
		}
	}
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
			kfree(cp);
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
	if (off && T32_IQ_SEC_OK(len, T32_CDNS_SEC_BYTES))
		t32_tisp_flush("cdns", t32_cdns_init(bin + off, tisp_w));
#endif
	off = t32_iq_section(bin, bytes, T32_SEC_GIB, &len);
	if (off && T32_IQ_SEC_OK(len, T32_GIB_SEC_BYTES) && !t32_core_sensor_mbus(&code) && t32_mbus_to_bayer(code) >= 0) {
		u8 *cp = kmemdup(bin + off, len, GFP_KERNEL);	/* the block stores into its section */

		if (cp) {
			t32_tisp_flush("gib", t32_gib_init(cp, (u32)t32_mbus_to_bayer(code), tisp_w));
			kfree(cp);
		}
	}
	off = t32_iq_section(bin, bytes, T32_SEC_DPC, &len);
	if (off && T32_IQ_SEC_OK(len, T32_DPC_SEC_BYTES))
		t32_tisp_flush("dpc", t32_dpc_init(bin + off, 0, tisp_w));	/* wide: ISP reg 0x88 & 0x1f > 4, never with this front end */
	off = t32_iq_section(bin, bytes, T32_SEC_YDNS, &len);
	if (off && T32_IQ_SEC_OK(len, T32_YDNS_SEC_BYTES))
		t32_tisp_flush("ydns", t32_ydns_init(bin + off, tisp_w));
	off = t32_iq_section(bin, bytes, T32_SEC_CCM, &len);
	if (off && T32_IQ_SEC_OK(len, T32_CCM_SEC_BYTES)) {
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

		if (cp && coff && clen >= T32_BCSH_CSC_BYTES)
			t32_tisp_flush("bcsh", t32_bcsh_init(cp, bin + off + len, bytes - off - len, bin + coff, tisp_w));
		kfree(cp);
	}
	off = t32_iq_section(bin, bytes, T32_SEC_LCE, &len);
	if (off && T32_IQ_SEC_OK(len, T32_LCE_SEC_BYTES) && !t32_core_sensor_size(&width, &height))
		t32_tisp_flush("lce", t32_lce_build(bin + off, width, height));
	off = t32_iq_section(bin, bytes, T32_SEC_SDNS, &len);
	if (off && T32_IQ_SEC_OK(len, T32_SDNS_SEC_BYTES))
		t32_tisp_flush("sdns", t32_sdns_init(bin + off, tisp_w));
	off = t32_iq_section(bin, bytes, T32_SEC_YSP, &len);
	if (off && T32_IQ_SEC_OK(len, T32_YSP_SEC_BYTES)) {
		u8 *cp = kmemdup(bin + off, len, GFP_KERNEL);

		if (cp) {
			t32_tisp_flush("ysp", t32_ysp_init(cp, tisp_w));
			kfree(cp);
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
	}
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
	u32 bytes, w, h;

	if (t32_core_iq_bin(&bin, &bytes) || t32_core_sensor_size(&w, &h))
		return;
	p = t32_lsc_block(bin, bytes);
	if (!p || t32_lsc_setup(p, &s, w, h, lsc_ct)) {
		isp_printf(1, "tx-isp-t32: no usable LSC block in the IQ bin, LSC off\n");
		return;
	}
	isp_wr(T32_LSC_ID_REG, T32_LSC_ID);
	if (t32_lsc_fill_list(p, &s, l, lsc_scratch) == 0)
		isp_printf(0, "tx-isp-t32: LSC %ux%u cells %u table %u/%u at %u K\n",
			   s.cols, s.rows, s.cells, s.table_lo, s.table_hi, lsc_ct);
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
	/* MSCA input size and channel defaults */
	isp_wr(0xf0404, size);
	isp_wr(0xf0090, 0);
	isp_wr(0xf0094, size);
	isp_wr(T32_MSCA_CH_EN2, 0);
	isp_wr(T32_MSCA_CH_EN, 0);
	isp_wr(T32_MSCA_UPDATE, 1);
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
	t32_stats_init();
	t32_tisp_init();
	if (T32_SOC_LIST_IRQ) {
		t33_rcfg_write_all();
	} else {
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
	/* the vendor writes 0 to the clears after a push: only bit 0 clears */
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
void t32_hw_msca_start(int ch, u32 w, u32 h, u32 stride, u32 in_w, u32 in_h)
{
	u32 size = t32_msca_size_word(w, h);

	curve_geom[ch].in_w = in_w;
	curve_geom[ch].in_h = in_h;
	curve_geom[ch].w = w;
	curve_geom[ch].h = h;
	curve_geom[ch].on = true;
	ch_wr(ch, T32_MSCA_Y_STRIDE, stride);
	ch_wr(ch, T32_MSCA_UV_STRIDE, stride);
	isp_wr(T32_MSCA_CH_EN2, 0);
	isp_wr(T32_MSCA_UPDATE, 1);
	t32_hw_msca_load_curves();
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
		t32_stats_off();	/* no statistics DMA into the buffers freed below */
	}
	if (irqs_requested) {
		free_irq(vic_irq, &csi_base);
		free_irq(isp_irq, &isp_base);
		irqs_requested = 0;
	}
	cancel_work_sync(&t32_bringup_ae_w);	/* it reads registers and the AE buffer */
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
	lce_work = lce_curve = 0;
	kfree(tisp_w);
	tisp_w = NULL;
	t33_mod_free();
	free_pages(rcfg_pages, get_order((T32_RCFG_TABLE_WORDS + T32_RCFG_LIST_WORDS) * 4));
	rcfg_pages = 0;
}
