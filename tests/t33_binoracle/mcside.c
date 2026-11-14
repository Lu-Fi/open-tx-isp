/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mcside": the module-control side effects
 * (driver/t32/tx_isp_t33_mcside.h) with the vendor calling convention,
 * built for mipsel and run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mcside.h"

/* vendor callees: recorded and compared, never run */
extern void tisp_lce_curve_init_default(u32 ch);
extern void tisp_reg_write_t33(u32 id, u32 a, u32 b, u32 c);

/* module data objects (GLOBAL lines) */
u8 lceInfo[16];
u8 mdnsCfgMode;
u8 g_mdnsStart;

int bo_tisp_lce_top_change_state(u32 ch)
{
	u32 *lce = (u32 *)lceInfo;

	if (t33_lce_top_change_state(lce, ch))
		tisp_lce_curve_init_default(ch);
	return 0;
}

void bo_tisp_mdns_start(u32 ch)
{
	u32 reg = t33_mdns_start(mdnsCfgMode);

	tisp_reg_write_t33(ch, 15, reg, 1);
	g_mdnsStart = 1;
}
