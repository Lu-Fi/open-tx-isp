/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AE statistics set-up (module list 30) and digital gain (DG,
 * module list 5), as tisp_ae_init leaves them: tisp_ae_malloc_info (four
 * 4 KiB statistics and four 2 KiB histogram buffers, one 16 KiB and one
 * 8 KiB block), tisp_normal_ae_init's hardware words from the AE section
 * (IQ bin section 1: zone start x / y at 0xad4 / 0xad0, the 30 x 30 zone
 * grid over the picture, the histogram threshold 0xad8 and the luma limit
 * 0xada), tisp_ae_reg_write, and the DG words at the AE's initial DG
 * (0x400, 1.0).  QEMU trace of the vendor module (openimp #389): lists 30
 * and 5 word-equal (0x1708c is not in the QEMU trace).
 */
#ifndef TX_ISP_T33_AE_H
#define TX_ISP_T33_AE_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_AE		30
#define T33_MOD_DG		5
#define T33_SEC_AE		1
#define T33_AE_SEC_MIN		0xadcU		/* the hardware words read up to 0xadb */
#define T33_AE_STAT_BYTES	0x4000U		/* 4 x 0x1000 zone statistics */
#define T33_AE_HIST_BYTES	0x2000U		/* 4 x 0x800 histograms */
#define T33_AE_DG_INIT		0x400U

static const struct t33_tcmd t33_ae_tmpl[] = {
	{ 0x17010, 1, 0 }, { 0x17040, 2, 0 }, { 0x17064, 14, 0 }, { 0x170b8, 1, 0 },
};

static const struct t33_tcmd t33_dg_tmpl[] = {
	{ 0x4010, 1, 0 }, { 0x4060, 3, 0 },
};

/*
 * AE list for a w x h picture from the AE section sec; stat / hist: the
 * physical addresses of the statistics (T33_AE_STAT_BYTES) and histogram
 * (T33_AE_HIST_BYTES) buffers.  Returns the list length.
 */
static inline int t33_ae_init(const u8 *sec, u32 w, u32 h, u32 stat, u32 hist, u32 *l, u32 cap)
{
	int len = t33_rcfg_tmpl_init(l, cap, t33_ae_tmpl, ARRAY_SIZE(t33_ae_tmpl));
	u32 y0 = t32_tp_le16(sec + 0xad0), x0 = t32_tp_le16(sec + 0xad4), lum = t32_tp_le16(sec + 0xada);
	u32 zx = (u32)((int)(w - x0 + 1) / 0x1e), zy = (u32)((int)(h - y0 + 1) / 0x1e), k;

	if (len <= 0)
		return len;
	if (lum < 0xff)
		lum = 0xff;
	/* tisp_ae_malloc_info: 4 statistics and 4 histogram buffers, 4 - 1 in use */
	for (k = 0; k < 4; k++) {
		T33_SW(l, 7 + k, stat + 0x1000 * k);
		T33_SW(l, 11 + k, hist + 0x800 * k);
	}
	/* tisp_normal_ae_init's hardware words, tisp_ae_reg_write */
	T33_SW(l, 3, (x0 & 0xfff) << 16 | (y0 & 0xfff));
	T33_SW(l, 4, (zy & 0xff) << 16 | (zx & 0xff));
	T33_SW(l, 6, ((lum * 2 / 3) & 0xff) << 8 | sec[0xad8] | 0x110000);
	T33_SW(l, 0xf, 3 | 0x10010000 | 3 << 4);
	T33_SW(l, 1, 1);
	return len;
}

/* DG list at the AE's DG (0x400 = 1.0) */
static inline int t33_dg_init(u32 dg, u32 *l, u32 cap)
{
	int len = t33_rcfg_tmpl_init(l, cap, t33_dg_tmpl, ARRAY_SIZE(t33_dg_tmpl));

	if (len <= 0)
		return len;
	T33_SW(l, 3, (dg & 0xffff) | dg << 16);
	T33_SW(l, 4, (dg & 0xffff) | dg << 16);
	T33_SW(l, 5, 0);
	T33_SW(l, 1, 1);
	return len;
}

#endif /* TX_ISP_T33_AE_H */
