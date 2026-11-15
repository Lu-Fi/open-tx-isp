/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "adralgo": tiziano_adr_algorithm (driver/t31/tx_isp_t31_adralgo.h) vs the vendor
 * tx-isp-t31.ko instructions (spec: adralgo.conf; openimp #563).  Globals carry the vendor names; Tiziano_adr_fpga
 * is not run (recorded call), so only the EV interpolation, the CTC curve, the X copies and the smoothing are compared.
 */
#include <stdint.h>
extern int Tiziano_adr_fpga(void);
#define T31_ADR_LOG_CTRL(ev, idx)	do { } while (0)
#define T31_ADR_FPGA()			Tiziano_adr_fpga()
#define T31_ADR_LOG_FINAL()		do { } while (0)

uint32_t ev_changed, ev_now;
uint32_t *adr_ev_list_now, *adr_map_mode_now, *adr_light_end_now, *adr_block_light_now, *adr_blp2_list_now;
uint32_t *adr_ligb_list_now, *adr_mapb1_list_now, *adr_mapb2_list_now, *adr_mapb3_list_now, *adr_mapb4_list_now;
uint32_t *adr_ctc_map2cut_y_now;
uint32_t param_adr_min_kneepoint_array[23], param_adr_min_kneepoint_array_def[23], param_adr_map_kneepoint_array[23];
uint32_t param_adr_ctc_kneepoint_array[17], param_adr_tool_control_array[14];
uint8_t param_adr_gam_y_array[0x102];
uint32_t min_kneepoint_x[11], map_kneepoint_x[11], ctc_kneepoint_x[9];
uint32_t map_kneepoint_y_pre[264], map_kneepoint_y[264];

#include "tx_isp_t31_adralgo.h"

uint32_t bo_tiziano_adr_algorithm(void)
{
	return t31_adr_algorithm();
}
