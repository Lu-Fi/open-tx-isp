/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP register map (offsets from the ISP core base 0x13300000).
 *
 * Source: the S0 MMIO trace of the vendor stack in QEMU -M ingenic-t32
 * (writes r10, reads r11; audit/T32_S0_1025.md) and the QEMU model notes.
 * Generation: T41-family Tiziano core, T23-style MSCA at 0xf0000.
 */
#ifndef TX_ISP_T32_REGS_H
#define TX_ISP_T32_REGS_H

#define T32_ISP_PHYS			0x13300000U
#define T32_ISP_SIZE			0x00100000U
#define T32_CSI_HOST_PHYS		0x10054000U	/* "mipi-rx4l" */
#define T32_DPHY_PHYS			0x10022000U

/* IRQ lines (INTC numbers as /proc/interrupts shows them) */
#define T32_IRQ_ISP_M0			29
#define T32_IRQ_VIC			30	/* isp-w02 */
#define T32_IRQ_ISP_M1			38
#define T32_IRQ_IVDC			57

/* core */
#define T32_CORE_ID			0x00040	/* reads 0x00704100 */
#define T32_CORE_STATUS			0x00060
#define T32_CORE_IDLE_REQ		0x00200	/* bit 0: request idle */
#define T32_CORE_IDLE_ACK		0x00204	/* bit 0: idle */

/* register DMA / register lists (RCFG) */
#define T32_RDMA_TRIG			0x22014	/* bit 17 starts list 0 at stream-on */
#define T32_RDMA_TRIG2			0x2201c
#define T32_RDMA_TABLE			0x22020	/* pa of the list table */
#define T32_RDMA_IRQ_EN			0x22028
#define T32_RDMA_IRQ_CLR		0x2202c	/* written 0 in every ISR */
#define T32_RDMA_BUSY_MASK		0x00030000U

/* interrupt block */
#define T32_INT_EN			0x30020
#define T32_INT_CLR			0x30024
#define T32_INT_STA			0x30028	/* bit 0: frame end */
#define T32_CHDONE_EN			0x30030
#define T32_CHDONE_CLR			0x30034
#define T32_CHDONE_STA			0x30038	/* bit N: MSCA channel N done */
#define T32_INT2_EN			0x30060
#define T32_INT2_STA			0x30068
#define T32_MSCADONE_EN			0x300a0
#define T32_MSCADONE_CLR		0x300a4
#define T32_MSCADONE_STA		0x300a8	/* bit 1: MSCA frame done (OSD load point) */
#define T32_INT3_EN			0x300b0
#define T32_INT3_CLR			0x300b4
#define T32_INT3_STA			0x300b8
#define T32_INT4_EN			0x300d0
#define T32_INT4_CLR			0x300d4
#define T32_INT4_STA			0x300d8

/* statistics */
#define T32_AWB_BASE			0x16000
#define T32_AWB_STAT			0x1608c
#define T32_AE_BASE			0x17000
#define T32_AE_STAT			0x1708c

/* VIC: two instances, 0x1000 apart; the second idles with one sensor */
#define T32_VIC_BASE(n)			(0x80000 + (n) * 0x1000)
#define T32_VIC_CTRL			0x010	/* 4: reset, 1: run (level) */
#define T32_VIC_RESET_BUSY		0x024	/* (core 0x24) bits 3:0 clear after reset */
#define T32_VIC_INT_STA			0x1e0
#define T32_VIC_INT_STA2		0x1e4
#define T32_VIC_INT_MSK			0x1e8
#define T32_VIC_INT_MSK2		0x1ec
#define T32_VIC_INT_CLR			0x1f0
#define T32_VIC_INT_CLR2		0x1f4
#define T32_VIC_INT_CFG			0x200	/* 0x80000011 at stream-on */
#define T32_VIC_STATUS			0x20c

/* MSCA (multi-scaler): T23 channel packing at 0xf0000 */
#define T32_MSCA_BASE			0xf0000
#define T32_MSCA_UPDATE			0xf0010
#define T32_MSCA_CH_EN			0xf0040
#define T32_MSCA_CH_EN2			0xf004c
#define T32_MSCA_BUSY			0xf0054
#define T32_MSCA_CH(n)			(0xf0100 + (n) * 0x100)
#define T32_MSCA_SHADOW_OPEN		0xf8000	/* (value, offset) pair port */
#define T32_MSCA_SHADOW_DATA		0xf8004
#define T32_MSCA_SHADOW_COUNT		0xf8100	/* pairs << 4 | 1 after a block */
#define T32_MSCA_SHADOW_BEGIN		0x00000101U	/* to SHADOW_OPEN, before the pairs */
#define T32_MSCA_SHADOW_END		0x01010102U	/* to SHADOW_OPEN, after them */
/* polyphase curves (shadow port targets): channel n at 0xf0820 + n * 0xa0,
 * 13 words horizontal, then 13 words vertical at +0x50 */
#define T32_MSCA_CURVE(n)		(0xf0820 + (n) * 0xa0)
#define T32_MSCA_CURVE_V		0x50
/* the last vertical word of channel 2 also holds a field the vendor sets to
 * 0x5a (bits 22:16) with one more pair after the curves, not counted */
#define T32_MSCA_CURVE_TAIL		0xf09e0
#define T32_MSCA_CURVE_TAIL_FIELD	(0x5aU << 16)

#endif /* TX_ISP_T32_REGS_H */
