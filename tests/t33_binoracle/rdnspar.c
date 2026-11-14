/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rdnspar": RDNS / CDNS para_update (tx_isp_t33_rdns.h, tx_isp_t32_cdns.h) with the
 * vendor calling convention: the vendor reads rdnsInfo[0] / cdnsInfo[0], ours gets the block.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rdns.h"
#include "tx_isp_t32_cdns.h"

u8 blcCfgMode, dgCfgMode;
extern u32 tisp_reg_read_t33(u32 ch, u32 mod, u32 reg);

void bo_func_rdns_bin_para_update(u32 *v) { t33_rdns_bin_para_update(v, (const u8 *)v[T33_RDNS_SEC_WORD]); }
void bo_func_cdns_para_update(u32 *v) { t33_cdns_para_update(v); }

/* the vendor reads the BLC / DG list slot 3 (cfg mode 1, as our driver: GLOBAL ... w:0:1) */
void bo_func_rdns_para_update(u32 *v)
{
	u32 blc = tisp_reg_read_t33(0, 4, 3), dg = tisp_reg_read_t33(0, 5, 3);

	t33_rdns_para_update(v, blc, dg);
}
