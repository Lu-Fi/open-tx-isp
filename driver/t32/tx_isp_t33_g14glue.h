/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) wiring group 14 (openimp #99): the vendor-form block init /
 * refresh ports proven in port-loop rounds 7..10 run in the driver.
 *
 * g14a/b, YDNS / SDNS / CDNS:
 * - tisp_ydns_init / tisp_sdns_init / tisp_cdns_init (tx_isp_t33_i9A.h,
 *   oracle i9A) on the driver's info structs (ydnsInfo / sdnsInfo / cdnsInfo
 *   = t33_ydns_inf / t33_sdns_inf / t33_cdns_inf, vendor word layout), at the
 *   stIspInitPar words the vendor copies from fstframe_defaultp (232: init
 *   gain 0x10000, 250: gain tolerance 0x100);
 * - tisp_ydns_refresh / tisp_sdns_refresh (tx_isp_t33_refB.h, oracle refB),
 *   tisp_cdns_refresh (tx_isp_t33_refA.h, oracle refA) and their
 *   tisp_*_par_refresh (tx_isp_t33_parA.h / parB.h, oracles parA / parB):
 *   the init and every long-gain update (tisp_long_tgain_update) run them;
 *   the par_refresh keeps the vendor's own gain gate (last gain word, the
 *   tolerance word) and writes the list slots (or refresh 2 with the list
 *   busy);
 * - tparamsP: each block's section is copied into its own buffer at the
 *   init and at every bin switch (the vendor's tisp_bin_data_parse into its
 *   parameter struct), the info struct points at that copy, never at
 *   libimp's bin buffer.
 *
 * The register lists are registered and laid out by tisp_rcfg_module_register
 * (t33_mod_buf: the reg_init skeleton) before the init writes the slots;
 * system_irq_func_set is the fixed dispatch of t33_bst_irq (irq numbers
 * checked).  Only tx_isp_t32_hw.c (TX_ISP_T33) includes this file, after
 * tx_isp_t33_swglue.h.
 */
#ifndef TX_ISP_T33_G14GLUE_H
#define TX_ISP_T33_G14GLUE_H

#include "tx_isp_t33_i9A.h"
#include "tx_isp_t33_parA.h"
#include "tx_isp_t33_parB.h"
#include "tx_isp_t33_refB.h"
#define T33_REFA_GLUE	1
static u32 *cdnsInfo[4] = { t33_cdns_inf };
static void tisp_cdns_par_refresh(u32 ch, u32 *a1, u32 hyst);
/* refA's other wrappers (rdns / ysp) are not wired yet: declared only */
extern u32 *yspInfo[4];
extern void tisp_rdns_par_refresh(u32 ch, u32 *inf, u32 gain);
extern void tisp_ysp_par_refresh(u32 ch, u32 *inf, u32 gain);
#include "tx_isp_t33_refA.h"

/* the section copies t33_ydns_sec / t33_sdns_sec / t33_cdns_sec: tx_isp_t33_swglue.h */
/* init ydns sdns cdns, refreshes past the gain gate ydns sdns cdns, irq number mismatches */
u32 t33_g14_runs[7];
static u32 t33_g14_last;	/* the last par_refresh's return (1: below the gain tolerance) */

/* tisp_rcfg_module_register: the list page with its reg_init skeleton, marked built for the slot writes */
static int t33_g14_reg(u32 ch, u32 id, void *cb)
{
	(void)ch;
	(void)cb;
	if (!t33_mod_buf(id))
		return -1;
	t33_mod_done(id, 1);
	return 0;
}

static void t33_g14_alloc_fail(u32 line)
{
	isp_printf(2, "tx-isp-t33: block state alloc failed (%u)\n", line);
}

static void t33_g14_reg_fail(u32 line, int r)
{
	isp_printf(2, "tx-isp-t33: rcfg module register failed (%u, %d)\n", line, r);
}

/* private_kmalloc of the block state: the driver's static info struct (zeroed; the vendor's is not) */
static void *t33_g14_ydns_alloc(u32 size, u32 flags)
{
	(void)flags;
	if (size > sizeof(t33_ydns_inf))
		return NULL;
	memset(t33_ydns_inf, 0, sizeof(t33_ydns_inf));
	return t33_ydns_inf;
}

