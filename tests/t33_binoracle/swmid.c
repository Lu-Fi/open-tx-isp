/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "swmid": the day/night switch_para ports (driver/t32/tx_isp_t33_swmid.h) with the
 * vendor calling convention, run against tx-isp-t33.ko by tools/ko_emu/t33_binoracle.py (spec: swmid.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_swmid.h"

u8 *bo_tparamsP[4];
u32 *bo_ccm_para[4];
u8 bo_tispCcmRefresh;
u32 *bo_csccrInfo[4];
u8 bo_csccrCfgMode;
u8 bo_tispCsccrRefresh;
u32 *bo_defog_info[4];
u32 *bo_lsc_para[4];

int bo_tisp_ccm_switch_para(u32 a)
{
	return t33_ccm_switch_para(a, bo_tparamsP, bo_ccm_para, &bo_tispCcmRefresh);
}

int bo_tisp_csccr_switch_para(u32 a)
{
	return t33_csccr_switch_para(a, bo_tparamsP, bo_csccrInfo, &bo_csccrCfgMode, &bo_tispCsccrRefresh);
}

int bo_tisp_defog_switch_para(u32 a)
{
	return t33_defog_switch_para(a, bo_defog_info);
}

int bo_tisp_lsc_switch_para(u32 a)
{
	return t33_lsc_switch_para(a, bo_tparamsP, bo_lsc_para);
}
