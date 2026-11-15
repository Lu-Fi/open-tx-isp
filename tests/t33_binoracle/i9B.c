/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "i9B": tisp_rdns_init / tisp_dmsc_init / tisp_ysp_init with the vendor calling
 * convention (spec: i9B.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9B.h"

u32 *rdnsInfo[4];
u32 *dmscInfo[4];
u32 *yspInfo[4];
u8 *tparamsP[4];
u8 *ysp_paramsP[4];

int bo_tisp_rdns_init(u32 ch, u8 *inf) { return t33_rdns_init(ch, (const u32 *)inf); }
int bo_tisp_dmsc_init(u32 ch, u8 *inf) { return t33_dmsc_init(ch, (const u32 *)inf); }
int bo_tisp_ysp_init(u32 ch, u8 *inf) { return t33_ysp_init(ch, (const u32 *)inf); }
