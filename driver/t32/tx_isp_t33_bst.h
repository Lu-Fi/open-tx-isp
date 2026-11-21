/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) vendor-style block state (openimp #99): the per-block info
 * structs (ydnsInfo, sdnsInfo, cdnsInfo, dpcInfo, dmscInfo, yspInfo,
 * rdnsInfo), the tisp*Refresh bytes and the vendor's list callees
 * (tisp_reg_write_t33, tisp_rcfg_get_dma_state, tisp_rcfg_module_cfg_trig,
 * tisp_mdns_decode_reg_write), so the oracle-proven
 * tisp_*_t33_frame_done_interrupt ports (tx_isp_t33_fd_*.h) run as in the
 * vendor module:
 *
 * - tisp_*_par_refresh (AE event 4, t33_tgain_refresh): the block's info
 *   struct at the new gain, then, with the module's list idle, the list
 *   slots and refresh = 1, else refresh = 2 (the par_refresh ports in tx_isp_t33_g14glue.h; the frame-done
 *   handler's refresh-2 branch is the same write set);
 * - tisp_*_t33_frame_done_interrupt (system irqs 38, 39, 43, 51, 54, 55, 56
 *   = interrupt status 0x300a8 bits 6, 7, 11, 19, 22, 23, 24): refresh 1
 *   starts the list, refresh 2 writes the slots once the list is idle.
 *
 * Only the T33 build includes this file (tx_isp_t32_hw.c, TX_ISP_T33).
 * Lists are always in the configuration mode (*CfgMode = 1: slot indexes,
 * word index + 1 of the module page), stIspInitPar word 226 (byte 904) = 1.
 * Host test: tests/tx_isp_t33_bst_test.c (the frame-done writes from the
 * info structs give the init builders' lists at every gain).
 */
#ifndef TX_ISP_T33_BST_H
#define TX_ISP_T33_BST_H

#include "tx_isp_t33_fd_ydns.h"
#include "tx_isp_t33_fd_sdns.h"
#include "tx_isp_t33_fd_cdns.h"
#include "tx_isp_t33_fd_dpc.h"
#include "tx_isp_t33_fd_dmsc.h"
#include "tx_isp_t33_fd_ysp.h"
#include "tx_isp_t33_fd_rdns.h"

enum t33_bst_blk { T33_BST_DPC, T33_BST_RDNS, T33_BST_DMSC, T33_BST_YDNS, T33_BST_YSP, T33_BST_SDNS,
		   T33_BST_CDNS, T33_BST_N };

/* system irq - 32 (bit of T32_MSCADONE_STA) of each block's tisp_*_t33_frame_done_interrupt */
static const u8 t33_bst_bit[T33_BST_N] = { 6, 7, 11, 19, 22, 23, 24 };
static const u8 t33_bst_mod[T33_BST_N] = { T33_MOD_DPC, T33_MOD_RDNS, T33_MOD_DMSC, T33_MOD_YDNS,
					   T33_MOD_YSP, T33_MOD_SDNS, T33_MOD_CDNS };

static u8 t33_bst_refresh[T33_BST_N];		/* tispDpcRefresh, tispRdnsRefresh, ... */
static u8 t33_bst_cfg_mode = 1;
static u8 t33_bst_refresh_mdns;			/* tispMdnsRefresh (set by the CSCCR day path) */			/* dpcCfgMode, ...: slot indexes */
#define t33_bst_initpar t33_st_isp_init_par_mode	/* stIspInitPar (tx_isp_t32_hw.c) */
static u32 t33_ydns_inf[T33_YDNS_INFO_WORDS], t33_sdns_inf[T33_SDNS_INFO_WORDS];
static u32 t33_cdns_inf[T33_CDNS_INFO_WORDS];
u32 t33_bst_fd_runs[T33_BST_N], t33_bst_writes, t33_bst_bad;
extern u32 t33_gr_runs[];

/* tisp_reg_write_t33: word reg + 1 of the module's list page (direct register without the cfg mode) */
int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val)
{
	u32 *l;

	if (!t33_bst_cfg_mode) {
		isp_wr(reg, val);
		return 0;
	}
	mod &= 0xff;
	if (mod >= T33_MODULES || !(t33_mod_built & (1ULL << mod)) || !t33_mod_page[mod])
		return 0;
	l = (u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_mod_page[mod]));
	if (reg == 0x3fff || reg + 1 > l[0] || reg + 1 >= T33_SLOT_WORDS) {
		t33_bst_bad++;		/* the block code and the list layout disagree */
		return 0;
	}
	l[reg + 1] = val;
	t33_bst_writes++;
	return 0;
}

/* tisp_rcfg_get_dma_state: the module's list still runs */
int tisp_rcfg_get_dma_state(u32 mod)
{
	mod &= 0xff;
	return (isp_rd(mod < 32 ? T33_RCFG_TRIG : T33_RCFG_TRIG_HI) >> (mod & 31)) & 1;
}

/*
 * tisp_rcfg_module_cfg_trig (tx_isp_t33_rcfgcore.h): the module's table word from its record, then
 * its start bit; returns the 0x22014 read as the vendor.  A list that is not the registered one
 * (none yet): only the start bit, its table word is set at stream-on.
 */
int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod)
{
	mod &= 0xff;
	if (t33_rc_ok(mod)) {
		t33_rcfg_runs++;
		return (int)t33_rcfg_module_cfg_trig(&t33_rcc, 0, mod);
	}
	wmb();
	isp_wr(mod < 32 ? T33_RCFG_TRIG : T33_RCFG_TRIG_HI, 1U << (mod & 31));
	t33_rcfg_runs++;
	return 0;
}

