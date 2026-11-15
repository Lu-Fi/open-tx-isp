/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "bcshN": tisp_bcsh_init with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bcshN.h"

void *bcsh_para[4];
void *bcshInfo[4];
u8 bcshCfgMode;
u8 tispBcshRefresh;
u8 *tparamsP[4];
u8 bcsh_ip[40];
u8 bcsh_cp[300];
u8 bcsh_sp[112];
u8 bcsh_ap[144];

int bo_tisp_bcsh_init(u32 ch, u8 *par) { return t33_bcshN_init(ch, par); }
