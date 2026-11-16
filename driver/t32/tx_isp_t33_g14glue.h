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
static void tisp_dmsc_refresh(u32 ch, u32 gain);	/* g14b-2 below */
/* refA's rdns / ysp wrappers: the par_refreshes of i9H / i9M (g14-1, below) */
static u32 *yspInfo[4] = { t33_ysp_v };
static void tisp_rdns_par_refresh(u32 ch, u32 *a1, u32 thr);
static void tisp_ysp_par_refresh(u32 ch, u32 *a1, u32 thr);
static void tisp_rdns_refresh(u32 ch, u32 gain);
static void tisp_ysp_refresh(u32 ch, u32 gain);
#include "tx_isp_t33_refA.h"

/* the section copies t33_ydns_sec / t33_sdns_sec / t33_cdns_sec: tx_isp_t33_swglue.h */
/* init ydns sdns cdns, refreshes past the gain gate ydns sdns cdns, irq number mismatches */
u32 t33_g14_runs[14];	/* + [7] dmsc init, [8] dmsc refreshes past the gate, [9..12] rdns / ysp the same, [13] dpc init */
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
	else if (b == T33_BST_DMSC && t33_dmsc_v[670])
		tisp_dmsc_refresh(0, gain);
	else if (b == T33_BST_RDNS && t33_rdns_v[T33_RDNS_SEC_WORD])
		tisp_rdns_refresh(0, gain);
	else if (b == T33_BST_YSP && t33_ysp_v[T33_YSP_SEC_WORD])
		tisp_ysp_refresh(0, gain);
	if (t33_g14_last != 1) {
		t33_g14_runs[b == T33_BST_YDNS ? 3 : b == T33_BST_SDNS ? 4 : b == T33_BST_CDNS ? 5 :
			     b == T33_BST_DMSC ? 8 : b == T33_BST_RDNS ? 10 : 12]++;
		t33_gr_runs[t33_bst_mod[b]]++;
	}
	local_irq_restore(flags);
}


/*
 * g14b-2, DMSC: tisp_dmsc_init (i9B), tisp_dmsc_refresh (refB) and tisp_dmsc_par_refresh (i9N) on the
 * vendor-layout dmscInfo (t33_dmsc_v: section 670, last gain 671, sharpness 672, gain tolerance 673).
 * i9B's tisp_rdns_init / tisp_ysp_init are not wired yet (their refreshes are declared only).  The i9B
 * names are mapped onto the glue below and unmapped after the include (the driver's own t33_*_init
 * builders keep their names).
 */
static u32 *dmscInfo[4] = { t33_dmsc_v };
static u8 *t33_g14_tp8[4];			/* tparamsP of the i9B ports (u8 view) */

static void *t33_g14_kmalloc(unsigned long size, unsigned int flags)
{
	(void)flags;
	if (size == sizeof(t33_dmsc_v))
		return t33_dmsc_v;
	if (size == sizeof(t33_rdns_v)) {	/* rdnsInfo (848 B; zeroed, the vendor's is not) */
		memset(t33_rdns_v, 0, sizeof(t33_rdns_v));
		return t33_rdns_v;
	}
	if (size == sizeof(t33_ysp_v))		/* yspInfo (2184 B; tisp_ysp_init zeroes it itself) */
		return t33_ysp_v;
	return NULL;
}

/* tisp_ysp_init's private_vmalloc of ysp_paramsP: the switch's section copy t33_swg_ysp_par */
static void *t33_g14_vmalloc(unsigned long size)
{
	return size == sizeof(t33_swg_ysp_par) ? t33_swg_ysp_par : NULL;
}

static void t33_g14_irq_v(u32 ch, u32 irq, void *fn)
{
	(void)fn;
	if (irq == 43U)		/* DMSC */
		t33_g14_irq_set(ch, irq, T33_BST_DMSC);
	else if (irq == 39U)	/* RDNS */
		t33_g14_irq_set(ch, irq, T33_BST_RDNS);
	else
		t33_g14_irq_set(ch, irq, T33_BST_YSP);
}