/* tisp_reg_read_t33: word reg + 1 of the module's list page (0 when the list is not built) */
u32 tisp_reg_read_t33(u32 ch, u32 mod, u32 reg)
{
	const u32 *l;

	mod &= 0xff;
	if (mod >= T33_MODULES || !(t33_mod_built & (1ULL << mod)) || !t33_mod_page[mod] || reg + 1 >= T33_SLOT_WORDS)
		return 0;
	l = (const u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_mod_page[mod]));
	return l[reg + 1];
}

/* TOP helpers (block top): tisp_top_get_bypass reads the TOP list's bypass slot (ipcCfgMode 1: slot 1) */
u8 ipcCfgMode = 1;
static u32 t33_top_par_blk[11];			/* stTopPar[0]: 44 bytes, the 32 bypass bytes at +12 (tisp_top_set_bypass) */
u32 *stTopPar[256] = { t33_top_par_blk };

/*
 * tisp_top_reg_cfg for the bypass state only: the 32 bypass bytes of stTopPar[id] become the TOP list's bypass
 * word; the caller's tispTopRefresh (1 = idle: the list runs at the next null-down / frame end, 2 = it runs).  The
 * other TOP words stay as the IQ bin /
 * the last bin switch built them.
 */
void tisp_top_reg_cfg(u32 id)
{
	const u8 *p = (const u8 *)stTopPar[id & 0xff];
	u32 byp = 0, k;

	if (!p)
		return;
	for (k = 0; k < 32; k++)
		byp |= (u32)(p[12 + k] & 1) << k;
	tisp_reg_write_t33(id, 33, ipcCfgMode ? 1 : 64, byp);
}
#include "tx_isp_t33_top.h"

/* tisp_mdns_decode_reg_write (from the YSP frame done): par = yspInfo + 1068 */
static void t33_bst_mdns_decode(u32 ch, const u8 *par)
{
	if (!(t33_mod_built & (1ULL << T33_MOD_MDNS)) || !t33_mod_page[T33_MOD_MDNS])
		return;
	t33_mdns_decode((const u32 *)(par - 1068),
			(u32 *)CKSEG1ADDR(virt_to_phys((void *)t33_mod_page[T33_MOD_MDNS])));
}

