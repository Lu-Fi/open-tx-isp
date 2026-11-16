/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r11E": CCM / scaler level / autozoom / frame drop / module control / CSC-CR /
 * IPC memory functions (driver/t32/tx_isp_t33_r11E.h) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r11E.h"

u8 *bo_msca_info[2] = { 0, 0 };
u8 bo_mscaCfgMode = 0;
u32 bo_stMemInfo[76] = { 0 };

int bo_tisp_s_ccm_attr(u32 ch, u8 *buf) { return t33_s_ccm_attr(ch, buf); }
int bo_tisp_set_scaler_level_control(u32 ch, u8 *buf)
{
	return t33_set_scaler_level_control_set(bo_msca_info, bo_mscaCfgMode, ch, buf);
}
int bo_tisp_set_scaler_level_control_set(u32 ch, u8 *buf)
{
	return t33_set_scaler_level_control_set(bo_msca_info, bo_mscaCfgMode, ch, buf);
}
int bo_tisp_s_autozoom_control(u32 ch, u8 *buf) { return t33_s_autozoom_control(ch, buf); }
int bo_tisp_get_frame_drop(u32 ch, u32 idx, u8 *out) { return t33_get_frame_drop(ch, idx, out); }
int bo_tisp_set_frame_drop(u32 ch, u32 idx, u8 *cfg) { return t33_set_frame_drop(ch, idx, cfg); }
u32 bo_tisp_g_module_control(u32 ch, u32 *out) { return t33_g_module_control(ch, out); }
int bo_tisp_s_module_control(u32 ch, u32 en) { return t33_s_module_control(ch, en); }
int bo_tisp_g_csccr_mode(u32 ch, u8 *buf) { return t33_g_csccr_mode(ch, buf); }
u32 bo_tisp_top_ipc_get_mem_size(u32 w, u32 h) { return t33_ipc_get_mem_size(w, h, bo_stMemInfo); }
int bo_tisp_top_ipc_set_mem_cfg(u32 base) { return t33_ipc_set_mem_cfg(base, bo_stMemInfo); }
