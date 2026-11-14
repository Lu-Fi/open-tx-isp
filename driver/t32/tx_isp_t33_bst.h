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
 *   slots and refresh = 1, else refresh = 2 (t33_bst_par_tail: the frame-done
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
static u8 t33_bst_cfg_mode = 1;			/* dpcCfgMode, ...: slot indexes */
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

/* tisp_rcfg_module_cfg_trig: start the module's list (its table word is set at stream-on) */
int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod)
{
	mod &= 0xff;
	wmb();
	isp_wr(mod < 32 ? T33_RCFG_TRIG : T33_RCFG_TRIG_HI, 1U << (mod & 31));
	t33_rcfg_runs++;
	return 0;
}

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

/* system irqs 38..56 of the blocks (interrupt context) */
static void t33_bst_irq(u32 msca)
{
	unsigned int b;

	for (b = 0; b < T33_BST_N; b++)
		if (msca & (1U << t33_bst_bit[b])) {
			t33_bst_fd(b);
			t33_bst_fd_runs[b]++;
		}
}

/*
 * The end of tisp_*_par_refresh, after the info struct is updated: the
 * slots now when the list is idle (refresh 1: the next frame done starts
 * it), else refresh 2 (the frame done writes them).
 */
static void t33_bst_par_tail(unsigned int b)
{
	unsigned long flags;

	local_irq_save(flags);
	t33_bst_refresh[b] = 2;
	t33_bst_fd(b);
	local_irq_restore(flags);
	t33_gr_runs[t33_bst_mod[b]]++;
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
}

#endif /* TX_ISP_T33_BST_H */
