/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mscacur": tisp_msca_t33_scaling_algorithm / _curve_calc / _ch_curve_write /
 * _ctrl_reg_write (openimp #99, #643), callees as CALLS
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mscacur.h"

extern int tisp_reg_write_t33(u32 id, u32 mod, u32 reg, u32 val);
extern void tisp_sin(u32 n, u32 cnt, short *out);
extern void tisp_msca_t33_two_point_normalized(u32 n, u32 taps, short *c);
extern void tisp_msca_t33_thr_point_normalized(u32 n, u32 taps, short *c);
extern void tisp_msca_t33_four_ponit_normalized(u32 n, u32 taps, short *c);
extern void tisp_msca_t33_six_point_normalized(u32 n, u32 taps, short *c);
extern void system_reg_write(u32 reg, u32 val);
extern u32 system_reg_read(u32 reg);
extern void tisp_msca_t33_chx_en(u32 id, u32 ch, u32 en);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern int tisp_rcfg_module_cfg_trig(u32 id, u32 mod);

u8 *bo_msca_info[4];
u8 *bo_msca_attr[4];
u8 bo_cfg_mode;
u8 bo_init_par[908];
u8 bo_refresh;

static const struct t33_mscacur m = {
	bo_msca_info, bo_msca_attr, &bo_cfg_mode, bo_init_par, &bo_refresh, tisp_reg_write_t33, tisp_sin,
	tisp_msca_t33_two_point_normalized, tisp_msca_t33_thr_point_normalized, tisp_msca_t33_four_ponit_normalized,
	tisp_msca_t33_six_point_normalized, system_reg_write, system_reg_read, tisp_msca_t33_chx_en,
	tisp_rcfg_get_dma_state, tisp_rcfg_module_cfg_trig,
};

void bo_tisp_msca_t33_scaling_algorithm(u32 id)
{
	t33_msca_scaling_algorithm_v(&m, id);
}

void bo_tisp_msca_t33_curve_calc(u32 id, u32 ch)
{
	t33_msca_curve_calc_v(&m, id, ch);
}

void bo_tisp_msca_t33_ch_curve_write(u32 id, u32 ch)
{
	t33_msca_ch_curve_write_v(&m, id, ch);
}

int bo_tisp_msca_t33_ctrl_reg_write(u32 id, u32 ch)
{
	return t33_msca_ctrl_reg_write_v(&m, id, ch);
}
