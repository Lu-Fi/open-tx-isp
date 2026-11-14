/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "top": TOP bypass / register-write helpers
 * (driver/t32/tx_isp_t33_top.h) with the vendor calling convention, run
 * against the vendor tx-isp-t33.ko instructions (spec: top.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_top.h"

u8 ipcCfgMode;
u8 tispTopRefresh;
u32 *stTopPar[256];

u32 bo_tisp_top_get_bypass(u32 id) { return t33_top_get_bypass(id); }
u32 bo_tisp_top_set_bypass(u32 id, u32 bits) { return t33_top_set_bypass(id, bits); }
u32 bo_tisp_top_reg_write(u32 id) { return t33_top_reg_write(id); }