static void *t33_g14_sdns_alloc(u32 size, u32 flags)
{
	(void)flags;
	if (size > sizeof(t33_sdns_inf))
		return NULL;
	memset(t33_sdns_inf, 0, sizeof(t33_sdns_inf));
	return t33_sdns_inf;
}

static void *t33_g14_cdns_alloc(u32 size, u32 flags)
{
	(void)flags;
	if (size > sizeof(t33_cdns_inf))
		return NULL;
	memset(t33_cdns_inf, 0, sizeof(t33_cdns_inf));
	return t33_cdns_inf;
}

/* system_irq_func_set: t33_bst_irq dispatches the block's frame done on its fixed bit (irq = 32 + bit) */
static void t33_g14_irq_set(u32 ch, u32 idx, unsigned int b)
{
	(void)ch;
	if (idx != 32U + t33_bst_bit[b])
		t33_g14_runs[6]++;
}

static void t33_g14_ydns_irq(u32 ch, u32 idx, void *cb)
{
	(void)cb;
	t33_g14_irq_set(ch, idx, T33_BST_YDNS);
}

static void t33_g14_sdns_irq(u32 ch, u32 idx, void *cb)
{
	(void)cb;
	t33_g14_irq_set(ch, idx, T33_BST_SDNS);
}

static void t33_g14_cdns_irq(u32 ch, u32 idx, void *cb)
{
	(void)cb;
	t33_g14_irq_set(ch, idx, T33_BST_CDNS);
}

static int t33_g14_dma(u32 mod)
{
	return tisp_rcfg_get_dma_state(mod);
}

static const struct t33_par_ops t33_g14_ydns_pops = {
	.reg_write = t33_swg_wr_v, .dma_state = t33_g14_dma,
	.pre_update = func_ydns_bin_para_update, .para_update = func_ydns_para_update,
};

static const struct t33_par_ops t33_g14_sdns_pops = {
	.reg_write = t33_swg_wr_v, .dma_state = t33_g14_dma,
	.pre_update = tisp_sdns_other_params_transform, .para_update = func_sdns_para_update,
};

/* tisp_ydns_par_refresh(ch, gain, tol): the tolerance is ydnsInfo word 25 */
static void tisp_ydns_par_refresh(u32 ch, u32 gain, u32 tol)
{
	(void)tol;
	t33_g14_last = t33_ydns_par_refresh(ch, gain, ydnsInfo[ch & 3], t33_bst_cfg_mode, &t33_g14_ydns_pops,
					    &t33_bst_refresh[T33_BST_YDNS]);
}

static void tisp_sdns_par_refresh(u32 ch, u32 gain, u32 tol)
{
	t33_g14_last = t33_sdns_par_refresh(ch, gain, tol, sdnsInfo[ch & 3], t33_bst_cfg_mode, &t33_g14_sdns_pops,
					    &t33_bst_refresh[T33_BST_SDNS]);
}

/* tisp_cdns_par_refresh(ch, gain, hyst): a1 is the gain register passed through by tisp_cdns_refresh */
static void tisp_cdns_par_refresh(u32 ch, u32 *a1, u32 hyst)
{
	u32 *inf = cdnsInfo[ch & 3], prev = inf[T33_CDNS_GAIN_OFF / 4], g = (u32)(unsigned long)a1;

	t33_cdns_par_refresh(ch, g, hyst, (u8 *)inf, t33_bst_cfg_mode, &t33_bst_refresh[T33_BST_CDNS],
			     tisp_rcfg_get_dma_state, t33_swg_wr_v, t33_swg_cdns_para);
	/* below the hysteresis the port leaves the old gain (it returns the state pointer either way) */
	t33_g14_last = (prev != 0xffffffffU && (g >= prev ? g - prev : prev - g) < hyst) ? 1 : 0x10000;
}

static void tisp_ydns_refresh(u32 ch, u32 gain)
{
	t33_ydns_refresh(ch, gain, ydnsInfo, tisp_ydns_par_refresh);
}

static void tisp_sdns_refresh(u32 ch, u32 gain)
{
	t33_sdns_refresh(ch, gain, sdnsInfo, tisp_sdns_par_refresh);
}

static void tisp_cdns_refresh(u32 ch, u32 gain)
{
	t33_cdns_refresh(ch, (u32 *)(unsigned long)gain);
}

