/* SPDX-License-Identifier: GPL-2.0 */
/*
 * binary oracle, block "t32-ldcdma": tisp_func_ldc_dma_cfg / tisp_func_ldc_dma_stride of the vendor
 * tx-isp-t32.ko (T32_OEM_KO) vs driver/t32/tx_isp_t32_dma.h (openimp #751).  The LDC memory record is
 * the vendor's stMemInfo (buffer +0x38, total +0x48).  Every register write goes through system_reg_write
 * (recorded calls, in order; the last one of each function is a tail call, 0x20 with the DMA start bit and 0x21058),
 * the 0x20 read through system_reg_read.
 */
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;	/* tx_isp_t32_dma.h types only u32 on the host */
#include "tx_isp_t32_dma.h"

u8 bo_mem[76];
extern void system_reg_write(u32 reg, u32 val);
extern u32 system_reg_read(u32 reg);

static u32 bo_wr(u32 reg, u32 val)
{
	system_reg_write(reg, val);
	return 0;
}

static u32 bo_rd(u32 reg)
{
	return system_reg_read(reg);
}

static const struct t32_dma_ops bo_ops = { .reg_write = bo_wr, .reg_read = bo_rd };

u32 bo_tisp_func_ldc_dma_cfg(u32 w, u32 h, u32 unused, u32 stride)
{
	struct t32_dma_dev d = { .ops = &bo_ops, .ldc0 = (const struct t32_dma_ldc_rec *)bo_mem };

	t32_func_ldc_dma_cfg(&d, w, h, unused, stride);
	return 0;
}

u32 bo_tisp_func_ldc_dma_stride(u32 stride)
{
	struct t32_dma_dev d = { .ops = &bo_ops, .ldc0 = (const struct t32_dma_ldc_rec *)bo_mem };

	t32_func_ldc_dma_stride(&d, stride);
	return 0;
}