#define T33_I9N_GLUE	1
#define dmscCfgMode	t33_bst_cfg_mode
#define tispDmscRefresh	t33_bst_refresh[T33_BST_DMSC]
#define tisp_ratio	t33_swg_ratio
#include "tx_isp_t33_i9N.h"
#undef dmscCfgMode
#undef tispDmscRefresh
#undef tisp_ratio

static void tisp_dmsc_par_refresh(u32 ch, u32 gain, u32 thr)
{
	u32 last = dmscInfo[ch & 3][671];

	t33_dmsc_par_step(ch, gain, thr);
	t33_g14_last = (last != 0xffffffffU && (gain >= last ? gain - last : last - gain) < thr) ? 1 : 0x10000;
}

static void tisp_dmsc_refresh(u32 ch, u32 gain)
{
	t33_dmsc_refresh(ch, gain, dmscInfo, tisp_dmsc_par_refresh);
}

/*
 * g14-1, RDNS / YSP: tisp_rdns_par_refresh (i9H) and tisp_ysp_par_refresh (i9M) behind refA's
 * tisp_rdns_refresh / tisp_ysp_refresh (the state's tolerance word as the threshold), on the vendor-layout
 * rdnsInfo (t33_rdns_v: section 208, last gain 209, EV 210, tolerance 211) and yspInfo (t33_ysp_v: section
 * 543, last gain 544, tolerance 545); ysp_paramsP[0] = t33_swg_ysp_par (the bin data refresh reads it).
 */
#define T33_I9H_GLUE	1
#include "tx_isp_t33_i9H.h"

#define T33_YSPPR_GLUE	1
#define yspCfgMode	t33_bst_cfg_mode
#define tispYspRefresh	t33_bst_refresh[T33_BST_YSP]
#define tisp_ysp_bin_data_refresh	t33_swg_ysp_bin
#define tisp_ysp_reg_data_refresh	t33_swg_ysp_reg
#define tisp_mdns_decode_reg_write	t33_swg_mdns_dec
#define t33_ysp_wr	t33_i9m_wr		/* the frame done port (tx_isp_t33_fd_ysp.h) has its own helpers */
#define t33_ysp_q4	t33_i9m_q4
#define t33_ysp_r3	t33_i9m_r3
#include "tx_isp_t33_i9M.h"
#undef t33_ysp_wr
#undef t33_ysp_q4
#undef t33_ysp_r3
#undef yspCfgMode
#undef tispYspRefresh
#undef tisp_ysp_bin_data_refresh
#undef tisp_ysp_reg_data_refresh
#undef tisp_mdns_decode_reg_write

static void tisp_rdns_par_refresh(u32 ch, u32 *a1, u32 thr)
{
	u32 last = rdnsInfo[ch & 3][T33_I9H_GAIN], g = (u32)(unsigned long)a1;

	t33_rdns_par_refresh_i9h(ch, g, thr);
	t33_g14_last = (last != 0xffffffffU && (g >= last ? g - last : last - g) < thr) ? 1 : 0x10000;
}

static void tisp_ysp_par_refresh(u32 ch, u32 *a1, u32 thr)
{
	u32 last = yspInfo[ch & 3][2176 / 4], g = (u32)(unsigned long)a1;

	t33_ysp_par_refresh(ch, g, thr);
	t33_g14_last = (last != 0xffffffffU && (g >= last ? g - last : last - g) < thr) ? 1 : 0x10000;
}

static void tisp_rdns_refresh(u32 ch, u32 gain)
{
	t33_rdns_refresh(ch, (u32 *)(unsigned long)gain);
}

static void tisp_ysp_refresh(u32 ch, u32 gain)
{
	t33_ysp_refresh(ch, (u32 *)(unsigned long)gain);
}

