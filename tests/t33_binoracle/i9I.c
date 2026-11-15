/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "i9I": tisp_bin_data_parse with the vendor calling convention
 * (driver/t32/tx_isp_t33_i9I.h), run against the vendor module by tools/ko_emu/t33_binoracle.py.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9I.h"

u32 lutMaxSize[28];
u32 stLutInfo[4];
u8 *tparamsP[4];
u8 stParSel[448];
u8 st_tisp_com_par[1344];

int bo_tisp_bin_data_parse(u32 ch, u32 size, u8 *bin) { return t33_bin_data_parse(ch, size, bin); }
