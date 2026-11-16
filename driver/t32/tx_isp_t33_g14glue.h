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
u32 t33_g14_runs[13];	/* + [7] dmsc init, [8] dmsc refreshes past the gate, [9..12] rdns / ysp the same */
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

#endif /* TX_ISP_T33_G14GLUE_H */