static u8 *t33_g14_yspp[4];		/* ysp_paramsP */
#define T33_I9B_GLUE	1
#define t33_rdns_init	t33_i9b_rdns_init
#define t33_dmsc_init	t33_i9b_dmsc_init
#define t33_ysp_init	t33_i9b_ysp_init
#define tparamsP	t33_g14_tp8
#define tisp_rcfg_module_register	t33_g14_reg
#define private_kmalloc	t33_g14_kmalloc
#define private_vmalloc	t33_g14_vmalloc
#define ysp_paramsP	t33_g14_yspp
#define system_irq_func_set	t33_g14_irq_v
#define tisp_rcfg_rdns_reg_init	NULL
#define tisp_rcfg_dmsc_reg_init	NULL
#define tisp_rcfg_ysp_reg_init	NULL
#define tisp_rdns_t33_frame_done_interrupt	NULL
#define tisp_dmsc_t33_frame_done_interrupt	NULL
#define tisp_ysp_t33_frame_done_interrupt	NULL
#include "tx_isp_t33_i9B.h"
#undef t33_rdns_init
#undef t33_dmsc_init
#undef t33_ysp_init
#undef tparamsP
#undef tisp_rcfg_module_register
#undef private_kmalloc
#undef private_vmalloc
#undef ysp_paramsP
#undef system_irq_func_set
#undef tisp_rcfg_rdns_reg_init
#undef tisp_rcfg_dmsc_reg_init
#undef tisp_rcfg_ysp_reg_init
#undef tisp_rdns_t33_frame_done_interrupt
#undef tisp_dmsc_t33_frame_done_interrupt
#undef tisp_ysp_t33_frame_done_interrupt

/* lib_tisp_init's tisp_dmsc_init on the bin section (copied into its tparamsP slot); refresh as the other inits */
static void t33_g14_dmsc_init(const u8 *sec)
{
	unsigned long flags;

	if (!t33_mod_buf(T33_MOD_DMSC))
		return;
	t33_bst_hold(T33_BST_DMSC);
	local_irq_save(flags);
	memcpy(t33_dmsc_sec, sec, sizeof(t33_dmsc_sec));
	t33_g14_tp8[0] = t33_dmsc_sec - T33_TP_OFF_DMSC;
	memset(t33_dmsc_v, 0, sizeof(t33_dmsc_v));	/* the vendor's kmalloc is not zeroed; the init writes 670..673 */
	t33_i9b_dmsc_init(0, t33_st_isp_init_par_mode);
	t33_g14_runs[7]++;
	t33_bst_refresh[T33_BST_DMSC] = 0;
	local_irq_restore(flags);
}

/*
 * lib_tisp_init's tisp_rdns_init / tisp_ysp_init (g14-1) on the bin section, copied into its tparamsP slot
 * (t33_rdns_sec / t33_ysp_sec; the switch copies the new bin's section there too).
 */
static void t33_g14_rdns_init(const u8 *sec)
{
	unsigned long flags;

	if (!t33_mod_buf(T33_MOD_RDNS))
		return;
	t33_bst_hold(T33_BST_RDNS);
	local_irq_save(flags);
	memcpy(t33_rdns_sec, sec, sizeof(t33_rdns_sec));
	t33_g14_tp8[0] = t33_rdns_sec - T33_TP_OFF_RDNS;
	t33_i9b_rdns_init(0, t33_st_isp_init_par_mode);
	t33_g14_runs[9]++;
	t33_bst_refresh[T33_BST_RDNS] = 0;
	local_irq_restore(flags);
}

static void t33_g14_ysp_init(const u8 *sec)
{
	unsigned long flags;

	if (!t33_mod_buf(T33_MOD_YSP))
		return;
	t33_bst_hold(T33_BST_YSP);
	local_irq_save(flags);
	memcpy(t33_ysp_sec, sec, sizeof(t33_ysp_sec));
	t33_g14_tp8[0] = t33_ysp_sec - T33_YSP_SEC;
	t33_i9b_ysp_init(0, t33_st_isp_init_par_mode);
	t33_g14_runs[11]++;
	t33_bst_refresh[T33_BST_YSP] = 0;
	local_irq_restore(flags);
}

