/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "mdnsA": MDNS frame-done IRQ and compare-register writer. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mdnsA.h"

extern int tisp_reg_write_t33(u32 id, u32 blk, u32 reg, u32 val);
extern int tisp_rcfg_module_cfg_trig(u32 id, u32 blk);

u8 stIspInitPar[4032];
u8 mdnsInit;
u8 tispMdnsRefresh;
u8 g_mdnsStart;
u8 mdnsCfgMode;
u32 *mdnsInfo[4];

int bo_tisp_mdns_t33_frame_done_interrupt(u32 id)
{
	u32 ip = stIspInitPar[904] | stIspInitPar[905] << 8 | stIspInitPar[906] << 16 | (u32)stIspInitPar[907] << 24;

	return t33_mdns_frame_done(id, ip, &mdnsInit, &tispMdnsRefresh, &g_mdnsStart, mdnsCfgMode,
				   tisp_reg_write_t33, tisp_rcfg_module_cfg_trig);
}

void bo_tisp_mdns_t33_cmp_reg_write(u32 id)
{
	t33_mdns_cmp_reg_write(id, mdnsInfo[id], mdnsCfgMode, tisp_reg_write_t33);
}
