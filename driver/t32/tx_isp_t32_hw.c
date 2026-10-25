// SPDX-License-Identifier: GPL-2.0
/*
 * T32 ISP hardware access for S2: VIC + MIPI CSI start/stop, the interrupt
 * block, MSCA channel programming and the frame-done path.
 *
 * Register values and their order follow the vendor stack's QEMU trace
 * (S0 runs r10/r11, audit/T32_S0_1025.md): VIC input configuration at
 * S_INPUT, IRQ enables + VIC run + CSI/D-PHY at STREAMON, MSCA channel setup
 * and buffer pushes at FRAME_STREAMON/QBUF, done-FIFO drain in the ISP
 * interrupt.  The VIC/CSI words are the ones the vendor wrote for the cv8001
 * RAW10 MIPI mode; deriving them from the sensor attributes is S3 work
 * (no tisp / no image pipeline programming yet).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_regs.h"
#include "tx_isp_t32_msca.h"

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
	u32 sta, done, msca, s3, s4;

	sta = isp_rd(T32_INT_STA);
	done = isp_rd(T32_CHDONE_STA);
	msca = isp_rd(T32_MSCADONE_STA);
	s3 = isp_rd(T32_INT3_STA);
	s4 = isp_rd(T32_INT4_STA);
	if (!(sta | done | msca | s3 | s4))
		return IRQ_NONE;
	isp_wr(T32_INT_CLR, sta);
	isp_wr(T32_CHDONE_CLR, done);
	isp_wr(T32_MSCADONE_CLR, msca);
	isp_wr(T32_INT3_CLR, s3);
	isp_wr(T32_INT4_CLR, s4);
	isp_wr(T32_RDMA_IRQ_CLR, 0);
	t32_hw_stats.isp_irqs++;
	if (sta & 1)
		t32_hw_stats.frame_ends++;
	if (done)
		t32_fs_done_irq(done);
	return IRQ_HANDLED;
}

static irqreturn_t t32_vic_irq(int irq, void *dev)
{
	u32 s0 = readl(isp_base + T32_VIC_BASE(0) + T32_VIC_INT_STA);
	u32 s1 = readl(isp_base + T32_VIC_BASE(0) + T32_VIC_INT_STA2);

	if (!(s0 | s1))
		return IRQ_NONE;
	writel(s0, isp_base + T32_VIC_BASE(0) + T32_VIC_INT_CLR);
	writel(s1, isp_base + T32_VIC_BASE(0) + T32_VIC_INT_CLR2);
	t32_hw_stats.vic_irqs++;
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/* VIC and CSI                                                        */

static void t32_vic_wr(int n, u32 off, u32 v)
{
	writel(v, isp_base + T32_VIC_BASE(n) + off);
}

/* VIC input configuration, written by the vendor at S_INPUT (tisp init) */
void t32_hw_input_setup(u32 w, u32 h)
{
	u32 size = (w << 16) | h;

	isp_wr(T32_CORE_IDLE_REQ, 1);
	isp_wr(0x0024, 0x40000001);
	isp_wr(0x0070, size);
	isp_wr(0x0080, size);
	isp_wr(0x0078, 0x100);
	isp_wr(0x0088, 0x100);
	t32_vic_wr(0, 0x1a8, 0x4444);
	t32_vic_wr(1, 0x1a8, 0x4444);
	t32_vic_wr(0, T32_VIC_INT_CFG, 0);
	t32_vic_wr(1, T32_VIC_INT_CFG, 0);
	t32_vic_wr(0, 0x018, 0);
	t32_vic_wr(1, 0x018, 0);
	t32_vic_wr(0, T32_VIC_CTRL, 4);		/* reset */
	t32_vic_wr(1, T32_VIC_CTRL, 4);
	if (t32_wait_clear(T32_VIC_RESET_BUSY, 0xf, 1000))
		isp_printf(2, "tx-isp-t32: VIC reset did not finish\n");
	t32_vic_wr(0, 0x1a4, 0x000a000a);
	t32_vic_wr(0, 0x100, 0x4b0);		/* MIPI input, RAW10 (cv8001) */
	t32_vic_wr(0, 0x01c, 2);
	t32_vic_wr(0, 0x024, 1);
	t32_vic_wr(0, 0x014, size);
	t32_vic_wr(0, 0x10c, 0x2b000);
	t32_vic_wr(0, 0x110, w << 16);
	t32_vic_wr(0, 0x114, 0);
	t32_vic_wr(0, 0x118, 0);
	t32_vic_wr(0, 0x11c, 0);
	t32_vic_wr(0, T32_VIC_INT_CFG, 0x80000011);
	t32_vic_wr(0, 0x204, 0);
	t32_vic_wr(0, 0x1ac, 0x20000);
	t32_vic_wr(0, 0x1a8, 0x4440);
	t32_vic_wr(0, 0x1b0, 0x10);
	t32_vic_wr(0, 0x1a0, 0);
	t32_vic_wr(0, 0x104, 0);
	t32_vic_wr(0, 0x108, 0);
	/* MSCA input size and channel defaults */
	isp_wr(0xf0404, size);
	isp_wr(0xf0090, 0);
	isp_wr(0xf0094, size);
	isp_wr(T32_MSCA_CH_EN2, 0);
	isp_wr(T32_MSCA_CH_EN, 0);
	isp_wr(T32_MSCA_UPDATE, 1);
	isp_wr(T32_CORE_IDLE_REQ, 0);
}