/*
 * g14-2a, DPC: lib_tisp_init's tisp_dpc_init (port-loop block i9G) on the vendor-layout dpcInfo (t33_dpc_v:
 * section 170 = t33_dpc_sec, the tparamsP + 0x3e2c copy, gain 171): bin copy, gain interpolation, the DPC /
 * CTR / CAC parameter updates and the vendor's 37 slot stores, refresh mode and irq 38.  Before: the DPC init
 * builder (t33_dpc_init) and the section / gain words set afterwards.
 */
#include "tx_isp_t33_i9G.h"

static int t33_g14_dpc_reg(u8 ch, u32 mod)
{
	return t33_g14_reg(ch, mod, NULL);
}

static void t33_g14_dpc_warn(int r)
{
	t33_g14_reg_fail(2, r);
}

static void *t33_g14_dpc_alloc(u32 size, u32 flags)
{
	(void)flags;
	return size == sizeof(t33_dpc_v) ? t33_dpc_v : NULL;	/* the init zeroes it itself */
}

static void t33_g14_zero(void *p, u32 n)
{
	memset(p, 0, n);
}

static void t33_g14_dpc_wr(u8 ch, u32 mod, u32 where, u32 val)
{
	tisp_reg_write_t33(ch, mod, where, val);
}

static void t33_g14_dpc_irq(u8 ch, u32 irq)
{
	t33_g14_irq_set(ch, irq, T33_BST_DPC);
}

static const struct t33_dpc_init_ops t33_g14_dpc_ops = {
	.module_register = t33_g14_dpc_reg, .warn = t33_g14_dpc_warn, .alloc = t33_g14_dpc_alloc,
	.zero = t33_g14_zero, .reg_write = t33_g14_dpc_wr, .dma_state = t33_g14_dma, .irq_set = t33_g14_dpc_irq,
};

static void t33_g14_dpc_init(const u8 *sec)
{
	unsigned long flags;
	u8 *tp[4] = { NULL };

	if (!t33_mod_buf(T33_MOD_DPC))
		return;
	t33_bst_hold(T33_BST_DPC);
	local_irq_save(flags);
	memcpy(t33_dpc_sec, sec, sizeof(t33_dpc_sec));
	tp[0] = t33_dpc_sec - 15916;
	t33_dpc_init_vendor(&t33_g14_dpc_ops, 0, T33_INIT_GAIN, dpcInfo, tp, t33_bst_cfg_mode,
			    &t33_bst_refresh[T33_BST_DPC]);
	t33_g14_runs[13]++;
	t33_bst_refresh[T33_BST_DPC] = 0;
	local_irq_restore(flags);
}

/*
 * g14c, BCSH (list 18): lib_tisp_init's tisp_bcsh_init (port-loop block bcshN), tisp_bcsh_irq_frame_down (bcshO,
 * system irq 52), tisp_bcsh_ev_update and the API setters brightness / contrast (bcshK), hue / saturation (bcshH)
 * on the vendor layout: bcsh_para[0] = t33_bcsh_st (0x290 B, word 0 = the section t33_bcsh_sec = tparamsP +
 * 0x8b78), bcshInfo[0] = t33_bcsh_inf16 (tisp_params_transform's u16 block, 0xb2 B).  The writers store the list
 * 18 slots with tisp_reg_write_t33 when the list is idle (tispBcshRefresh 1: irq 52 starts the list), else
 * tispBcshRefresh 2 (irq 52 transforms again and rewrites the slots).  Their callees are the proven parts of
 * tx_isp_t33_bcsh.h / bcshI.h (blocks bcshI, bcshct, bcshsw, bcshlist): get_new_para, interp_by_ev, interp_by_ct,
 * calc_para_refresh modes 0..3, params_transform.  Before: the init builder t33_bcsh_init and the EV update into a
 * list copy (t33_gr_commit); the CT update and the bin switch keep their proven driver form on the same state.
 */
