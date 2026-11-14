/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "tmoSW": tisp_tmo_switch_para with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_tmoSW.h"

extern void func_bin_para_update(u32 ch);
extern void tisp_tmo_tmo_lumcurve_ev_interp(u32 ch);
extern void tisp_tmo_tmo_satcurve_ev_interp(u32 ch);
extern void func_tmo_ic_para_update(u32 ch);
extern void tisp_tmo_ramY_vi0_reg_refresh(u32 ch);
extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);
extern int system_reg_write(u32 reg, u32 val);

u8 bo_tmoCfgMode;
u8 bo_tispTmoRefresh;
u32 bo_tmoInfo[4];
u32 bo_tparamsP[4];

static void w_bin(u32 ch) { func_bin_para_update(ch); }
static void w_lum(u32 ch) { tisp_tmo_tmo_lumcurve_ev_interp(ch); }
static void w_sat(u32 ch) { tisp_tmo_tmo_satcurve_ev_interp(ch); }
static void w_ic(u32 ch) { func_tmo_ic_para_update(ch); }
static void w_y(u32 ch) { tisp_tmo_ramY_vi0_reg_refresh(ch); }
static void w_rw(u32 ch, u32 blk, u32 reg, u32 val) { tisp_reg_write_t33(ch, blk, reg, val); }
static void w_sys(u32 reg, u32 val) { system_reg_write(reg, val); }

static const struct t33_tmosw_ops bo_ops = {
	w_bin, w_lum, w_sat, w_ic, w_y, w_rw, w_sys,
};

int bo_tisp_tmo_switch_para(u32 a0)
{
	u32 ch = a0 & 0xff;

	return t33_tmo_switch_para(&bo_ops, ch, (u8 *)(unsigned long)bo_tparamsP[a0],
				   (u32 *)(unsigned long)bo_tmoInfo[a0], bo_tmoCfgMode, &bo_tispTmoRefresh);
}
