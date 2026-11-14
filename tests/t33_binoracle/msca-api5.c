/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "msca-api5": autozoom, mirror/flip, mask out cfg. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_msca_api5.h"

u8 *bo_stMscaAttr[4];
u32 bo_fcrop_attr[60];
u8 bo_mscaCfgMode;
u8 bo_tispMscaRefresh;

void bo_tisp_msca_t33_api_set_autozoom(u32 ch, u32 *zoom)
{
	t33_msca_set_autozoom(ch, zoom, bo_stMscaAttr, bo_fcrop_attr, &bo_mscaCfgMode, &bo_tispMscaRefresh);
}

void bo_tisp_msca_t33_api_set_mirr_flip(u32 ch, u8 *flip)
{
	t33_msca_set_mirr_flip(ch, flip, bo_stMscaAttr, &bo_mscaCfgMode, &bo_tispMscaRefresh);
}

void bo_tisp_msca_t33_api_get_mask_out_cfg(u32 ch, u8 *o)
{
	t33_msca_get_mask_out_cfg(ch, o, bo_stMscaAttr);
}
