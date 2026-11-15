/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "parB": tisp_cdns_par_refresh (driver/t32/tx_isp_t33_parB.h)
 * with the vendor calling convention, built for mipsel and run against the vendor
 * tx-isp-t33.ko instructions (spec: parB.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_parB.h"

/* module globals the vendor function reads/writes (GLOBAL lines) */
u8 cdnsCfgMode;
u8 tispCdnsRefresh;
u32 *cdnsInfo[4];

/* callees: neither side runs them, calls are recorded and compared */
extern void func_cdns_para_update(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern void tisp_reg_write_t33(u32 id, u32 mod, u32 reg, u32 val);

u32 bo_tisp_cdns_par_refresh(u32 ch, u32 gain, u32 hyst)
{
	return t33_cdns_par_refresh(ch, gain, hyst, (u8 *)cdnsInfo[ch], cdnsCfgMode,
				    &tispCdnsRefresh, tisp_rcfg_get_dma_state,
				    tisp_reg_write_t33, func_cdns_para_update);
}
