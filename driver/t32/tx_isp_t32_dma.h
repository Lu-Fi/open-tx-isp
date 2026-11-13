/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core resets and the DMA test/LDC paths: readable C of the vendor's
 * tisp_func_isp_reset, tisp_dma_init, tisp_func_ldc_dma_cfg,
 * tisp_func_ldc_dma_stride, tisp_dma_read_file and tisp_dma_rdma_isp_in_cfg.
 *
 * Registers (core base 0): 0x200 / 0x204 core reset request / done (bit 0),
 * 0x24 (4 after reset), 0x1040 (1), 0x20 (bit 31 = DMA start); WDMA block
 * 0x20054 .. 0x20064 and RDMA block 0x21048 .. 0x21068.
 *
 * The OS calls (register access, IRQ table, kmalloc, file I/O, printf) are
 * hooks (struct t32_dma_ops).  The vendor calls system_reg_write directly
 * (no shadow-register path), so t32_data_d03a0 does not matter here.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_dma_oracle).
 *
 * Vendor behaviour kept:
 * - tisp_func_isp_reset polls 0x204 bit 0 without a time limit.
 * - tisp_func_ldc_dma_cfg ignores its third argument: the WDMA / RDMA
 *   addresses always come from the LDC memory record of channel 0
 *   (t32_data_dc074 +56 buffer, +72 total size).
 * - tisp_dma_read_file does not check the filp_open result (an error
 *   pointer is used as a file); the file is always /tmp/dmaInData.
 * - tisp_dma_rdma_isp_in_cfg allocates width * height * 2 bytes but reads
 *   up to 0x3f4800 bytes (1920 x 1080 x 2) into it; the buffer is never
 *   freed.  Its first argument is not used.
 */
#ifndef TX_ISP_T32_DMA_H
#define TX_ISP_T32_DMA_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <stddef.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_DMA_REG_RESET	0x200
#define T32_DMA_REG_RESET_DONE	0x204
#define T32_DMA_REG_CORE_24	0x24
#define T32_DMA_REG_CORE_1040	0x1040
#define T32_DMA_REG_START	0x20
#define T32_DMA_START		0x80000000u

#define T32_WDMA(off)		(0x20000u + (off))
#define T32_RDMA(off)		(0x21000u + (off))

#define T32_DMA_IRQ_WDMA	68	/* tisp_wdma_fra_down_interrupt */
#define T32_DMA_IRQ_RDMA	69	/* tisp_rdma_fra_down_interrupt */

#define T32_DMA_FILE		"/tmp/dmaInData"
#define T32_DMA_FILE_FLAGS	10
#define T32_DMA_FILE_MODE	0644
#define T32_DMA_PATH_LEN	128
#define T32_DMA_FILE_BYTES	0x3f4800u	/* 1920 x 1080 x 2 */
#define T32_DMA_GFP		0x24000c0u
#define T32_DMA_KSEG0		0x80000000u	/* kseg0 virtual - physical */
#define T32_DMA_FILE_POS	48		/* struct file f_pos */

struct t32_dma_ops {
	u32 (*reg_write)(u32 reg, u32 val);
	u32 (*reg_read)(u32 reg);
	void (*irq_func_set)(u32 ch, u32 irq, void *handler);
	void *(*kmalloc)(u32 size, u32 flags);
	u32 (*printf)(u32 level, const char *msg);
	void *(*filp_open)(const char *path, u32 flags, u32 mode);
	u32 (*get_fs)(void);
	void (*set_fs)(u32 fs);
	void (*vfs_read)(void *file, void *buf, u32 len, void *pos);
	u32 (*filp_close)(void *file, u32 id);
};

/* the part of the LDC memory record (struct t32_ldc_mem, t32_data_dc074) used here */
struct t32_dma_ldc_rec {
	u8 rsvd[56];
	u32 buf;		/* +56 */
	u32 rsvd1[3];
	u32 total;		/* +72 */
};

struct t32_dma_dev {
	const struct t32_dma_ops *ops;
	void *wdma_irq, *rdma_irq;		/* tisp_{w,r}dma_fra_down_interrupt */
	const struct t32_dma_ldc_rec *ldc0;	/* t32_data_dc074, channel 0 */
	u32 cfg[2];				/* t32_data_e3140 */
	u32 rdma_phys;				/* t32_data_e3154 */
};

