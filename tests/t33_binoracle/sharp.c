/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "sharp": tisp_set_sharpness (YSP tables + refresh; DMSC part as a call), openimp #707. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_sharp.h"

u32 *yspInfo[4];
u8 yspCfgMode;
u8 tispYspRefresh;
u8 *ysp_paramsP[4];
u32 ysp_ratio;

void bo_tisp_set_sharpness(u32 ch, u32 level) { t33_set_sharpness(ch, level); }
