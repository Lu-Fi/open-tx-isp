/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r11B": AWB accessors on awbInfo[ch] with the vendor calling convention
 * (tests/t33_binoracle/run.sh, spec r11B.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r11B.h"

struct t33_awb_ctx *bo_awbInfo[4];

extern u32 tisp_simple_intp_uint16(u32 i, u32 f, const u8 *t);
extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 slot, u32 val);
extern void system_reg_set_awb_trig(u32 v, u32 ch);

int bo_tisp_awb_set_lum_th_freq(u32 ch)
{
	return t33_awb_set_lum_th_freq(bo_awbInfo, ch, tisp_simple_intp_uint16, tisp_reg_write_t33,
				       system_reg_set_awb_trig);
}

int bo_tisp_awb_set_regional_threshold(u32 ch)
{
	return t33_awb_set_regional_threshold(bo_awbInfo, ch, tisp_simple_intp_uint16, tisp_reg_write_t33,
					      system_reg_set_awb_trig);
}

void bo_tisp_awb_show_para_update(u32 ch) { t33_awb_show_para_update(bo_awbInfo, ch); }
void bo_tisp_awb_get_zone(u32 ch, u8 *out) { t33_awb_get_zone(bo_awbInfo, ch, out); }
int bo_tisp_awb_get_global_statis(u32 ch, u8 *out) { return t33_awb_get_global_statis(bo_awbInfo, ch, out); }
u32 bo_tisp_awb_api_get_ct_trend_offset(u32 ch, u8 *out) { return t33_awb_api_get_ct_trend_offset(bo_awbInfo, ch, out); }
void bo_tisp_awb_api_set_ct_trend_offset(u32 ch, u8 *in) { t33_awb_api_set_ct_trend_offset(bo_awbInfo, ch, in); }
int bo_tisp_g_awb_ct_trend_offset(u32 ch, u8 *out) { return t33_g_awb_ct_trend_offset(bo_awbInfo, ch, out); }
int bo_tisp_s_awb_ct_trend_offset(u32 ch, u8 *in) { return t33_s_awb_ct_trend_offset(bo_awbInfo, ch, in); }
int bo_tisp_g_awb_statis_attr(u32 ch, u8 *out) { return t33_g_awb_statis_attr(bo_awbInfo, ch, out); }
int bo_tisp_g_awb_global_statis_attr(u32 ch, u8 *out) { return t33_g_awb_global_statis_attr(bo_awbInfo, ch, out); }