/* tisp_func_isp_reset */
static inline u32 t32_func_isp_reset(struct t32_dma_dev *dev)
{
	const struct t32_dma_ops *o = dev->ops;

	o->reg_write(T32_DMA_REG_RESET, 1);
	while (!(o->reg_read(T32_DMA_REG_RESET_DONE) & 1))
		;
	o->reg_write(T32_DMA_REG_CORE_24, 4);
	return o->reg_write(T32_DMA_REG_CORE_1040, 1);
}

/* tisp_dma_init: keep the two config words, hook the WDMA / RDMA frame-done IRQs */
static inline u32 t32_dma_init(struct t32_dma_dev *dev, u32 ch, const u32 *cfg)
{
	ch &= 0xff;
	dev->cfg[0] = cfg[0];
	dev->cfg[1] = cfg[1];
	dev->ops->irq_func_set(ch, T32_DMA_IRQ_WDMA, dev->wdma_irq);
	dev->ops->irq_func_set(ch, T32_DMA_IRQ_RDMA, dev->rdma_irq);
	return 0;
}

/* tisp_func_ldc_dma_cfg: WDMA writes the LDC buffer, RDMA reads it back */
static inline u32 t32_func_ldc_dma_cfg(struct t32_dma_dev *dev, u32 width, u32 height,
				       u32 unused, u32 stride)
{
	const struct t32_dma_ops *o = dev->ops;
	u32 size = (width & 0xffff) * (height & 0xffff);
	u32 buf = dev->ldc0->buf;
	u32 rbuf = buf + dev->ldc0->total;

	(void)unused;
	stride &= 0xffff;
	o->reg_write(T32_WDMA(0x64), 1);
	o->reg_write(T32_WDMA(0x54), 0x40034);
	o->reg_write(T32_WDMA(0x60), stride << 16 | stride);
	o->reg_write(T32_WDMA(0x58), buf);
	o->reg_write(T32_WDMA(0x5c), buf + size);
	o->reg_write(T32_RDMA(0x68), 1);
	o->reg_write(T32_RDMA(0x64), 53);
	o->reg_write(T32_RDMA(0x48), 4);
	o->reg_write(T32_RDMA(0x54), stride);
	o->reg_write(T32_RDMA(0x58), stride);
	o->reg_write(T32_RDMA(0x4c), rbuf);
	o->reg_write(T32_RDMA(0x50), rbuf + size);
	return o->reg_write(T32_DMA_REG_START, o->reg_read(T32_DMA_REG_START) | T32_DMA_START);
}

/* tisp_func_ldc_dma_stride */
static inline u32 t32_func_ldc_dma_stride(struct t32_dma_dev *dev, u32 stride)
{
	const struct t32_dma_ops *o = dev->ops;

	stride &= 0xffff;
	o->reg_write(T32_WDMA(0x60), stride << 16 | stride);
	o->reg_write(T32_RDMA(0x54), stride);
	return o->reg_write(T32_RDMA(0x58), stride);
}

/* tisp_dma_read_file: read len bytes of /tmp/dmaInData into buf (kernel fs segment) */
static inline u32 t32_dma_read_file(struct t32_dma_dev *dev, void *buf, u32 len)
{
	const struct t32_dma_ops *o = dev->ops;
	char path[T32_DMA_PATH_LEN] = T32_DMA_FILE;
	void *file;
	u32 old_fs;

	file = o->filp_open(path, T32_DMA_FILE_FLAGS, T32_DMA_FILE_MODE);
	old_fs = o->get_fs();
	o->set_fs(0);
	o->vfs_read(file, buf, len, (char *)file + T32_DMA_FILE_POS);
	o->set_fs(old_fs);
	return o->filp_close(file, 0);
}

/* tisp_dma_rdma_isp_in_cfg: load a test frame from the file and feed it to the ISP input by RDMA */
static inline u32 t32_dma_rdma_isp_in_cfg(struct t32_dma_dev *dev, u32 unused, u32 width, u32 height)
{
	const struct t32_dma_ops *o = dev->ops;
	void *buf;

	(void)unused;
	width &= 0xffff;
	buf = o->kmalloc((width * (height & 0xffff)) << 1, T32_DMA_GFP);
	if (!buf)
		return o->printf(1, "not enough mem space for vic dma~~~\n");
	dev->rdma_phys = (u32)(uintptr_t)buf + T32_DMA_KSEG0;
	t32_dma_read_file(dev, buf, T32_DMA_FILE_BYTES);
	o->reg_write(T32_RDMA(0x64), 16);
	o->reg_write(T32_RDMA(0x48), 1);
	o->reg_write(T32_RDMA(0x54), width << 1);
	return o->reg_write(T32_RDMA(0x4c), dev->rdma_phys);
}

#endif
