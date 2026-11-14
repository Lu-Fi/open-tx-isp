/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) day/night switch_para entry points: the vendor's
 * tisp_af_switch_para / tisp_clm_switch_para / tisp_top_switch_para.
 *
 * Each function takes a channel index, looks up the per-channel block state
 * (af_info / clm_info / stTopPar) and the per-channel IQ section pointer
 * (tparamsP[ch]), stores the section pointer into the block state and then
 * runs the block's refresh (par_init + reg_write for AF, ct_interp +
 * calc_para_refresh + write_reg for CLM, reg_write for TOP).  The block
 * state and section pointer words are the only memory these entry points
 * touch; the refresh functions live in the other blocks and are called
 * through the binary oracle's CALLS mechanism.
 */
#ifndef TX_ISP_T33_SWSM2_H
#define TX_ISP_T33_SWSM2_H

#include "tx_isp_t33_blk.h"

/* refreshed block functions ported elsewhere: recorded, not run */
extern void tisp_af_par_init(u32 ch);
extern void tisp_af_t33_all_reg_write(u32 ch, u32 *p);
extern void tisp_clm_ct_interp(u32 ch, u32 p, u32 forced);
extern void tisp_clm_calc_para_refresh(u32 ch);
extern void tisp_clm_t33_write_reg(u32 ch);
extern int tisp_top_reg_write(u32 ch);

/*
 * tisp_af_switch_para(ch): af_info[ch] word 0x13a (byte 1256) =
 * tparamsP[ch] + 0x9fec, then tisp_af_par_init(ch) and
 * tisp_af_t33_all_reg_write(ch, af_info[ch] + 1028).  Returns 0.
 */
static inline int t33_af_switch_para(u32 ch, u32 *af_info, const u32 *tparamsP)
{
	u32 *blk = (u32 *)af_info[ch];

	blk[0x4e8 / 4] = tparamsP[ch] + 0x9fec;
	tisp_af_par_init(ch);
	tisp_af_t33_all_reg_write(ch, blk + 1028 / 4);
	return 0;
}

/*
 * tisp_clm_switch_para(ch): clm_info[ch] word 0 = tparamsP[ch] + 0x903c,
 * then tisp_clm_ct_interp(ch, clm_info[ch] word 0x18, 1),
 * tisp_clm_calc_para_refresh(ch) and tisp_clm_t33_write_reg(ch).  Returns 0.
 */
static inline int t33_clm_switch_para(u32 ch, u32 *clm_info, const u32 *tparamsP)
{
	u32 *blk = (u32 *)clm_info[ch];

	blk[0] = tparamsP[ch] + 0x903c;
	tisp_clm_ct_interp(ch, blk[96 / 4], 1);
	tisp_clm_calc_para_refresh(ch);
	tisp_clm_t33_write_reg(ch);
	return 0;
}

/*
 * tisp_top_switch_para(ch): stTopPar[ch & 0xff] = tparamsP[ch & 0xff],
 * then tail-calls tisp_top_reg_write(ch & 0xff) and returns its return value.
 */
static inline int t33_top_switch_para(u32 ch, u32 *stTopPar, const u32 *tparamsP)
{
	stTopPar[ch & 0xff] = tparamsP[ch & 0xff];
	return tisp_top_reg_write(ch & 0xff);
}

#endif /* TX_ISP_T33_SWSM2_H */