static int t33_bst_wr_i(u32 ch, u32 mod, u32 reg, u32 val)
{
	return tisp_reg_write_t33(ch, mod, reg, val);
}

static void t33_bst_wr_v(u32 ch, u32 mod, u32 reg, u32 val)
{
	tisp_reg_write_t33(ch, mod, reg, val);
}

static int t33_bst_trig_i(u32 ch, u32 mod)
{
	return tisp_rcfg_module_cfg_trig(ch, mod);
}

static void t33_bst_trig_v(u32 ch, u32 mod)
{
	tisp_rcfg_module_cfg_trig(ch, mod);
}

static u32 t33_bst_dma_u(u32 mod)
{
	return (u32)tisp_rcfg_get_dma_state(mod);
}

static const struct t33_ydns_fd_ops t33_bst_ydns_ops = { t33_bst_wr_v, tisp_rcfg_get_dma_state, t33_bst_trig_v };
static const struct t33_sdns_fd_ops t33_bst_sdns_ops = { t33_bst_wr_i, tisp_rcfg_get_dma_state, t33_bst_trig_i };
static const struct t33_cdns_ops t33_bst_cdns_ops = { t33_bst_wr_i, tisp_rcfg_get_dma_state, t33_bst_trig_i };
static const struct t33_dpc_hw t33_bst_dpc_ops = { t33_bst_wr_i, tisp_rcfg_get_dma_state, t33_bst_trig_i };
static const struct t33_ysp_ops t33_bst_ysp_ops = { t33_bst_wr_v, t33_bst_mdns_decode, t33_bst_trig_v, t33_bst_dma_u };

/* the info struct pointers per channel (one channel) */
static u8 *t33_bst_info(unsigned int b)
{
	switch (b) {
	case T33_BST_DPC:	return (u8 *)t33_dpc_v;
	case T33_BST_RDNS:	return (u8 *)t33_rdns_v;
	case T33_BST_DMSC:	return (u8 *)t33_dmsc_v;
	case T33_BST_YDNS:	return (u8 *)t33_ydns_inf;
	case T33_BST_YSP:	return (u8 *)t33_ysp_v;
	case T33_BST_SDNS:	return (u8 *)t33_sdns_inf;
	default:		return (u8 *)t33_cdns_inf;
	}
}

/* the block's tisp_*_t33_frame_done_interrupt (channel 0) */
static void t33_bst_fd(unsigned int b)
{
	u8 *inf[4] = { t33_bst_info(b), NULL, NULL, NULL };
	u8 *rf = &t33_bst_refresh[b];
	u32 ip = t33_bst_initpar[226];

	switch (b) {
	case T33_BST_DPC:
		t33_dpc_frame_done(&t33_bst_dpc_ops, 0, ip, rf, t33_bst_cfg_mode, inf[0]);
		break;
	case T33_BST_RDNS:
		t33_rdns_frame_done(0, t33_bst_initpar, rf, t33_bst_cfg_mode, inf);
		break;
	case T33_BST_DMSC:
		t33_dmsc_frame_done(0, ip, inf, &t33_bst_cfg_mode, rf);
		break;
	case T33_BST_YDNS:
		t33_ydns_frame_done(0, ip, rf, t33_bst_cfg_mode, inf[0], &t33_bst_ydns_ops);
		break;
	case T33_BST_YSP:
		t33_ysp_frame_done(&t33_bst_ysp_ops, 0, ip, rf, t33_bst_cfg_mode, inf);
		break;
	case T33_BST_SDNS:
		t33_sdns_frame_done(0, t33_bst_initpar, rf, t33_bst_cfg_mode, (const u8 *const *)inf,
				    &t33_bst_sdns_ops);
		break;
	default:
		t33_cdns_frame_done(&t33_bst_cdns_ops, 0, ip, rf, t33_bst_cfg_mode, inf);
		break;
	}
}