static void t32_csi_start(void)
{
	static const u32 lanes[] = { 0x128, 0x1a8, 0x228, 0x2a8, 0x328, 0x3a8 };
	static const u32 lanes2[] = { 0x160, 0x1e0, 0x260, 0x2e0, 0x360 };
	int i;

	writel(0, csi_base + 0x0c);
	writel(1, csi_base + 0x0c);
	writel(1, csi_base + 0x04);		/* lanes - 1 (2 lanes) */
	writel(3, csi_base + 0x100);
	writel(0, csi_base + 0x08);
	writel(0, csi_base + 0x10);
	writel(1, csi_base + 0x10);
	writel(0xffffffff, csi_base + 0x28);
	writel(0xffffffff, csi_base + 0x2c);
	writel(0x7d, dphy_base + 0x000);
	writel(0, csi_base + 0x80);
	writel(0x1f, dphy_base + 0x080);
	for (i = 0; i < ARRAY_SIZE(lanes); i++)
		writel(0x3f, dphy_base + lanes[i]);
	for (i = 0; i < ARRAY_SIZE(lanes2); i++)
		writel(0x19, dphy_base + lanes2[i]);
}

static void t32_csi_stop(void)
{
	writel(0, csi_base + 0x10);
	writel(0, csi_base + 0x0c);
}

/* STREAMON: interrupts on, VIC running, CSI up */
void t32_hw_stream_on(void)
{
	isp_wr(T32_INT_EN, 0xffffffff);
	isp_wr(T32_CHDONE_EN, 0xffffffff);
	isp_wr(T32_INT2_EN, 0xffffffff);
	isp_wr(T32_MSCADONE_EN, 0xffffffff);
	isp_wr(T32_INT3_EN, 0xffffffff);
	isp_wr(T32_INT4_EN, 0xffffffff);
	t32_vic_wr(0, T32_VIC_INT_MSK, 0);
	t32_vic_wr(0, T32_VIC_INT_MSK2, 0);
	t32_vic_wr(0, T32_VIC_CTRL, 1);
	t32_csi_start();
	t32_hw_stats.stream_ons++;
}

void t32_hw_stream_off(void)
{
	t32_vic_wr(0, T32_VIC_CTRL, 4);		/* stop + reset */
	t32_vic_wr(1, T32_VIC_CTRL, 4);
	t32_wait_clear(T32_VIC_RESET_BUSY, 0xf, 1000);
	t32_csi_stop();
	isp_wr(T32_INT_EN, 0);
	isp_wr(T32_CHDONE_EN, 0);
	isp_wr(T32_INT2_EN, 0);
	isp_wr(T32_MSCADONE_EN, 0);
	isp_wr(T32_INT3_EN, 0);
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

void t32_hw_msca_start(int ch, u32 w, u32 h, u32 stride)
{
	u32 size = t32_msca_size_word(w, h);

	ch_wr(ch, T32_MSCA_Y_STRIDE, stride);
	ch_wr(ch, T32_MSCA_UV_STRIDE, stride);
	ch_wr(ch, 0x00, 0);
	ch_wr(ch, T32_MSCA_SCL_SIZE, size);
	ch_wr(ch, T32_MSCA_CROP_POS, 0);
	ch_wr(ch, T32_MSCA_CROP_SIZE, size);
	isp_wr(T32_MSCA_CH_EN, isp_rd(T32_MSCA_CH_EN) | (1U << ch));
	isp_wr(T32_MSCA_UPDATE, 1);
}

void t32_hw_msca_stop(int ch)
{
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
	/* nothing latched from an earlier load may fire before we are ready */
	isp_wr(T32_INT_EN, 0);
	isp_wr(T32_CHDONE_EN, 0);
	isp_wr(T32_MSCADONE_EN, 0);
	isp_wr(T32_INT3_EN, 0);
	isp_wr(T32_INT4_EN, 0);
	t32_vic_wr(0, T32_VIC_INT_MSK, 0xffffffff);
	t32_vic_wr(0, T32_VIC_INT_MSK2, 0xffffffff);
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
	if (!isp_base)
		return;
	t32_hw_stream_off();
	t32_hw_msca_stop(0);
	if (irqs_requested) {
		free_irq(vic_irq, &csi_base);
		free_irq(isp_irq, &isp_base);
		irqs_requested = 0;
	}
	iounmap(dphy_base);
	iounmap(csi_base);
	iounmap(isp_base);
	isp_base = csi_base = dphy_base = NULL;
}