static const struct t33_dnsi_ops t33_g14_ydns_iops = {
	.reg_register = t33_g14_reg, .alloc = t33_g14_ydns_alloc, .alloc_fail = t33_g14_alloc_fail,
	.reg_fail = t33_g14_reg_fail, .refresh = tisp_ydns_refresh, .irq_set = t33_g14_ydns_irq,
};

static const struct t33_dnsi_ops t33_g14_sdns_iops = {
	.reg_register = t33_g14_reg, .alloc = t33_g14_sdns_alloc, .alloc_fail = t33_g14_alloc_fail,
	.reg_fail = t33_g14_reg_fail, .refresh = tisp_sdns_refresh, .irq_set = t33_g14_sdns_irq,
};

static const struct t33_dnsi_ops t33_g14_cdns_iops = {
	.reg_register = t33_g14_reg, .alloc = t33_g14_cdns_alloc, .alloc_fail = t33_g14_alloc_fail,
	.reg_fail = t33_g14_reg_fail, .refresh = tisp_cdns_refresh, .irq_set = t33_g14_cdns_irq,
};

/*
 * lib_tisp_init's tisp_<blk>_init (channel 0) on the bin section sec (copied into the tparamsP slot).  The
 * init's tisp<Blk>Refresh = 1 is the stream-on mark of T33_INIT_REFRESH (the list runs once more at the first
 * frame end), so the flag itself is cleared here as before.
 */
static void t33_g14_dns_init(unsigned int b, const u8 *sec)
{
	unsigned long flags;
	u32 tp[4] = { 0 };
	u8 cr = 0;

	/* the list page is allocated (GFP_KERNEL) before the interrupts go off; reg_register then finds it */
	if (!t33_mod_buf(t33_bst_mod[b]))
		return;
	t33_bst_hold(b);
	local_irq_save(flags);
	switch (b) {
	case T33_BST_YDNS:
		memcpy(t33_ydns_sec, sec, sizeof(t33_ydns_sec));
		tp[0] = (u32)(unsigned long)t33_ydns_sec - T33_TP_OFF_YDNS;
		t33_ydns_init_ch(0, (const u8 *)t33_st_isp_init_par_mode, (u32 *)ydnsInfo, tp, &t33_g14_ydns_iops);
		t33_g14_runs[0]++;
		break;
	case T33_BST_SDNS:
		memcpy(t33_sdns_sec, sec, sizeof(t33_sdns_sec));
		tp[0] = (u32)(unsigned long)t33_sdns_sec - T33_TP_OFF_SDNS;
		t33_sdns_init_ch(0, (const u8 *)t33_st_isp_init_par_mode, (u32 *)sdnsInfo, tp, &t33_g14_sdns_iops);
		t33_g14_runs[1]++;
		break;
	default:
		memcpy(t33_cdns_sec, sec, sizeof(t33_cdns_sec));
		tp[0] = (u32)(unsigned long)t33_cdns_sec - T33_CDNS_SEC_OFF;
		t33_cdns_init_ch(0, (const u8 *)t33_st_isp_init_par_mode, (u32 *)cdnsInfo, tp, &cr, &t33_g14_cdns_iops);
		t33_g14_runs[2]++;
		break;
	}
	t33_bst_refresh[b] = 0;
	local_irq_restore(flags);
}

/* tisp_long_tgain_update's tisp_{cdns,ydns,sdns}_refresh (process context, t33_gr_lock held) */
static void t33_g14_dns_refresh(unsigned int b, u32 gain)
{
	unsigned long flags;

	if (!(t33_mod_built & (1ULL << t33_bst_mod[b])))
		return;
	t33_bst_hold(b);
	local_irq_save(flags);
	t33_g14_last = 1;
	if (b == T33_BST_YDNS && t33_ydns_inf[24])
		tisp_ydns_refresh(0, gain);
	else if (b == T33_BST_SDNS && t33_sdns_inf[0x24])
		tisp_sdns_refresh(0, gain);
	else if (b == T33_BST_CDNS && t33_cdns_inf[T33_CDNS_SEC_PTR_OFF / 4])
		tisp_cdns_refresh(0, gain);
	if (t33_g14_last != 1) {
		t33_g14_runs[b == T33_BST_YDNS ? 3 : b == T33_BST_SDNS ? 4 : 5]++;
		t33_gr_runs[t33_bst_mod[b]]++;
	}
	local_irq_restore(flags);
}

#endif /* TX_ISP_T33_G14GLUE_H */