static u16 t33_bcsh_inf16[(T33_BCSH_INFO_BYTES + 1) / 2];
static void *bcshInfo[4];
static u8 tispBcshRefresh;
static u32 bcsh_frame_num[4];
static u8 *t33_g14c_tp[4];
static const u8 *t33_g14c_lut;	/* the CT knots of the bin being initialised (the init's interp_by_ct) */
/* init, irq 52 list starts, irq 52 rewrites, API sets, EV updates past the hysteresis, irq number mismatches */
u32 t33_g14c_runs[6];
/* the vendor module defaults bcsh_ip (.data: CT 5000, 0xff), bcsh_cp / bcsh_sp (.bss), bcsh_ap (.data) */
static u8 bcsh_ip[40] = { [24] = 0x88, [25] = 0x13, [28] = 0xff };
static u8 bcsh_cp[300], bcsh_sp[112], bcsh_ap[144];

static int t33_g14c_reg(u32 ch, u32 id, u32 (*fn)(u32 *))
{
	(void)fn;
	return t33_g14_reg(ch, id, NULL);
}

static u32 t33_g14c_rinit(u32 *e)	/* the skeleton is laid out by t33_mod_buf (tisp_rcfg_bcsh_reg_init) */
{
	(void)e;
	return 0;
}

static void *t33_g14c_vmalloc(unsigned long size)
{
	return size == T33_BCSH_ST_BYTES ? t33_bcsh_st : NULL;
}

static void *t33_g14c_kmalloc(unsigned long size, unsigned int flags)
{
	(void)flags;
	return size == T33_BCSH_INFO_BYTES ? (void *)t33_bcsh_inf16 : NULL;
}

static void t33_g14c_lock_init(void *l)
{
	(void)l;
}

static __printf(2, 3) int t33_g14c_printf(int level, const char *fmt, ...)
{
	va_list ap;
	int r = 0;

	if (level >= 1) {	/* level 0: the version line */
		va_start(ap, fmt);
		r = vprintk(fmt, ap);
		va_end(ap);
	}
	return r;
}

/* tisp_bcsh_get_new_para(sec, bcsh_para[ch] + 0x25c) */
static int t33_g14c_new_para(u8 *sec)
{
	t33_bcsh_new_para(sec, (u8 *)bcsh_para[0] + 0x25c);
	return 0;
}

#include "tx_isp_t33_bcshI.h"

static int tisp_bcsh_interp_by_ev(u32 ch, u32 a1, u32 lo, u32 hi, u32 force)
{
	return t33_bcsh_interp_by_ev(ch & 3, a1, lo, hi, force);
}

static int tisp_bcsh_interp_by_ct(u32 ch, u32 a1, u32 ct, u32 a3, u32 force)
{
	u8 *st = bcsh_para[ch & 3];

	(void)a1;
	(void)a3;
	if (!t33_g14c_lut)
		return 1;
	return t33_bcsh_ct_interp(st, *(u8 **)st, t33_g14c_lut, (int)ct, (int)force);
}

/* mode 0: all three parts, 1: brightness / contrast / saturation, 2: hue, 3: the tail */
static int tisp_bcsh_calc_para_refresh(u32 ch, u32 mode)
{
	u8 *st = bcsh_para[ch & 3];
	const u8 *sec = *(u8 **)st;

	if (mode == 0)
		t33_bcsh_calc(st, sec);
	else if (mode == 1)
		t33_bcsh_calc_bcs(st, sec);
	else if (mode == 2)
		t33_bcsh_hue(st, sec);
	else if (mode == 3)
		t33_bcsh_calc_tail(st, sec);
	return 0;
}

static int tisp_params_transform(u32 ch)
{
	u8 *st = bcsh_para[ch & 3];

	t33_bcsh_transform(st, *(u8 **)st, bcshInfo[ch & 3]);
	return 0;
}

static int t33_g14c_fd_cb(void)	/* registered for irq 52; t33_bcsh_fd below is the dispatch */
{
	return 1;
}

static int t33_g14c_irq(u32 ch, u32 id, int (*fn)(void))
{
	(void)ch;
	(void)fn;
	if (id != 52U)
		t33_g14c_runs[5]++;
	return 0;
}

