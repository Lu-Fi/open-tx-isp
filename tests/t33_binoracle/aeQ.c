/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "aeQ": AE ROI / fps entry points (driver/t32/tx_isp_t33_aeQ.h). */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_aeQ.h"

extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 slot, u32 val);
extern int isp_printf(int level, const char *fmt, ...);
extern int tisp_normal_ae_calc_process(u32 a0, u32 a1, u32 a2, u32 a3, u32 s0, u32 s1, u32 s2, u32 s3, u32 s4,
				       u32 s5);
extern void tisp_ae_clac_deflicker_cfg(u32 ch);

u8 o_aeCfgMode;
u8 *o_ae_cache[2];
u8 *o_stAeInfo[2];
u8 o_stSensorAttr[928];
u8 o_h_roi_block_start[4];
u8 o_h_roi_block_end[4];
u8 o_v_roi_block_start[4];
u8 o_v_roi_block_end[4];

static void w_err(u32 level)
{
	isp_printf((int)level, "aeQ");
}

static const struct t33_aeq_ops ops = {
	tisp_reg_write_t33, w_err, tisp_normal_ae_calc_process, tisp_ae_clac_deflicker_cfg,
};

int bo_tisp_ae_calc_process(u32 a0, u32 a1, u32 a2, u32 a3, u32 s0, u32 s1, u32 s2, u32 s3, u32 s4, u32 s5)
{
	return t33_ae_calc_process_fwd(a0, a1, a2, a3, s0, s1, s2, s3, s4, s5, &ops);
}

int bo_tisp_ae_roi_point(u32 ch, u32 x0, u32 y0, u32 x1, u32 y1)
{
	return t33_ae_roi_point(ch, x0, y0, x1, y1, &o_aeCfgMode, o_ae_cache, &ops);
}

void bo_tisp_ae_sepc_area(u32 ch, u8 *p)
{
	t33_ae_sepc_area(ch, p, o_ae_cache, o_stAeInfo, o_h_roi_block_start, o_h_roi_block_end,
			 o_v_roi_block_start, o_v_roi_block_end, &o_aeCfgMode, &ops);
}

int bo_tisp_ae_api_set_fps(u32 ch, u8 *fps)
{
	return t33_ae_api_set_fps(ch, fps, o_stAeInfo, o_stSensorAttr, &ops);
}

int bo_tisp_set_fps(u32 ch, u32 fps, u32 unused_a, u32 unused_b)
{
	(void)unused_a;
	(void)unused_b;
	return t33_set_fps(ch, fps, o_stAeInfo, o_stSensorAttr, &ops);
}