/*
 * BLC (openimp #99 group 13a): the vendor's blc_para[0] (struct t33_blc_info of tx_isp_t33_blcsw.h, oracle
 * blocks blcsw / blcwr / irq) instead of a list rebuilt from the section:
 * - tparamsP's BLC section (+17640) is t33_blc_sec, the bin's section copied at every bin parse (init, day /
 *   night) like the vendor's tisp_bin_data_parse into its own parameter struct; tisp_blc_interp_by_tgain
 *   writes into it (bytes 129 / 130 at the init, the u16 levels at +118), never into libimp's bin buffer;
 * - tisp_blc_init (t33_blc_attach: stIspInitPar word 1 bayer, word 232 the init gain), tisp_blc_tgain_update
 *   (t33_blc_gain, every gain refresh), tisp_blc_switch_para (t33_blc_switch);
 * - tisp_blc_write_reg: the slots of list 4 (blcCfgMode = t33_bst_cfg_mode) and tispBlcRefresh
 *   (t33_blc_refresh); tisp_blc_t33_frame_done_interrupt (system irq 40, interrupt status 0x300a8 bit 8):
 *   refresh 1 starts list 4, refresh 2 writes the slots once it is idle.
 * The list layout itself still comes from the init builder (t33_blc_init, the same words; tisp_rcfg reg_init
 * checks it).  The blc_ip / cp / sp / ap defaults are zero: every byte tisp_blc_init copies from them is
 * written again before it is read (sp is not read by any BLC function of the module).
 */
#include "tx_isp_t33_blcsw.h"
#define T33_TP_OFF_BLC		17640U
#define T33_BLC_BIT		8	/* system irq 40 */
static struct t33_blc_info t33_blc_blk, *blc_para[4];
static u8 t33_blc_sec[T33_BLC_SEC_BYTES];	/* tparamsP[0] + 17640 */
static u8 t33_blc_refresh;			/* tispBlcRefresh */
static u32 t33_blc_isp[233];			/* the stIspInitPar words tisp_blc_init reads */
static const u8 t33_blc_dflt[44];
u32 t33_blc_runs[4];				/* init, gain updates, switches, frame dones with a refresh */

static const struct t33_blc_hw t33_blc_hw = { tisp_rcfg_get_dma_state, tisp_reg_write_t33, tisp_rcfg_module_cfg_trig };

static void tisp_blc_write_reg(u32 ch)
{
	if (blc_para[ch & 3])
		t33_blc_write_reg(ch, blc_para[ch & 3], t33_bst_cfg_mode, &t33_blc_refresh, &t33_blc_hw);
}

static int t33_blc_reg0(u32 ch)
{
	(void)ch;
	return 0;	/* list 4 is registered and built (t33_mod_set_tmpl) before */
}

static void *t33_blc_alloc(u32 size)
{
	(void)size;
	memset(&t33_blc_blk, 0, sizeof(t33_blc_blk));
	return &t33_blc_blk;
}

static void t33_blc_log(u32 line, int val)
{
	isp_printf(2, "[ISP][tisp_blc_init, %u]: error %d\n", line, val);
}

static void t33_blc_irq_set(u32 ch)
{
	(void)ch;	/* irq 40 is dispatched by t33_bst_irq */
}

static const struct t33_blc_ops t33_blc_ops = {
	tisp_blc_write_reg, t33_blc_reg0, t33_blc_alloc, t33_blc_log, t33_blc_irq_set,
	t33_blc_dflt, t33_blc_dflt, t33_blc_dflt, t33_blc_dflt,
};

static u8 *t33_blc_tparams(const u8 *sec)
{
	memcpy(t33_blc_sec, sec, T33_BLC_SEC_BYTES);
	return t33_blc_sec - T33_TP_OFF_BLC;
}