#define bcshCfgMode	t33_bst_cfg_mode
#define tparamsP	t33_g14c_tp
#define tisp_rcfg_bcsh_reg_init	t33_g14c_rinit
#define tisp_rcfg_module_register	t33_g14c_reg
#define private_vmalloc	t33_g14c_vmalloc
#define private_kmalloc	t33_g14c_kmalloc
#define private_spin_lock_init	t33_g14c_lock_init
#define isp_printf	t33_g14c_printf
#define tisp_bcsh_get_new_para	t33_g14c_new_para
#define tisp_bcsh_irq_frame_down	t33_g14c_fd_cb
#define system_irq_func_set	t33_g14c_irq
#include "tx_isp_t33_bcshN.h"
#include "tx_isp_t33_bcshH.h"
#include "tx_isp_t33_bcshO.h"
#undef bcshCfgMode
#undef tparamsP
#undef tisp_rcfg_bcsh_reg_init
#undef tisp_rcfg_module_register
#undef private_vmalloc
#undef private_kmalloc
#undef private_spin_lock_init
#undef isp_printf
#undef tisp_bcsh_get_new_para
#undef tisp_bcsh_irq_frame_down
#undef system_irq_func_set

/*
 * lib_tisp_init's tisp_bcsh_init (channel 0) on section 19 (copied into its tparamsP slot t33_bcsh_sec) and its CT
 * knots lut (lutbytes; the init's interp_by_ct).  A section the old builder refused stays refused (BCSH off).  The
 * init's tispBcshRefresh = 1 is the stream-on mark of T33_INIT_REFRESH, cleared as for the other blocks.
 */
static void t33_g14c_bcsh_init(const u8 *sec, const u8 *lut, u32 lutbytes)
{
	unsigned long flags;

	bcsh_para[0] = NULL;
	if (t33_le32(sec) != T33_BCSH_ID || sec[0x4a2] > 2 ||
	    (sec[0x4a2] == 1 && (!sec[0x4bf] || lutbytes < sec[0x4bf] * 40U)) || !t33_mod_buf(T33_MOD_BCSH))
		return;
	local_irq_save(flags);
	memcpy(t33_bcsh_sec, sec, T33_BCSH_SEC_BYTES);
	memcpy(bcsh_ap, t33_bcsh_ap_def, sizeof(bcsh_ap));
	t33_g14c_tp[0] = t33_bcsh_sec - 0x8b78;
	t33_g14c_lut = lut;
	t33_bcshN_init(0, (const u8 *)t33_st_isp_init_par_mode);
	t33_g14c_lut = NULL;
	tispBcshRefresh = 0;
	t33_g14c_runs[0]++;
	local_irq_restore(flags);
}

static void t33_g14c_trig(u32 ch, u32 mod)
{
	tisp_rcfg_module_cfg_trig(ch, mod);
}

static void t33_g14c_xform(u32 ch)
{
	tisp_params_transform(ch);
}

static void t33_g14c_wr(u32 ch, u32 mod, u32 slot, u32 val)
{
	tisp_reg_write_t33(ch, mod, slot, val);
}

static const struct t33_bcsh_fd_ops t33_g14c_fd_ops = {
	.trig = t33_g14c_trig, .xform = t33_g14c_xform, .dma = t33_g14_dma, .wr = t33_g14c_wr,
};

/* system irq 52 (interrupt context): tisp_bcsh_irq_frame_down */
static void t33_bcsh_fd(void)
{
	if (!bcsh_para[0])
		return;
	if (tispBcshRefresh == 1 || tispBcshRefresh == 2)
		t33_g14c_runs[tispBcshRefresh]++;
	t33_bcsh_frame_down(0, &t33_g14c_fd_ops, bcsh_frame_num, t33_st_isp_init_par_mode[226], &tispBcshRefresh,
			    bcshInfo, t33_bst_cfg_mode);
}

#endif /* TX_ISP_T33_G14GLUE_H */
