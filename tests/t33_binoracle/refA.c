/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "refA": the three block refresh wrappers
 * (driver/t32/tx_isp_t33_refA.h) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_refA.h"

u32 *cdnsInfo[4];
u32 *rdnsInfo[4];
u32 *yspInfo[4];

void bo_tisp_cdns_refresh(u32 ch, u32 *inf) { t33_cdns_refresh(ch, inf); }
void bo_tisp_rdns_refresh(u32 ch, u32 *inf) { t33_rdns_refresh(ch, inf); }
void bo_tisp_ysp_refresh(u32 ch, u32 *inf) { t33_ysp_refresh(ch, inf); }
