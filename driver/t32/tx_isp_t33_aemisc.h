/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AE / AWB block-info allocation and the AE start parameters:
 * tisp_ae_malloc_info, tisp_awb_malloc_info, tisp_ae_set_start_param.
 * Kernel services are passed in (struct t33_aemisc_ops) so the code can be
 * run against the vendor module by the binary oracle.
 */
#ifndef TX_ISP_T33_AEMISC_H
#define TX_ISP_T33_AEMISC_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_awb.h"
#include "tx_isp_t33_aealg.h"

struct t33_aemisc_ops {
	void *(*kmalloc)(u32 size, u32 flags);
	void (*kfree)(void *p);
	void (*reg_write)(u32 ch, u32 mod, u32 slot, u32 val);
	void (*err)(int level, int line);
};

#define T33_AEM_GFP		208U
#define T33_AEM_ENOMEM		12
#define T33_AEM_PHYS_BASE	0x80000000U

/*
 * tisp_ae_malloc_info: inf is the AE info block as a word array (words
 * 5..11: statistics base, count, histogram base ...).  The statistics block
 * (word 6) and the histogram block (word 9) are allocated when missing.
 */
static inline int t33_ae_malloc_info(u32 ch, u32 *inf, const struct t33_aemisc_ops *o)
{
	u32 c = ch & 0xff;
	void *p;

	inf[7] = 4;
	if (!inf[6]) {
		p = o->kmalloc(0x4000, T33_AEM_GFP);
		inf[6] = (u32)(unsigned long)p;
		if (!p) {
			o->err(2, 80);
			return -12;
		}
	}
	inf[5] = inf[6] + 0x80000000U;
	inf[10] = 4;
	if (!inf[9]) {
		p = o->kmalloc(0x2000, T33_AEM_GFP);
		inf[9] = (u32)(unsigned long)p;
		if (!p) {
			o->err(2, 93);
			o->kfree((void *)(unsigned long)inf[6]);
			return -T33_AEM_ENOMEM;
		}
	}
	inf[8] = inf[9] + 0x80000000U;
	o->reg_write(c, 30, 7, inf[5]);
	o->reg_write(c, 30, 8, inf[5] + 0x1000);
	o->reg_write(c, 30, 9, inf[5] + 0x2000);
	o->reg_write(c, 30, 10, inf[5] + 0x3000);
	o->reg_write(c, 30, 11, inf[8]);
	o->reg_write(c, 30, 12, inf[8] + 0x800);
	o->reg_write(c, 30, 13, inf[8] + 0x1000);
	o->reg_write(c, 30, 14, inf[8] + 0x1800);
	((u8 *)inf)[16] = (u8)(inf[7] - 1);
	((u8 *)inf)[8] = (u8)(inf[10] - 1);
	((u8 *)inf)[9] = 1;
	((u8 *)inf)[11] = 1;
	return 0;
}

/*
 * tisp_awb_malloc_info: stat = &stAwbStatBuff[ch], infoslot = &awbInfo[ch].
 * Like the vendor code it does not check the first allocation before use.
 */
static inline void t33_awb_malloc_info(u32 ch, u32 *stat, const u32 *infoslot, const struct t33_aemisc_ops *o)
{
	u32 c = ch & 0xff;
	u32 *blk;
	u8 *aux;
	void *p;

	if (*stat) {
		o->err(1, 0);
		return;
	}
	aux = (u8 *)(unsigned long)((const u32 *)(unsigned long)*infoslot)[2];
	p = o->kmalloc(12, T33_AEM_GFP);
	*stat = (u32)(unsigned long)p;
	if (!p)
		o->err(2, 1046);
	blk = (u32 *)(unsigned long)*stat;
	blk[2] = 4;
	p = o->kmalloc(29696 * blk[2], T33_AEM_GFP);
	blk[1] = (u32)(unsigned long)p;
	if (!p)
		o->err(2, 1053);
	blk[0] = blk[1] + 0x80000000U;
	o->reg_write(c, 29, 11, blk[0]);
	o->reg_write(c, 29, 12, blk[0] + 29696);
	o->reg_write(c, 29, 13, blk[0] + 0xe800);
	o->reg_write(c, 29, 14, blk[0] + 0xe800 + 29696);
	aux[127] = 0;
	aux[143] = 0;
	aux[126] = (u8)(blk[2] - 1);
}

/*
 * tisp_ae_set_start_param: R = ae_cache[ch], T = the tuning block of
 * stAeInfo[ch]; a = the caller's parameter block (EV at 920 / 924, again /
 * dgain / integration time at 932 / 936 / 940).
 */
static inline void t33_ae_start_param(u8 *R, u8 *T, const u8 *a)
{
	u32 it = t33_le32(a + 940), ag = t33_le32(a + 932), dg = t33_le32(a + 936);
	u32 lo = t33_le32(a + 920), hi = t33_le32(a + 924);
	unsigned int k;

	t32_wr32(R, 576, it);
	t32_wr32(R, 580, ag);
	t32_wr32(R, 588, dg);
	for (k = 0; k < 10; k++) {
		t32_wr32(R, 208 + 4 * k, it);
		t32_wr32(R, 248 + 4 * k, ag);
		t32_wr32(R, 288 + 4 * k, dg);
		t32_wr64(R, 8 * k, t32_aem_mult3_64(10, (t32_u64)it << 10, ag, dg));
		t32_wr32(R, 328 + 4 * k, t33_fpm2_32(10, ag, dg));
	}
	t32_wr32(T, 368, (lo >> 10) + (hi << 22));
	t32_wr32(T, 372, hi >> 10);
}

#endif /* TX_ISP_T33_AEMISC_H */
