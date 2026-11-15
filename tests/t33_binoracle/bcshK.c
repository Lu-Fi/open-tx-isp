/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "bcshK": the BCSH runtime entry points
 * (driver/t32/tx_isp_t33_bcshK.h) with the vendor calling convention,
 * built for mipsel and run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bcshK.h"

/* module data objects (GLOBAL lines) */
void *bcsh_para[4];
void *bcshInfo[4];
u8 bcshCfgMode;
u8 tispBcshRefresh;

int bo_tisp_bcsh_ev_update(u32 ch, u32 a1, u32 lo, u32 hi, u32 bvl, u32 bvh)
{
	return t33_bcshk_ev_update(ch, a1, lo, hi, bvl, bvh);
}

int bo_tisp_bcsh_api_set_brightness(u32 ch, const u8 *val)
{
	return t33_bcsh_set_brightness(ch, val);
}

int bo_tisp_bcsh_api_set_contrast(u32 ch, const u8 *val)
{
	return t33_bcsh_set_contrast(ch, val);
}
