/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r13A": tmo lum/sat curve interpolation, Y/V ram refresh, rcfg num disV
 * with the vendor calling convention (spec: r13A.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r13A.h"

u8 *ours_tmoInfo[4];
u8 ours_stIspInitPar[4032];
u8 ours_tmoCfgMode;
u8 ours_stModuleAttr[7168];
u16 ours_tmo_rcfg_reg_num;

extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);
extern void tisp_tmo_drc_ctrl(u32 ch);
extern void tisp_tmo_ramV_vi23_reg_ctrl(u32 ch);
extern int system_reg_write(u32 reg, u32 val);

static void w_drc(u32 ch) { tisp_tmo_drc_ctrl(ch); }
static void w_wr(u32 ch, u32 b, u32 r, u32 v) { tisp_reg_write_t33(ch, b, r, v); }
static void w_sys(u32 r, u32 v) { system_reg_write(r, v); }
static void w_ctrl(u32 ch) { tisp_tmo_ramV_vi23_reg_ctrl(ch); }

static u8 src_tab[402];

void bo_tisp_tmo_tmo_lumcurve_ev_interp(u32 ch)
{
	u8 cx[402];
	volatile unsigned long n = 402;

	memcpy(cx, src_tab, n);
	t33_tmo_lumcurve_interp(ours_tmoInfo[ch], cx);
}

void bo_tisp_tmo_tmo_satcurve_ev_interp(u32 ch)
{
	t33_tmo_satcurve_interp(ours_tmoInfo[ch]);
}

void bo_tisp_tmo_ramY_vi0_reg_refresh(u32 ch)
{
	struct t33_r13a_ops o;

	o.drc_ctrl = w_drc; o.reg_write = w_wr; o.sys_write = w_sys; o.vi23_ctrl = w_ctrl;
	t33_tmo_ramy_refresh(&o, ours_tmoInfo, ours_tmoCfgMode, ch);
}

void bo_tisp_tmo_ramV_vi0_reg_refresh(u32 ch)
{
	struct t33_r13a_ops o;

	o.drc_ctrl = w_drc; o.reg_write = w_wr; o.sys_write = w_sys; o.vi23_ctrl = w_ctrl;
	t33_tmo_ramv_refresh(&o, ours_tmoInfo, t33_le32(ours_stIspInitPar + 904), ours_tmoCfgMode, ch);
}

int bo_tisp_tmo_rcfg_reg_num_disV(u32 ch)
{
	return t33_tmo_rcfg_reg_num_disV(ours_stModuleAttr, &ours_tmo_rcfg_reg_num, ch);
}
