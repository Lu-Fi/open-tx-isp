/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r12F": libisp entry points with the vendor calling convention.
 * Callees are extern with the vendor names (CALLS in r12F.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r12F.h"

extern u32 tisp_event_process(u32 ch);
extern u32 system_reg_read(u32 reg);
extern u32 system_reg_write(u32 reg, u32 val);
extern u32 tisp_top_ipc_get_mem_size(u32 w, u32 cfg0);
extern u32 lib_tisp_ldc_get_mem_size(u32 ch, u32 w0, u32 w1);
extern u32 tisp_mdns_t33_get_mem_size(u32 ch, u32 w, u32 h, u32 mode, u32 pad);
extern u32 tisp_mdns_get_mem_size(u32 ch, u32 w, u32 h, u32 mode, u32 pad);
extern u32 tisp_top_ipc_set_mem_cfg(u32 base);
extern u32 tisp_ldc_cfg_dma(u32 ch, u32 attr);
extern u32 tisp_mdns_t33_set_mem_cfg(u32 ch, u32 a1, u32 a2, u32 a3);
extern u32 tisp_mdns_set_mem_cfg(u32 ch, u32 a1, u32 a2, u32 a3);
extern u32 tisp_ae_par_sensor_trig(u32 ch);
extern u32 tisp_msca_t33_channel_attr_set(u32 ch, u32 a1, u32 a2, u32 a3);
extern u32 tisp_msca_t33_api_chx_stream_off(u32 ch);
extern u32 tisp_msca_t33_api_chx_stream_on(u32 ch);

u32 g_stIspInitPar[1008];
u32 g_stMemInfo[76];
u32 g_stSensorAttr[232];
u32 g_stLDCAttr[4];
u8 g_LdcInfo[2];
u8 g_ispInitStatic[2];

static const struct t33_r12f_ops ops = {
	tisp_event_process, system_reg_read, system_reg_write, tisp_top_ipc_get_mem_size,
	lib_tisp_ldc_get_mem_size, tisp_mdns_t33_get_mem_size, tisp_mdns_get_mem_size,
	tisp_top_ipc_set_mem_cfg, tisp_ldc_cfg_dma, tisp_mdns_t33_set_mem_cfg, tisp_mdns_set_mem_cfg,
	tisp_ae_par_sensor_trig, tisp_msca_t33_channel_attr_set, tisp_msca_t33_api_chx_stream_off,
	tisp_msca_t33_api_chx_stream_on,
};

u32 bo_lib_tisp_fw_process(u32 ch) { return t33_lib_fw_process(&ops, ch); }
u32 bo_lib_tisp_get_ipc_output_pos(void) { return t33_lib_get_ipc_output_pos(&ops); }
u32 bo_lib_tisp_get_isp_ipc_memory_space(u32 ch, const u8 *cfg)
{
	return t33_lib_get_ipc_mem(&ops, (u8 *)g_stIspInitPar, (u8 *)g_stMemInfo, g_LdcInfo, ch, cfg);
}
u32 bo_lib_tisp_get_isp_memory_space(u32 ch, const u8 *cfg)
{
	return t33_lib_get_isp_mem(&ops, (u8 *)g_stIspInitPar, (u8 *)g_stMemInfo, ch, cfg);
}
u32 bo_lib_tisp_set_isp_ipc_memory_space(u32 ch, u32 base)
{
	return t33_lib_set_ipc_mem(&ops, g_ispInitStatic, (u8 *)g_stIspInitPar, (u8 *)g_stMemInfo, g_LdcInfo,
				   g_stLDCAttr, ch, base);
}
u32 bo_lib_tisp_set_isp_memory_space(u32 ch, u32 a1, u32 a2, u32 a3)
{
	return t33_lib_set_isp_mem(&ops, g_ispInitStatic, (u8 *)g_stIspInitPar, ch, a1, a2, a3);
}
u32 bo_lib_tisp_sync_sensor_attr(u32 ch, const u8 *attr)
{
	return t33_lib_sync_sensor_attr(&ops, (u8 *)g_stSensorAttr, ch, attr);
}
u32 bo_lib_tisp_msca_channel_attr_set(u32 ch, u32 a1, u32 a2, u32 a3)
{
	return t33_lib_msca_channel_attr_set(&ops, ch, a1, a2, a3);
}
u32 bo_lib_tisp_msca_chx_reg_stream_off(u32 ch) { return t33_lib_msca_chx_reg_stream_off(&ops, ch); }
u32 bo_lib_tisp_msca_chx_stream_off(u32 ch) { return t33_lib_msca_chx_stream_off(&ops, ch); }
u32 bo_lib_tisp_msca_chx_stream_on(u32 ch) { return t33_lib_msca_chx_stream_on(&ops, ch); }
