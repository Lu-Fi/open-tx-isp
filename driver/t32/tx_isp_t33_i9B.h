/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) block init functions: tisp_rdns_init, tisp_dmsc_init, tisp_ysp_init.
 * Each registers the block's register list, allocates the per-channel block
 * state (word array, byte offsets as in the vendor code), points it at its
 * section of the IQ parameters (tparamsP[ch] + offset), stores the init gain
 * word, refreshes at the gain and installs the frame-done interrupt.
 * YSP additionally keeps a zeroed copy of its 2724 byte section (ysp_paramsP).
 */
#ifndef TX_ISP_T33_I9B_H
#define TX_ISP_T33_I9B_H

#include "tx_isp_t33_blk.h"

#ifndef T33_I9B_GLUE
extern u32 *rdnsInfo[4];
extern u32 *dmscInfo[4];
extern u32 *yspInfo[4];
extern u8 *tparamsP[4];
extern u8 *ysp_paramsP[4];
extern int printk(const char *fmt, ...);
extern int isp_printf(u32 level, const char *fmt, ...);
extern void *private_kmalloc(unsigned long size, unsigned int flags);
extern void *private_vmalloc(unsigned long size);
extern int tisp_rcfg_module_register(u32 ch, u32 id, void *reg_init);
extern void tisp_rcfg_rdns_reg_init(void);
extern void tisp_rcfg_dmsc_reg_init(void);
extern void tisp_rcfg_ysp_reg_init(void);
extern void tisp_rdns_refresh(u32 ch, u32 gain);
extern void tisp_dmsc_refresh(u32 ch, u32 gain);
extern void tisp_ysp_refresh(u32 ch, u32 gain);
extern void system_irq_func_set(u32 ch, u32 irq, void *fn);
extern void tisp_rdns_t33_frame_done_interrupt(void);
extern void tisp_dmsc_t33_frame_done_interrupt(void);
extern void tisp_ysp_t33_frame_done_interrupt(void);
#endif

/* tisp_rdns_init: inf word 232 = gain, word 250 = init gain word */
static inline int t33_rdns_init(u32 ch, const u32 *inf)
{
	u32 c = ch & 0xff;
	u32 *st;
	int rc;

	rc = tisp_rcfg_module_register(c, 3, (void *)tisp_rcfg_rdns_reg_init);
	if (rc < 0)
		printk("%s %d: rcfg register failed %d\n", __func__, 1024, rc);
	st = private_kmalloc(848, 208);
	rdnsInfo[c] = st;
	if (!st)
		isp_printf(2, "%s %d: no memory\n", __func__, 1029);
	st = rdnsInfo[c];
	st[836 / 4] = (u32)-1;
	st[840 / 4] = (u32)-1;
	st[844 / 4] = inf[1000 / 4];
	st[832 / 4] = (u32)(unsigned long)(tparamsP[c] + 16520);
	tisp_rdns_refresh(c, inf[928 / 4]);
	system_irq_func_set(c, 39, (void *)tisp_rdns_t33_frame_done_interrupt);
	return 0;
}

/* tisp_dmsc_init */
static inline int t33_dmsc_init(u32 ch, const u32 *inf)
{
	u32 c = ch & 0xff;
	u32 *st;
	int rc;

	rc = tisp_rcfg_module_register(c, 9, (void *)tisp_rcfg_dmsc_reg_init);
	if (rc < 0)
		printk("%s %d: rcfg register failed %d\n", __func__, 2364, rc);
	st = private_kmalloc(2696, 208);
	dmscInfo[c] = st;
	if (!st)
		isp_printf(2, "%s %d: no memory\n", __func__, 2369);
	st = dmscInfo[c];
	st[2680 / 4] = (u32)(unsigned long)(tparamsP[c] + 17900);
	st[2684 / 4] = (u32)-1;
	st[2688 / 4] = 128;
	st[2692 / 4] = inf[1000 / 4];
	tisp_dmsc_refresh(c, inf[928 / 4]);
	system_irq_func_set(c, 43, (void *)tisp_dmsc_t33_frame_done_interrupt);
	return 0;
}

/* tisp_ysp_init */
static inline int t33_ysp_init(u32 ch, const u32 *inf)
{
	u32 c = ch & 0xff;
	u32 *st;
	int rc;

	rc = tisp_rcfg_module_register(c, 14, (void *)tisp_rcfg_ysp_reg_init);
	if (rc < 0)
		printk("%s %d: rcfg register failed %d\n", __func__, 1753, rc);
	st = private_kmalloc(2184, 208);
	yspInfo[c] = st;
	if (!st)
		isp_printf(2, "%s %d: no memory\n", __func__, 1758);
	memset(yspInfo[c], 0, 2184);
	st = yspInfo[c];
	st[2172 / 4] = (u32)(unsigned long)(tparamsP[c] + 0x907c);
	st[2176 / 4] = (u32)-1;
	st[2180 / 4] = inf[1000 / 4];
	ysp_paramsP[c] = private_vmalloc(2724);
	memset(ysp_paramsP[c], 0, 2724);
	memcpy(ysp_paramsP[c], tparamsP[c] + 0x907c, 2724);
	tisp_ysp_refresh(c, inf[928 / 4]);
	system_irq_func_set(c, 54, (void *)tisp_ysp_t33_frame_done_interrupt);
	return 0;
}

#endif /* TX_ISP_T33_I9B_H */
