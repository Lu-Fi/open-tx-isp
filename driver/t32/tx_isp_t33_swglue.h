/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) vendor-name glue for the oracle-proven day/night switch_para
 * ports (openimp #99, #504): tx_isp_t33_swsmall.h (tisp_sdns_switch_para,
 * tisp_ydns_switch_para, tisp_dpc_switch_para) run on the driver's block
 * state of tx_isp_t33_bst.h:
 *
 * - sdnsInfo / ydnsInfo / dpcInfo: t33_sdns_inf / t33_ydns_inf / t33_dpc_v
 *   (vendor word layout); the par refresh keeps the words the switch reads:
 *   sdnsInfo 0x24 / 0x25 (section, gain), ydnsInfo 24 / 26, dpcInfo 170 / 171
 *   (t33_swg_*_gain);
 * - tparamsP: the vendor's parameter struct holds the sections at fixed
 *   offsets, our bin keeps them in the file layout, so tparamsP[0] is set
 *   before each call such that tparamsP + vendor offset = our section;
 * - <blk>CfgMode = t33_bst_cfg_mode (slot indexes), tisp<Blk>Refresh =
 *   t33_bst_refresh[] (the frame-done handlers of tx_isp_t33_bst.h start the
 *   list at refresh 1 and write the slots at refresh 2);
 * - func_*_para_update / *_bin_para_update: the driver's proven par helpers
 *   (tx_isp_t33_blk.h, tx_isp_t33_dpc.h) on the info words.
 *
 * Only tx_isp_t32_hw.c (TX_ISP_T33) includes this file, after tx_isp_t33_bst.h.
 */
#ifndef TX_ISP_T33_SWGLUE_H
#define TX_ISP_T33_SWGLUE_H

#define T33_SW_GLUE	1

#define sdnsCfgMode	t33_bst_cfg_mode
#define ydnsCfgMode	t33_bst_cfg_mode
#define dpcCfgMode	t33_bst_cfg_mode
#define tispSdnsRefresh	t33_bst_refresh[T33_BST_SDNS]
#define tispYdnsRefresh	t33_bst_refresh[T33_BST_YDNS]
#define tispDpcRefresh	t33_bst_refresh[T33_BST_DPC]
#define rdnsCfgMode	t33_bst_cfg_mode
#define tispRdnsRefresh	t33_bst_refresh[T33_BST_RDNS]

/* vendor tparams offsets of the sections the ports address through tparamsP */
#define T33_TP_OFF_SDNS	0x9b20U
#define T33_TP_OFF_YDNS	0x88acU

static u32 *sdnsInfo[4] = { t33_sdns_inf }, *ydnsInfo[4] = { t33_ydns_inf }, *dpcInfo[4] = { t33_dpc_v };
static u32 *rdnsInfo[4] = { t33_rdns_v };
static u32 *tparamsP[4];
static const u8 *t33_swg_dpc_sec;		/* tisp_dpc_bin_para_update's section (tparamsP + dpc offset) */
u32 t33_swg_runs[6];				/* switch_para calls: sdns, ydns, dpc, cdns, rdns; rdns ev updates */

static inline u32 tisp_simple_intp_uint8_t(int idx, u32 frac, const u8 *tab)
{
	return t32_tp_intp_u8((u32)idx, frac, tab);
}

static inline u32 tisp_simple_intp_uint16_t(int idx, u32 frac, const u8 *tab)
{
	return t32_tp_intp_u16((u32)idx, frac, tab);
}

static inline const u8 *t33_swg_ptr(u32 w)
{
	return (const u8 *)(unsigned long)w;
}

static inline void tisp_sdns_other_params_transform(u32 ch)
{
	t33_sdns_other_params(t33_swg_ptr(sdnsInfo[ch][0x24]), sdnsInfo[ch]);
}

static inline void func_sdns_para_update(u32 ch)
{
	u32 *inf = sdnsInfo[ch], s[4];

	t33_sdns_strength(inf, s);
	t33_sdns_copy(inf, inf, s);
}

static inline void func_ydns_bin_para_update(u32 ch)
{
	ydnsInfo[ch][0] = t33_swg_ptr(ydnsInfo[ch][24])[4];
}

static inline void func_ydns_para_update(u32 ch)
{
	unsigned int k;

	for (k = 0; k < 12; k++)
		ydnsInfo[ch][12 + k] = ydnsInfo[ch][k];
}

static inline void tisp_dpc_bin_para_update(u32 ch)
{
	u32 *v = dpcInfo[ch];

	t33_vb_bin(v, t33_swg_dpc_sec, t33_dpc_bin, ARRAY_SIZE(t33_dpc_bin));
	v[170] = (u32)(unsigned long)t33_swg_dpc_sec;
}

