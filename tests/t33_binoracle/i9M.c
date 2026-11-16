/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "i9M": tisp_ysp_par_refresh with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9M.h"

u32 *yspInfo[4];
u8 yspCfgMode;
u8 tispYspRefresh;

void bo_tisp_ysp_par_refresh(u32 ch, u32 gain, u32 thr) { t33_ysp_par_refresh(ch, gain, thr); }
