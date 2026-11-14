/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "aeldc": AE day/night switch, online LDC LUT
 * refresh and the LSC mirror/flip helper (driver/t32/tx_isp_t33_aeldc.h)
 * with the calling convention of the vendor functions they port.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_aeldc.h"

u8 ldcCfgMode;
u8 stIspInitPar[4032];
u32 g_ae_contexts[4];

int bo_tisp_ae_switch_para(u32 ch) { return t33_ae_switch_para(ch); }
int bo_tisp_online_ldc_reflesh_lut(u32 ch, u32 addr) { return t33_online_ldc_refresh(ch, addr); }
void bo_do_mirror_flip(u8 *img, u32 w, u32 h, u32 mirror, u32 flip) { t33_mirror_flip(img, w, h, mirror, flip); }
