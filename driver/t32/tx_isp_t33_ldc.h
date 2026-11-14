/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 online LDC at ISP init (openimp #491): pure helpers for the vendor
 * tx-isp-t33.ko 2.0.2.1 flow (Ghidra; no code copied; analysis
 * audit/issue491/fable/REPORT.md):
 *   tisp_online_ldc_set_attr 0x104438 / tisp_ldc_cfg_load 0x1040c4  -> attribute + list 25 values
 *   tisp_rcfg_ldc_reg_init 0x103af0                                  -> list 25 layout (not ascending)
 *   lib_tisp_ldc_get_mem_size 0x103d38                               -> LDC part of the IPC memory
 *   lib_tisp_set_isp_ipc_memory_space 0x38848, tisp_ldc_cfg_dma 0x103e7c,
 *   tisp_func_ldc_t33_dma_cfg 0xee600                                -> writes at SET_ISP_IPC_MEM
 * Checked against the 50 writes of a vendor QEMU run by tests/tx_isp_t33_ldc_test.c.
 */
#ifndef TX_ISP_T33_LDC_H
#define TX_ISP_T33_LDC_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T33_LDC_HOST_FW
#define T33_LDC_HOST_FW
struct t32_front_write {
	u32 reg, val;
};
#endif
#else
#include "tx_isp_t32_front.h"
#endif

#define T33_MOD_LDC		25
#define T33_LDC_LIST_WRITES	10
#define T33_LDC_IPC_WRITES	28	/* 4 IPC words + 24 LDC / WDMA / RDMA */

/* the stLDCAttr record of one input */
struct t33_ldc_attr {
	u32 type;		/* 0 LDC, 1 HLDC, 2 I2D */
	u32 angle;		/* I2D */
	u32 size;		/* input w | h << 16 */
	u32 out;		/* output w | h << 16 */
	u32 block;
	u32 stride;		/* LUT row stride */
	u32 lut;		/* LUT physical address (past its header) */
};

/* tx_ldc_online_init + tisp_online_ldc_set_attr: the 48-byte 0xc00454ba block {vinum, func, block, .., angle, size, .., lut, lut offset} */
static inline int t33_ldc_attr_from_block(const u32 *u, struct t33_ldc_attr *a)
{
	switch (u[1]) {
	case 1:
		a->type = 2;
		a->angle = u[4];
		break;
	case 2:
		a->type = 1;
		break;
	case 4:
		a->type = 0;
		break;
	default:
		return -1;
	}
	if (a->type != 2) {
		a->block = u[2];
		a->lut = u[7] + u[8];
		a->angle = 0;
	}
	a->size = u[5];
	a->out = a->size;
	if (a->type == 2 && (a->angle & ~2U) == 1)
		a->out = (a->size & 0xffff) << 16 | a->size >> 16;
	a->stride = a->type != 2 && a->block ?
		(((a->size & 0xffff) + a->block - 1) / a->block * 4 + 0xb) & ~7U : 0;
	return 0;
}

/* tisp_ldc_cfg_load: list 25 values (the template gives the unwritten words 0) */
static inline unsigned int t33_ldc_list(const struct t33_ldc_attr *a, struct t32_front_write *w)
{
	unsigned int n = 0;
	u32 code;

#define T33_LW(r, v) do { w[n].reg = (r); w[n].val = (v); n++; } while (0)
	T33_LW(0x27044, a->size);
	T33_LW(0x27048, a->out);
	T33_LW(0x27040, a->angle << 16 | a->type);
	if (a->type != 2) {
		T33_LW(0x2706c, a->lut);
		T33_LW(0x27070, a->stride);
		code = a->block == 8 ? 0 : a->block == 16 ? 1 : a->block == 32 ? 2 : a->block == 64 ? 3 : 4;
		if (code < 4)
			T33_LW(0x27074, code);
	}
	if (a->type == 2 && (a->angle & ~2U) == 1)
		T33_LW(0x21084, a->out | 0x10000000);
	else
		T33_LW(0x21084, a->out);
	T33_LW(0x27020, 0xffffffff);
	T33_LW(0x27024, 1);
#undef T33_LW
	return n;
}

/* lib_tisp_ldc_get_mem_size: { total (IPC part), first area } of a w x h input, single sensor */
static inline u32 t33_ldc_mem_size(const struct t33_ldc_attr *a, u32 w, u32 h, u32 *area_a)
{
	u32 wh = w * h, q = (wh + 7) >> 3;

	if (a->type == 2) {
		*area_a = q * 0x48 >> 2;
		return (w * 16 + w * 8) * 8 + (q * 0x18 >> 1) + ((((wh >> 1) + 7) >> 3) * 0x18 >> 1);
	}
	*area_a = q * 0x18 >> 2;
	return (w * 16 + w * 8 + (((wh >> 1) + 7) >> 3) + (((wh >> 2) + 7) >> 3)) * 8;
}

/* SET_ISP_IPC_MEM: IPC words, tisp_ldc_cfg_dma and tisp_func_ldc_t33_dma_cfg for IPC base b */
static inline unsigned int t33_ldc_ipc_writes(const struct t33_ldc_attr *a, u32 b, u32 w, u32 h,
					      struct t32_front_write *o)
{
	u32 area_a, b2, ow = a->out & 0xffff, v = ((h >> 1) + 1) & ~1U, ob = (ow + 7) >> 3;
	unsigned int n = 0;

	t33_ldc_mem_size(a, w, h, &area_a);
	b2 = b + area_a;
#define T33_OW(r, x) do { o[n].reg = (r); o[n].val = (x); n++; } while (0)
	T33_OW(0x01094, b);
	T33_OW(0x01098, 0x200000);
	T33_OW(0x010c4, 0);
	T33_OW(0x010c8, 0);
	T33_OW(0x2704c, b);
	T33_OW(0x27050, w);
	T33_OW(0x27054, b2);
	T33_OW(0x27058, ow);
	T33_OW(0x2705c, b + w * h / 2);
	T33_OW(0x27098, v << 15 | v);
	T33_OW(0x2709c, 0x400080);
	T33_OW(0x27060, w);
	T33_OW(0x27064, b2 + ow * 0x80);
	T33_OW(0x27068, ow);
	T33_OW(0x20080, 1);
	T33_OW(0x20088, v << 15 | v);
	T33_OW(0x20090, b + ((w + 7) >> 3) * 8 * h / 2);
	T33_OW(0x20054, 0x40034);
	T33_OW(0x20060, w << 16 | w);
	T33_OW(0x2008c, b);
	T33_OW(0x21070, 1);
	T33_OW(0x21064, 0x100);
	T33_OW(0x21078, 0x400080);
	T33_OW(0x21060, 0x35);
	T33_OW(0x21048, 4);
	T33_OW(0x21054, ob << 19 | ob << 3);
	T33_OW(0x2107c, b2);
	T33_OW(0x21080, b2 + ob * 0x400);
#undef T33_OW
	return n;
}

#endif
