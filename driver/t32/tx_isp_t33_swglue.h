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

/* vendor tparams offsets of the sections the ports address through tparamsP */
#define T33_TP_OFF_SDNS	0x9b20U
#define T33_TP_OFF_YDNS	0x88acU

static u32 *sdnsInfo[4] = { t33_sdns_inf }, *ydnsInfo[4] = { t33_ydns_inf }, *dpcInfo[4] = { t33_dpc_v };
static u32 *tparamsP[4];
static const u8 *t33_swg_dpc_sec;		/* tisp_dpc_bin_para_update's section (tparamsP + dpc offset) */
u32 t33_swg_runs[3];				/* switch_para calls: sdns, ydns, dpc */

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

#include "tx_isp_t33_swsmall.h"

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
	return done;
}

#endif /* TX_ISP_T33_SWGLUE_H */