/* func_dpc_para_update + func_ctr_para_update + func_cac_para_update: tisp_dpc_init's para, then the copies */
static inline void func_dpc_para_update(u32 ch)
{
	t33_dpc_para(dpcInfo[ch]);
}

static inline void func_ctr_para_update(u32 ch)
{
	(void)ch;
}

static inline void func_cac_para_update(u32 ch)
{
	t33_vb_copy(dpcInfo[ch], t33_dpc_copy, ARRAY_SIZE(t33_dpc_copy));
}

/* tisp_reg_read_t33(BLC / DG list, slot 3) of func_rdns_para_update: our lists' word 4 */
static inline void t33_swg_blc_dg(u32 *blc, u32 *dg)
{
	u32 *b;

	*blc = *dg = 0;
	if ((t33_mod_built & (1ULL << T33_MOD_BLC)) && (b = t33_mod_buf(T33_MOD_BLC)))
		*blc = b[4];
	if ((t33_mod_built & (1ULL << T33_MOD_DG)) && (b = t33_mod_buf(T33_MOD_DG)))
		*dg = b[4];
}

static inline u32 tisp_ev_intp_uint8_t(u32 ev, const u8 *e, const u8 *t)
{
	return t33_ev_intp_u8(ev, e, t);
}

static inline void func_rdns_bin_para_update(u32 ch)
{
	t33_rdns_bin_para_update(rdnsInfo[ch], t33_swg_ptr(rdnsInfo[ch][T33_RDNS_SEC_WORD]));
}

static inline void func_rdns_para_update(u32 ch)
{
	u32 blc, dg;

	t33_swg_blc_dg(&blc, &dg);
	t33_rdns_para_update(rdnsInfo[ch], blc, dg);
}

static void t33_swg_wr_v(u32 ch, u32 mod, u32 reg, u32 val)
{
	tisp_reg_write_t33(ch, mod, reg, val);
}

static void t33_swg_cdns_para(u32 ch)
{
	(void)ch;
	t33_cdns_para_update(t33_cdns_inf);	/* func_cdns_para_update */
}

#include "tx_isp_t33_swsmall.h"
#include "tx_isp_t33_swcdns.h"
#include "tx_isp_t33_swrdns.h"
#include "tx_isp_t33_rdnsEv.h"

/* the gain and section words a par refresh / init leaves in the info structs (read by the switch) */
static inline void t33_swg_sdns_gain(const u8 *sec, u32 gain)
{
	t33_sdns_inf[0x24] = (u32)(unsigned long)sec;
	t33_sdns_inf[0x25] = gain;
}

static inline void t33_swg_ydns_gain(const u8 *sec, u32 gain)
{
	t33_ydns_inf[24] = (u32)(unsigned long)sec;
	t33_ydns_inf[26] = gain;
}

/* cdnsInfo bytes 432 / 436, rdnsInfo words 208..210 */
static inline void t33_swg_cdns_gain(const u8 *sec, u32 gain)
{
	t33_cdns_inf[T33_CDNS_SEC_PTR_OFF / 4] = (u32)(unsigned long)sec;
	t33_cdns_inf[T33_CDNS_GAIN_OFF / 4] = gain;
}

static inline void t33_swg_rdns_gain(const u8 *sec, u32 gain, u32 ev)
{
	t33_rdns_v[T33_RDNS_SEC_WORD] = (u32)(unsigned long)sec;
	t33_rdns_v[T33_RDNS_EV_GAIN_WORD] = gain;
	t33_rdns_v[T33_RDNS_EV_WORD] = ev;
}

static inline void t33_swg_dpc_gain(const u8 *sec, u32 gain)
{
	t33_dpc_v[170] = (u32)(unsigned long)sec;
	t33_dpc_v[171] = gain;
}

/*
 * lib_tisp_refresh_para's dpc / ydns / sdns part: each block's switch_para on
 * the new bin at the block's own last gain (info words above).  Returns the
 * mask of switched modules (the forced gain refresh skips them).
 */
