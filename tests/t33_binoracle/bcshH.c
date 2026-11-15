/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "bcshH": BCSH API hue / saturation setters. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bcshH.h"

void *bcsh_para[4];
void *bcshInfo[4];
u8 bcshCfgMode;
u8 tispBcshRefresh;

int bo_tisp_bcsh_api_set_hue(u32 ch, u8 *val) { return t33_bcsh_set_hue(ch, val); }
int bo_tisp_bcsh_api_set_saturation(u32 ch, u8 *val) { return t33_bcsh_set_saturation(ch, val); }
