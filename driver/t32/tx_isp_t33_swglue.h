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
 *   (tx_isp_t33_blk.h) on the info words; the DPC / CTR / CAC ones the vendor-form
 *   ports of tx_isp_t33_dpcpar.h (oracle dpcpar, group 13c) on the section copy
 *   t33_dpc_sec (the vendor's tparamsP + 0x3e2c); the gain update is the proven
 *   tisp_dpc_l_gain_update (t33_dpc_l_gain_update, oracle sw-small).
 *
 * - mdns (tx_isp_t33_swmdns.h): mdnsInfo = t33_mdns_v (word 2180 section,
 *   2181 gain, 2190 the compression flag), the five 11-byte threshold tables
 *   of the new section in t33_swg_mdns_g, stMemInfo without WDR, the slot
 *   writes into list 15 and tisp_mdns_t33_reg_trig (run at the next frame).
 *
 * Only tx_isp_t32_hw.c (TX_ISP_T33) includes this file, after tx_isp_t33_bst.h.
 */
#ifndef TX_ISP_T33_SWGLUE_H
#define TX_ISP_T33_SWGLUE_H

#include "tx_isp_t33_dpcpar.h"

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
static u8 t33_dpc_sec[T33_DPC_SEC_BYTES];	/* the DPC section (the vendor: tparamsP + 0x3e2c), dpcInfo word 170 */
/* YDNS / SDNS / CDNS sections (tparamsP + 0x88ac / 0x9b20 / 0x892c; group 14, tx_isp_t33_g14glue.h) */
static u8 t33_ydns_sec[T33_YDNS_SEC_BYTES], t33_sdns_sec[T33_SDNS_SEC_BYTES], t33_cdns_sec[T33_CDNS_SEC_BYTES];
static u8 t33_dmsc_sec[T33_DMSC_SEC_BYTES];	/* tparamsP + 17900 (group 14b-2) */
#define T33_TP_OFF_RDNS	16520U			/* tparamsP + this = RDNS section (rdnsInfo word 208) */
static u8 t33_rdns_sec[T33_RDNS_SEC_BYTES];	/* g14-1 */
static u8 t33_ysp_sec[T33_YSP_SEC_BYTES];	/* tparamsP + 0x907c (g14-1) */
u32 t33_dpc_gains;				/* tisp_dpc_l_gain_update calls (group 13c) */
u32 t33_swg_runs[9];				/* switch_para calls: sdns, ydns, dpc, cdns, rdns, rdns ev, dmsc, ysp, mdns */

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

/* the DPC / CTR / CAC parameter updates on dpcInfo (tx_isp_t33_dpcpar.h, group 13c): word 170 = t33_dpc_sec */
static inline void tisp_dpc_bin_para_update(u32 ch)
{
	t33_dpc_bin_para_v(dpcInfo[ch]);
}

static inline void func_dpc_para_update(u32 ch)
{
	t33_dpc_para_v(dpcInfo[ch]);
}

static inline void func_ctr_para_update(u32 ch)
{
	t33_ctr_para_v(dpcInfo[ch]);
}

static inline void func_cac_para_update(u32 ch)
{
	t33_cac_para_v(dpcInfo[ch]);
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
#include "tx_isp_t33_swdmsc.h"
#include "tx_isp_t33_swysp.h"
#include "tx_isp_t33_swmdns.h"

/* ---- dmsc / ysp switch_para (ops-struct ports) ---- */
#define T33_TP_OFF_DMSC		17900U		/* tparamsP + this = DMSC section */
#define T33_DMSC_SEC_WORD	670		/* dmscInfo: section, gain, sharpness (byte 2688, 0x80 = 1.0) */
#define T33_DMSC_GAIN_WORD	671
#define T33_DMSC_SHARP_WORD	672
#define T33_YSP_SEC_WORD	(2172 / 4)	/* yspInfo: section, gain */
#define T33_YSP_GAIN_WORD	(2176 / 4)

static u8 t33_swg_ysp_par[T33_YSP_SEC_LEN];	/* ysp_paramsP[0]: the section copy */

static u32 t33_swg_i8(u32 i, u32 f, const u8 *t)
{
	return t32_tp_intp_u8(i, f, t);
}

static u32 t33_swg_i16(u32 i, u32 f, const u8 *t)
{
	return t32_tp_intp_u16(i, f, t);
}

static u32 t33_swg_dma_u(u32 mod)
{
	return (u32)tisp_rcfg_get_dma_state(mod);
}

static int t33_swg_dma_i(u32 mod)
{
	return tisp_rcfg_get_dma_state(mod);
}

/* tisp_dmsc_bin_par_update: the bin bytes of the section at dmscInfo word 670 */
static void t33_swg_dmsc_bin(u32 ch)
{
	(void)ch;
	t33_vb_bin(t33_dmsc_v, t33_swg_ptr(t33_dmsc_v[T33_DMSC_SEC_WORD]), t33_dmsc_bin, ARRAY_SIZE(t33_dmsc_bin));
}

/* func_dmsc_para_update: the copies, then the derived words (as tisp_dmsc_init) */
static void t33_swg_dmsc_para(u32 ch)
{
	(void)ch;
	t33_vb_copy(t33_dmsc_v, t33_dmsc_copy, ARRAY_SIZE(t33_dmsc_copy));
	t33_dmsc_para(t33_dmsc_v);
}

static u32 t33_swg_ratio(u32 r, u32 v, u32 limit)
{
	return t32_dmsc_ratio(r, v, limit);	/* tisp_ratio */
}

static const struct t33_swdmsc_ops t33_swg_dmsc_ops = {
	t33_swg_dmsc_bin, t33_swg_i8, t33_swg_i16, t33_swg_ratio, t33_swg_dmsc_para, t33_swg_dma_u, t33_swg_wr_v,
};

static void t33_swg_copy(void *d, const void *s, u32 n)
{
	memcpy(d, s, n);
}

/* tisp_ysp_bin_data_refresh: the bin bytes from ysp_paramsP */
static void t33_swg_ysp_bin(u32 ch)
{
	(void)ch;
	t33_vb_bin(t33_ysp_v, t33_swg_ysp_par, t33_ysp_bin, ARRAY_SIZE(t33_ysp_bin));
}

static void t33_swg_ysp_reg(u32 ch)
{
	(void)ch;
	t33_ysp_reg_data(t33_ysp_v);	/* tisp_ysp_reg_data_refresh */
}

static void t33_swg_mdns_dec(u32 ch, u32 *info)
{
	t33_bst_mdns_decode(ch, (const u8 *)info);	/* tisp_mdns_decode_reg_write */
}

static const struct t33_swysp_ops t33_swg_ysp_ops = {
	t33_swg_copy, t33_swg_ysp_bin, t33_swg_ysp_reg, t33_swg_dma_i, t33_swg_wr_v, t33_swg_mdns_dec,
};

/* ---- mdns switch_para (ops-struct port) ---- */
#define T33_TP_OFF_MDNS		25144U		/* tparamsP + this = MDNS section (mdnsInfo word 2180) */
#define T33_MDNS_ZIP_WORD	2190		/* mdnsInfo byte 0x2238: reference compression on */

/* global_ybsnthres, global_ypbv0stathres, global_ypbv1stathres, global_yrefweimin, global_yrefweiminadjthres */
static u8 t33_swg_mdns_g[5][44];
static const u8 t33_swg_stmem[76];	/* stMemInfo[0]: byte 11 (WDR) 0, the driver has no WDR */

static void t33_swg_mdns_bin(u32 ch)
{
	(void)ch;
	t33_vb_bin(t33_mdns_v, t33_swg_ptr(t33_mdns_v[2180]), t33_mdns_bin, ARRAY_SIZE(t33_mdns_bin));	/* tisp_mdns_binpara_update */
}

static void t33_swg_mdns_para(u32 ch)
{
	(void)ch;
	t33_mdns_para(t33_mdns_v);	/* func_mdns_para_update */
}

static void t33_swg_mdns_trig(u32 ch)
{
	t33_mdns_reg_trig(ch, t33_bst_cfg_mode, t33_mdns_init_flag, &t33_bst_refresh_mdns);	/* tisp_mdns_t33_reg_trig */
}

static const struct t33_swmdns_ops t33_swg_mdns_ops = {
	t33_swg_wr_v, t33_swg_mdns_bin, t33_swg_mdns_para, t33_swg_mdns_trig, t33_swg_i8, t33_swg_i16,
};

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

static inline void t33_swg_dmsc_gain(const u8 *sec, u32 gain)
{
	t33_dmsc_v[T33_DMSC_SEC_WORD] = (u32)(unsigned long)sec;
	t33_dmsc_v[T33_DMSC_GAIN_WORD] = gain;
	t33_dmsc_v[T33_DMSC_SHARP_WORD] = 0x80;	/* the vendor's init sharpness (no ratio API on the T33 path) */
}

static inline void t33_swg_ysp_gain(const u8 *sec, u32 gain)
{
	t33_ysp_v[T33_YSP_SEC_WORD] = (u32)(unsigned long)sec;
	t33_ysp_v[T33_YSP_GAIN_WORD] = gain;
}

/* tisp_dpc_init: tx_isp_t33_g14glue.h (group 14-2a) */

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
		memcpy(t33_dpc_sec, bin + off, sizeof(t33_dpc_sec));	/* the bin parse into tparamsP */
		t33_dpc_v[T33_DPC_SEC_WORD] = (u32)(unsigned long)t33_dpc_sec;
		t33_dpc_switch(0);
		local_irq_restore(flags);
		t33_swg_runs[2]++;
		done |= 1ULL << T33_MOD_DPC;
	}
	off = t32_iq_section(bin, bytes, T33_SEC_YDNS, &len);
	if (off && len == T33_YDNS_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_YDNS))) {
		t33_bst_hold(T33_BST_YDNS);
		local_irq_save(flags);
		memcpy(t33_ydns_sec, bin + off, sizeof(t33_ydns_sec));	/* the bin parse into tparamsP */
		tparamsP[0] = (u32 *)((unsigned long)t33_ydns_sec - T33_TP_OFF_YDNS);
		t33_ydns_switch(0);
		local_irq_restore(flags);
		t33_swg_runs[1]++;
		done |= 1ULL << T33_MOD_YDNS;
	}
	off = t32_iq_section(bin, bytes, T33_SEC_SDNS, &len);
	if (off && len == T33_SDNS_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_SDNS))) {
		t33_bst_hold(T33_BST_SDNS);
		local_irq_save(flags);
		memcpy(t33_sdns_sec, bin + off, sizeof(t33_sdns_sec));
		tparamsP[0] = (u32 *)((unsigned long)t33_sdns_sec - T33_TP_OFF_SDNS);
		t33_sdns_switch(0);
		local_irq_restore(flags);
		t33_swg_runs[0]++;
		done |= 1ULL << T33_MOD_SDNS;
	}
	off = t32_iq_section(bin, bytes, T32_SEC_CDNS, &len);
	if (off && len == T33_CDNS_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_CDNS))) {
		t33_bst_hold(T33_BST_CDNS);
		local_irq_save(flags);
		memcpy(t33_cdns_sec, bin + off, sizeof(t33_cdns_sec));
		t33_bst_refresh[T33_BST_CDNS] = (u8)t33_cdns_switch(0, t33_cdns_inf,
			(const u8 *)((unsigned long)t33_cdns_sec - T33_CDNS_SEC_OFF), t33_bst_cfg_mode,
			tisp_rcfg_get_dma_state, t33_swg_wr_v, t33_swg_cdns_para);
		local_irq_restore(flags);
		t33_swg_runs[3]++;
		done |= 1ULL << T33_MOD_CDNS;
	}
	off = t32_iq_section(bin, bytes, T33_SEC_DMSC, &len);
	if (off && len == T33_DMSC_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_DMSC))) {
		t33_bst_hold(T33_BST_DMSC);
		local_irq_save(flags);
		memcpy(t33_dmsc_sec, bin + off, sizeof(t33_dmsc_sec));	/* the bin parse into tparamsP */
		t33_dmsc_switch_para(&t33_swg_dmsc_ops, 0, t33_dmsc_v,
				     (u32)((unsigned long)t33_dmsc_sec - T33_TP_OFF_DMSC), t33_bst_cfg_mode,
				     &t33_bst_refresh[T33_BST_DMSC]);
		local_irq_restore(flags);
		t33_swg_runs[6]++;
		done |= 1ULL << T33_MOD_DMSC;
	}
	off = t32_iq_section(bin, bytes, T33_SEC_YSP, &len);
	if (off && len == T33_YSP_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_YSP))) {
		t33_bst_hold(T33_BST_YSP);
		local_irq_save(flags);
		memcpy(t33_ysp_sec, bin + off, sizeof(t33_ysp_sec));	/* the bin parse into tparamsP */
		t33_ysp_switch_para(&t33_swg_ysp_ops, 0, t33_ysp_sec - T33_YSP_SEC,
				    t33_swg_ysp_par, t33_ysp_v, t33_bst_cfg_mode, &t33_bst_refresh[T33_BST_YSP]);
		local_irq_restore(flags);
		t33_swg_runs[7]++;
		done |= 1ULL << T33_MOD_YSP;
	}
	off = t32_iq_section(bin, bytes, T33_SEC_MDNS, &len);
	if (off && len == T33_MDNS_SEC_BYTES && (t33_mod_built & (1ULL << T33_MOD_MDNS)) && t33_mdns_v[0x888]) {
		u8 *g[5] = { t33_swg_mdns_g[0], t33_swg_mdns_g[1], t33_swg_mdns_g[2], t33_swg_mdns_g[3],
			     t33_swg_mdns_g[4] };

		local_irq_save(flags);
		t33_mdns_v[T33_MDNS_ZIP_WORD] = t32_core_memopt() / 100 - 11 < 10;
		t33_mdns_switch_para(&t33_swg_mdns_ops, 0, t33_mdns_v, (const u8 *)((unsigned long)(bin + off) - T33_TP_OFF_MDNS),
				     g, t33_bst_cfg_mode, t33_dn_uv_limit[0], t33_swg_stmem);
		local_irq_restore(flags);
		t33_swg_runs[8]++;
		done |= 1ULL << T33_MOD_MDNS;
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
	memcpy(t33_rdns_sec, bin + off, sizeof(t33_rdns_sec));	/* the bin parse into tparamsP */
	t33_rdns_v[T33_RDNS_SEC_WORD] = (u32)(unsigned long)t33_rdns_sec;
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
