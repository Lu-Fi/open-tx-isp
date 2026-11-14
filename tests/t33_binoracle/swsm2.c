/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "swsm2": the day/night switch_para entry points
 * (driver/t32/tx_isp_t33_swsm2.h) with the vendor calling convention, built
 * for mipsel (tests/t33_binoracle/run.sh) and run against the vendor
 * tx-isp-t33.ko instructions by tools/ko_emu/t33_binoracle.py (spec:
 * swsm2.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_swsm2.h"

/* module globals the vendor functions read/write (GLOBAL lines) */
u32 af_info[4];
u32 clm_info[4];
u32 stTopPar[256];
u32 tparamsP[4];

/* dummy definitions give the CALLS names symbols on the ours side */
#define KEEP __attribute__((noinline))
#define BARRIER() __asm__ __volatile__("" ::: "memory")
KEEP void tisp_af_par_init(u32 ch) { (void)ch; BARRIER(); }
KEEP void tisp_af_t33_all_reg_write(u32 ch, u32 *p) { (void)ch; (void)p; BARRIER(); }
KEEP void tisp_clm_ct_interp(u32 ch, u32 p, u32 forced) { (void)ch; (void)p; (void)forced; BARRIER(); }
KEEP void tisp_clm_calc_para_refresh(u32 ch) { (void)ch; BARRIER(); }
KEEP void tisp_clm_t33_write_reg(u32 ch) { (void)ch; BARRIER(); }
KEEP int tisp_top_reg_write(u32 ch) { (void)ch; BARRIER(); return 0; }

int bo_tisp_af_switch_para(u32 ch)
{
	return t33_af_switch_para(ch, af_info, tparamsP);
}

int bo_tisp_clm_switch_para(u32 ch)
{
	return t33_clm_switch_para(ch, clm_info, tparamsP);
}

int bo_tisp_top_switch_para(u32 ch)
{
	return t33_top_switch_para(ch, stTopPar, tparamsP);
}
