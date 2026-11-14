/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "msca-api2": MSCA channel start / enable / autozoom
 * helpers (driver/t32/tx_isp_t33_msca_api2.h) with the vendor calling
 * convention, built for mipsel and run against tx-isp-t33.ko (msca-api2.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_msca_api2.h"

u8 *stMscaAttr[4];

int bo_lib_tisp_msca_channel_start(u32 ch) { return t33_msca_channel_start(ch); }
int bo_tisp_msca_t33_get_msca_chx_en(u32 unused, u32 ch) { (void)unused; return t33_msca_chx_en(ch); }
void bo_tisp_msca_t33_api_get_autozoom(u32 ch, u32 *out) { t33_msca_get_autozoom(stMscaAttr[ch & 0xff], out); }
