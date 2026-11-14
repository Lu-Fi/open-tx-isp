/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "tmoA": TMO bin / IC para update, s2s3 ddrV init, frame-done interrupt
 * (driver/t32/tx_isp_t33_tmoA.h) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_tmoA.h"

u8 *bo_tmoInfo[4] = { 0 };
u8 *bo_tparamsP[4] = { 0 };
u32 bo_stIspInitPar[1008] = { 0 };
u8 bo_tispTmoRefresh = 0;
u8 bo_mulsensornum = 0;
u8 bo_tmoCfgMode = 0;

extern int tisp_reg_write_t33(u32 a, u32 b, u32 c, u32 d);
extern int tisp_rcfg_module_cfg_trig(u32 a, u32 b);
extern int tisp_tmo_ramV_vi23_reg_ctrl(u32 a);
extern int tisp_tmo_ramV_vi23_reg_refresh(u32 a);
extern int private_dma_cache_sync(u32 a, void *b, u32 c, u32 d);

void bo_func_bin_para_update(u32 ch)
{
	u8 *info = bo_tmoInfo[ch & 0xff];

	t33_tmo_bin_para((u32 *)info, *(u8 **)(info + T33_TMOA_SEC_PTR));
}

void bo_func_tmo_ic_para_update(u32 ch)
{
	t33_tmo_ic_para((u32 *)bo_tmoInfo[ch & 0xff]);
}

int bo_tisp_tmo_s2s3_ddrV_init(u32 ch)
{
	u8 *info = bo_tmoInfo[ch];
	u32 *out = *(u32 **)(info + T33_TMOA_DDRV_PTR);

	t33_tmo_ddrv_pack(info, out);
	return private_dma_cache_sync(0, out, 6000, 1);
}

static int w_reg(u32 a, u32 b, u32 c, u32 d) { return tisp_reg_write_t33(a, b, c, d); }
static int w_trig(u32 a, u32 b) { return tisp_rcfg_module_cfg_trig(a, b); }
static int w_ctrl(u32 a) { return tisp_tmo_ramV_vi23_reg_ctrl(a); }
static int w_ref(u32 a) { return tisp_tmo_ramV_vi23_reg_refresh(a); }

int bo_tisp_tmo_t33_frame_done_interrupt(u32 ch)
{
	struct t33_tmo_fd_ops o;

	o.reg_write = w_reg;
	o.cfg_trig = w_trig;
	o.ctrl = w_ctrl;
	o.refresh = w_ref;
	return t33_tmo_frame_done(&o, bo_tmoInfo, bo_tparamsP, bo_stIspInitPar[226], bo_tmoCfgMode,
				  &bo_mulsensornum, &bo_tispTmoRefresh, ch);
}