static u64 t33_swg_small(const u8 *bin, u32 bytes)
{
	unsigned long flags;
	u32 off, len;
	u64 done = 0;

	off = t32_iq_section(bin, bytes, T33_SEC_DPC, &len);
	if (off && len == T33_DPC_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_DPC))) {
		t33_bst_hold(T33_BST_DPC);
		local_irq_save(flags);
		t33_swg_dpc_sec = bin + off;
		t33_dpc_switch(0);
		local_irq_restore(flags);
		t33_swg_runs[2]++;
		done |= 1ULL << T33_MOD_DPC;
	}
	off = t32_iq_section(bin, bytes, T33_SEC_YDNS, &len);
	if (off && len == T33_YDNS_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_YDNS))) {
		t33_bst_hold(T33_BST_YDNS);
		local_irq_save(flags);
		tparamsP[0] = (u32 *)((unsigned long)(bin + off) - T33_TP_OFF_YDNS);
		t33_ydns_switch(0);
		local_irq_restore(flags);
		t33_swg_runs[1]++;
		done |= 1ULL << T33_MOD_YDNS;
	}
	off = t32_iq_section(bin, bytes, T33_SEC_SDNS, &len);
	if (off && len == T33_SDNS_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_SDNS))) {
		t33_bst_hold(T33_BST_SDNS);
		local_irq_save(flags);
		tparamsP[0] = (u32 *)((unsigned long)(bin + off) - T33_TP_OFF_SDNS);
		t33_sdns_switch(0);
		local_irq_restore(flags);
		t33_swg_runs[0]++;
		done |= 1ULL << T33_MOD_SDNS;
	}
	off = t32_iq_section(bin, bytes, T32_SEC_CDNS, &len);
	if (off && len == T33_CDNS_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_CDNS))) {
		t33_bst_hold(T33_BST_CDNS);
		local_irq_save(flags);
		t33_bst_refresh[T33_BST_CDNS] = (u8)t33_cdns_switch(0, t33_cdns_inf,
			(const u8 *)((unsigned long)(bin + off) - T33_CDNS_SEC_OFF), t33_bst_cfg_mode,
			tisp_rcfg_get_dma_state, t33_swg_wr_v, t33_swg_cdns_para);
		local_irq_restore(flags);
		t33_swg_runs[3]++;
		done |= 1ULL << T33_MOD_CDNS;
	}
	/* the vendor order puts rdns after lce: a separate call (t33_swg_rdns) */
	return done;
}

/* tisp_rdns_switch_para: func_rdns_bin_para_update reads the section through rdnsInfo word 208 */
static u64 t33_swg_rdns(const u8 *bin, u32 bytes)
{
	unsigned long flags;
	u32 off, len;

	off = t32_iq_section(bin, bytes, T33_SEC_RDNS, &len);
	if (!off || len != T33_RDNS_SEC_BYTES || !(t33_mod_built & (1ULL << T33_MOD_RDNS)))
		return 0;
	t33_bst_hold(T33_BST_RDNS);
	local_irq_save(flags);
	t33_rdns_v[T33_RDNS_SEC_WORD] = (u32)(unsigned long)(bin + off);
	t33_rdns_switch_para(0);
	local_irq_restore(flags);
	t33_swg_runs[4]++;
	return 1ULL << T33_MOD_RDNS;
}

/*
 * tisp_rdns_ev_update (top bypass bit 20 clear, every EV event): the EV, the bin bytes, the EV and gain
 * interpolations, func_rdns_para_update, then the 36 slot writes with the list idle (refresh 1) or
 * refresh 2 (the frame done writes them).
 */
static void t33_swg_rdns_ev(u32 ev)
{
	struct t33_rdns_ev_wr w[T33_RDNS_EV_NWR];
	unsigned long flags;
	const u8 *sec;
	unsigned int k, n;
	int busy;

	if (!(t33_mod_built & (1ULL << T33_MOD_RDNS)) || !t33_rdns_v[T33_RDNS_SEC_WORD])
		return;
	t33_bst_hold(T33_BST_RDNS);
	local_irq_save(flags);
	sec = t33_swg_ptr(t33_rdns_v[T33_RDNS_SEC_WORD]);
	t33_rdns_v[T33_RDNS_EV_WORD] = ev;
	func_rdns_bin_para_update(0);
	t33_rdns_ev_interp(t33_rdns_v, sec);
	func_rdns_para_update(0);
	busy = tisp_rcfg_get_dma_state(T33_MOD_RDNS);
	if (!busy) {
		n = t33_rdns_ev_writes(t33_rdns_v, t33_bst_cfg_mode, w);
		for (k = 0; k < n; k++)
			tisp_reg_write_t33(0, T33_MOD_RDNS, w[k].sel, w[k].val);
	}
	t33_bst_refresh[T33_BST_RDNS] = t33_rdns_ev_flag(busy);
	local_irq_restore(flags);
	t33_swg_runs[5]++;
}

#endif /* TX_ISP_T33_SWGLUE_H */