/* tisp_blc_init (channel 0) on the bin's BLC section, list 4 built */
static void t33_blc_attach(const u8 *sec, u32 bayer, u32 gain)
{
	unsigned long flags;
	u8 *tp = t33_blc_tparams(sec);

	t33_blc_isp[1] = bayer;
	t33_blc_isp[232] = gain;
	local_irq_save(flags);
	blc_para[0] = NULL;
	t33_blc_refresh = 0;
	t33_blc_init_ch(0, (const u8 *)t33_blc_isp, tp, &blc_para[0], &t33_blc_ops);
	local_irq_restore(flags);
	t33_blc_runs[0]++;
}

/* tisp_blc_tgain_update */
static void t33_blc_gain(u32 gain)
{
	unsigned long flags;

	if (!blc_para[0])
		return;
	local_irq_save(flags);
	t33_blc_tgain_update(0, blc_para[0], gain, tisp_blc_write_reg);
	local_irq_restore(flags);
	t33_blc_runs[1]++;
	t33_gr_runs[T33_MOD_BLC]++;
}

/* tisp_blc_switch_para on the new bin's section */
static void t33_blc_switch(const u8 *sec)
{
	unsigned long flags;
	u8 *tp;

	if (!blc_para[0])
		return;
	local_irq_save(flags);
	tp = t33_blc_tparams(sec);
	t33_blc_switch_para(0, blc_para[0], tp, tisp_blc_write_reg);
	local_irq_restore(flags);
	t33_blc_runs[2]++;
}

/*
 * LSC (openimp #99 group 13b): the vendor's lsc_para[0] (0xc4 bytes, tisp_lsc_init's layout) instead of the
 * init builder's private state:
 * - word 0 the section in tparamsP (+17488, t33_lsc_sec: the bin's section copied at every bin parse), word 1
 *   the 0x20-byte parameters (0 / 1 sensor height / width, 2 CT, 3 the last CT zone, 5 gain), word 2 the 0x34-byte
 *   mesh info (0 cells, 4 stride, 8..11 remainders / rows / cols, 0x10 / 0x12 / 0x13 CT / gain / last strength,
 *   0x14 the shift, 0x15 valid, 0x18 the table CTs, 0x1c..0x30 the six planes = t33_lsc_m's), word 3 a 0xc-byte
 *   block, words 4 / 5 the mesh table (tisp_lsc_malloc_info: virtual / physical address; here the driver's DMA
 *   pages, written through KSEG1), byte 0x18 the pending-rewrite flag of the frame done;
 * - tisp_lsc_init (t33_lsc_attach): tisp_lsc_lut_valid_check (t33_lsc_mesh_check), interp_by_tgain
 *   (t33_lsc_interp_by_tgain_v), interp_by_ct (t33_lsc_ct_info, tx_isp_t33_lscv.h), func_lsc_para_update, tisp_lsc_write_reg (the
 *   list 6 slots, tispLscRefresh), the two init stores 0x504c / 0x5010; tisp_lsc_switch_para (tx_isp_t33_swmid.h)
 *   on the same block at day / night;
 * - tisp_lsc_irq_frame_done (system irq 36, interrupt status bit 4): refresh 1 starts list 6, refresh 2 writes it.
 * tisp_lsc_interp_by_ct is t33_lsc_ct of tx_isp_t33_lsc.h (the QEMU-proven init builder's CT blend, strength,
 * distance and packing, openimp #388) on the mesh info's fields; it is not an oracle-proven port (its callees
 * tisp_look_up_ct_list / lut_update_by_strength / _by_distance are).  tisp_lsc_tgain_update / ct_update are not
 * wired: the PRJ008 top bypass bit 0x08 skips both (openimp #537).  The list layout itself still comes from the
 * init builder (t33_lsc_init, the same words; tisp_rcfg reg_init checks it).
 */
