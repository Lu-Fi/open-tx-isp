/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "sw-cdns": tisp_cdns_switch_para with the vendor
 * calling convention.  The vendor function reads tparamsP[ch] and cdnsInfo[ch]
 * from module globals; the wrapper receives cdnsInfo[ch] as the info: argument
 * and reads the same globals.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_swcdns.h"

extern int tisp_rcfg_get_dma_state(u32 id);
extern void tisp_reg_write_t33(u32 ch, u32 id, u32 reg, u32 val);
extern void func_cdns_para_update(u32 ch);

u32 tparamsP[4];
u8 cdnsCfgMode;
u8 tispCdnsRefresh;

int bo_tisp_cdns_switch_para(u32 ch, u32 *inf)
{
	int ret;

	ret = t33_cdns_switch(ch, inf, (const u8 *)tparamsP[ch], cdnsCfgMode,
			      tisp_rcfg_get_dma_state, tisp_reg_write_t33,
			      func_cdns_para_update);
	tispCdnsRefresh = (u8)ret;
	return ret;
}
