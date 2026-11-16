/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "i9H": tisp_rdns_par_refresh, tisp_dpc_l_gain_update. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9H.h"

u32 *rdnsInfo[4] = { 0 };
u32 *dpcInfo[4] = { 0 };
u8 rdnsCfgMode = 0;
u8 dpcCfgMode = 0;
u8 tispRdnsRefresh = 0;
u8 tispDpcRefresh = 0;

void bo_tisp_rdns_par_refresh(u32 ch, u32 gain, u32 thr) { t33_rdns_par_refresh_i9h(ch, gain, thr); }
void bo_tisp_dpc_l_gain_update(u32 ch, u32 gain) { t33_dpc_l_gain_update_i9h(ch, gain); }