#include "tx_isp_t33_lscv.h"
#include "tx_isp_t33_lscmf.h"
#define T33_LSC_IRQ_BIT		4	/* system irq 36 */
static struct t33_lsc_vblk t33_lsc_b;
static u32 *lsc_para[4];
static u8 t33_lsc_sec[T33_LSC_SEC_BYTES];	/* tparamsP[0] + 17488 */
static u8 t33_lsc_refresh;			/* tispLscRefresh */
static u32 t33_lsc_count[4];			/* lsc_count */
static const u8 *t33_lsc_tab;			/* stLutInfo[0] + 0x14: the CT tables */
u32 t33_lsc_runs[3];				/* init, switches, frame dones with a refresh */
u32 t33_lsc_flips;				/* tisp_lsc_mirror_flip runs (t33_lsc_hvflip, openimp #727) */

static void t33_lsc_wr(u32 ch, u32 blk, u32 sel, u32 val)
{
	tisp_reg_write_t33(ch, blk, sel, val);
}

/* tisp_lsc_write_reg(ch, mode) */
static void t33_lsc_write_reg(u32 ch, u32 mode)
{
	(void)mode;
	if (lsc_para[ch & 3])
		t33_lsc_refresh = (u8)t33_lsc_write_regs(lsc_para[ch & 3], ch, t33_bst_cfg_mode, t33_lsc_wr,
							   tisp_rcfg_get_dma_state);
}

/* tisp_lsc_init (channel 0): section sec / CT tables tab of the bin, sensor h x w, gain 16.16, ct in K */
static void t33_lsc_attach(const u8 *sec, const u8 *tab, u32 tabbytes, u32 h, u32 w, u32 gain, u32 ct)
{
	unsigned long flags;
	int rc;

	if (!t33_lsc_pages)
		return;
	local_irq_save(flags);
	lsc_para[0] = NULL;
	t33_lsc_refresh = 0;
	memcpy(t33_lsc_sec, sec, sizeof(t33_lsc_sec));
	t33_lsc_tab = tab;
	/* tisp_lsc_malloc_info: the mesh table = the driver's DMA pages, written through KSEG1 */
	rc = t33_lsc_vinit(&t33_lsc_b, &t33_lsc_m, t33_lsc_sec, tab, tabbytes, h, w, gain, ct,
			   (u8 *)CKSEG1ADDR(virt_to_phys((void *)t33_lsc_pages)), (u32)virt_to_phys((void *)t33_lsc_pages));
	if (!rc) {
		lsc_para[0] = t33_lsc_b.blk;
		t33_lsc_write_reg(0, 0);
		tisp_reg_write_t33(0, 6, t33_bst_cfg_mode ? 6 : 0x504c, 1);
		tisp_reg_write_t33(0, 6, t33_bst_cfg_mode ? 1 : 0x5010, 1);
	}
	local_irq_restore(flags);
	if (rc)
		isp_printf(1, "tx-isp-t33: LSC tables do not fit, block not attached\n");
	else
		t33_lsc_runs[0]++;
}

/*
 * CSCCR day / night (openimp #507): lib_tisp_set_csccr from SET_TIZIANO_PAR
 * (update_csccr) and tisp_csccr_t33_frame_done_interrupt (system irq 57,
 * block fd-csccr), with the vendor's glue (tx_isp_t33_dnglue.h, block
 * dnglue).  The MDNS UV limit bytes come from the MDNS block state
 * (t33_mdns_v = mdnsInfo[0], bytes 0x140c / 0x1410 / 0x1414) like the vendor.
 */
#define T33_CSCCR_BIT		25	/* system irq 57 */
static u8 t33_csccr_refresh;			/* tispCsccrRefresh */
static u32 t33_csccr_cnt[1], t33_csccr_cur[1] = { ~0U }, t33_csccr_cur2[1] = { ~0U };	/* frameCount, curFrame(2) */
static u8 t33_dn_uv_limit[1], t33_dn_init_finish[1] = { 1 }, t33_dn_start_night[1];	/* uvLimitFlag, ... */
u32 t33_csccr_fd_runs;

