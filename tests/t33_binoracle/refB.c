/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "refB": the per-channel refresh entry points
 * (driver/t32/tx_isp_t33_refB.h) with the vendor calling convention.
 * The info arrays are plain globals (GLOBAL lines), the par_refresh callees
 * are extern (CALLS).  The vendor leaves $a1 untouched, so it is an argument.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_refB.h"

u32 *ydnsInfo[4];
u32 *sdnsInfo[4];
u32 *dmscInfo[4];

extern void tisp_ydns_par_refresh(u32 ch, u32 a1, u32 gain);
extern void tisp_sdns_par_refresh(u32 ch, u32 a1, u32 gain);
extern void tisp_dmsc_par_refresh(u32 ch, u32 a1, u32 gain);

int bo_tisp_ydns_refresh(u32 ch, u32 a1) { return t33_ydns_refresh(ch, a1, ydnsInfo, tisp_ydns_par_refresh); }
int bo_tisp_sdns_refresh(u32 ch, u32 a1) { return t33_sdns_refresh(ch, a1, sdnsInfo, tisp_sdns_par_refresh); }
int bo_tisp_dmsc_refresh(u32 ch, u32 a1) { return t33_dmsc_refresh(ch, a1, dmscInfo, tisp_dmsc_par_refresh); }
