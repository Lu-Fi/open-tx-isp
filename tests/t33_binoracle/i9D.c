/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "i9D": tisp_top_init, tisp_top_ipc_init, tisp_af_init with the vendor
 * calling convention (spec: i9D.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9D.h"

u8 ipcCfgMode;
u8 tispTopRefresh;
u32 *stTopPar[256];
u32 *t33_top_tparams[256];
u32 t33_top_deir_en[256];
u32 t33_top_isp_init_par[256 * 252];
u32 t33_top_bayer_remap[32];
u8 tispAfRefresh;
u8 *t33_af_info[256];

int bo_tisp_top_init(u32 id, u8 *info) { return t33_top_start(id, info); }
int bo_tisp_top_ipc_init(u8 *info) { return t33_top_ipc_start(info); }
int bo_tisp_af_init(u32 id, u8 *info) { return t33_af_bringup(id, info); }
