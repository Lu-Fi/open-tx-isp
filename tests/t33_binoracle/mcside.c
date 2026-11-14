/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "mcside": module-control side effects (driver/t32/tx_isp_t33_mcside.h) */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mcside.h"

/* vendor callee, recorded and compared, never run */
extern void tisp_lce_curve_init_default(u32 ch);

/* module data objects (GLOBAL lines) */
u8 lceInfo[16];
u8 mdnsCfgMode;
u8 g_mdnsStart;

void bo_tisp_lce_top_change_state(u32 ch)
{
	u32 *outer = (u32 *)(unsigned long)((u32 *)lceInfo)[ch];

	t33_lce_top_change_state((u8 *)(unsigned long)outer[25336 / 4], ch, tisp_lce_curve_init_default);
}

void bo_tisp_mdns_start(u32 ch)
{
	t33_mdns_start(ch, mdnsCfgMode, &g_mdnsStart);
}