#include "tx_isp_t33_dnglue.h"

static const struct t33_dn_state t33_dn_st = {
	&t33_bst_cfg_mode, &t33_csccr_refresh, &t33_bst_cfg_mode, &t33_bst_refresh_mdns, t33_dn_uv_limit,
	t33_dn_init_finish, t33_dn_start_night, (u8 *)t33_bst_initpar, t33_csccr_cnt, t33_csccr_cur, t33_csccr_cur2,
};

static void tisp_csccr_set_day(u32 ch)
{
	t33_csccr_set_day(&t33_dn_st, ch);
}

static void tisp_mdns_dn_params_api2(u32 ch)
{
	t33_mdns_dn_api2(&t33_dn_st, ch, (const u8 *)t33_mdns_v);
}

static void tisp_mdns_dn_params_api(u32 ch)
{
	t33_mdns_dn_api(&t33_dn_st, ch, (const u8 *)t33_mdns_v);
}

#include "tx_isp_t33_fd_csccr.h"

/* lib_tisp_set_csccr with ispInitFinish (the ISP streams): night 0 day, 1 night */
void t33_set_csccr(u32 ch, u32 night)
{
	unsigned long flags;

	if (ch)
		return;
	local_irq_save(flags);
	t33_lib_set_csccr(&t33_dn_st, ch, night);
	local_irq_restore(flags);
}

/* system irqs 38..57 of the blocks (interrupt context) */
static void t33_bst_irq(u32 msca)
{
	unsigned int b;

	for (b = 0; b < T33_BST_N; b++)
		if (msca & (1U << t33_bst_bit[b])) {
			t33_bst_fd(b);
			t33_bst_fd_runs[b]++;
		}
	if (msca & (1U << T33_BLC_BIT)) {
		if (t33_blc_refresh)
			t33_blc_runs[3]++;
		t33_blc_frame_done_st(0, (const u8 *)t33_bst_initpar, &t33_blc_refresh, tisp_rcfg_module_cfg_trig,
				      tisp_blc_write_reg);
	}
	if ((msca & (1U << T33_LSC_IRQ_BIT)) && lsc_para[0]) {
		if (t33_lsc_refresh)
			t33_lsc_runs[2]++;
		t33_lsc_frame_done_v(0, lsc_para, t33_lsc_count, (const u8 *)t33_bst_initpar, &t33_lsc_refresh,
				     tisp_rcfg_module_cfg_trig, t33_lsc_write_reg);
	}
	if (msca & (1U << T33_CSCCR_BIT)) {
		t33_csccr_frame_done(0, (const u8 *)t33_bst_initpar, &t33_csccr_refresh, t33_csccr_cnt,
				     t33_csccr_cur, t33_csccr_cur2);
		t33_csccr_fd_runs++;
	}
}

/* before an info struct is rewritten: no frame done may read it half way (set again by the tail) */
static void t33_bst_hold(unsigned int b)
{
	unsigned long flags;

	local_irq_save(flags);
	if (t33_bst_refresh[b] == 2)
		t33_bst_refresh[b] = 0;
	local_irq_restore(flags);
}

/* stream-on: the lists run once more at the first frame end (T33_INIT_REFRESH), no refresh pending */
static void t33_bst_reset(void)
{
	memset(t33_bst_refresh, 0, sizeof(t33_bst_refresh));
	t33_bst_initpar[226] = 1;
	((u8 *)t33_bst_initpar)[T33_INITPAR_DAY_FLAG] = 0;
	t33_csccr_refresh = t33_bst_refresh_mdns = 0;
	t33_csccr_cnt[0] = 0;
	t33_csccr_cur[0] = t33_csccr_cur2[0] = ~0U;
}

#endif /* TX_ISP_T33_BST_H */
