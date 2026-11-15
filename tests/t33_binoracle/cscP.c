/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "cscP": our T33 CSC parameter ports
 * (driver/t32/tx_isp_t33_cscP.h) with the vendor calling convention, built
 * for mipsel and run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_cscP.h"

/* the six built-in CSC tables (.data CSC_BT*) as oracle-managed globals */
u8 cscp_bt601_full[92];
u8 cscp_bt601_limited[92];
u8 cscp_bt709_full[92];
u8 cscp_bt709_limited[92];
u8 cscp_bt2020_full[92];
u8 cscp_bt2020_limited[92];

/* CSWTCH.20 byte table and CSWTCH.19 pointer table */
u8 cswtch20[8];
u32 cswtch19[7];

/* csc_para[ch] */
u8 *csc_para[1];

u8 *bo_tisp_csc_para_refresh_by_mode(u32 mode, u32 refresh, u8 *dst)
{
	const u8 *mode1_tab[6] = {
		cscp_bt601_full, cscp_bt601_limited,
		cscp_bt709_full, cscp_bt709_limited,
		cscp_bt2020_full, cscp_bt2020_limited,
	};
	const u8 *mode2_tab[7];
	unsigned int i;

	for (i = 0; i < 7; i++)
		mode2_tab[i] = (const u8 *)(unsigned long)cswtch19[i];

	return t33_csc_para_refresh_by_mode(mode, refresh, dst, mode1_tab,
					    cswtch20, mode2_tab);
}

u32 bo_tisp_csc_update_para(u32 ch)
{
	return t33_csc_update_para(ch, (struct t33_cscp *const *)csc_para);
}

int bo_tisp_csc_sync_api_para(u32 ch, u32 dir)
{
	return t33_csc_sync_api_para(ch, dir, (struct t33_cscp *const *)csc_para);
}
