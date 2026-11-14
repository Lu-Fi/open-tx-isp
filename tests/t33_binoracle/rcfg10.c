/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg10": tisp_rcfg_msca_reg_init vs our
 * t33_rcfg_msca_init (driver/t32/tx_isp_t33_rcfg10.h).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg10.h"

u32 stIspInitPar[256];

u32 bo_tisp_rcfg_msca_reg_init(u32 a0, u32 *e)
{
	(void)a0;
	return t33_rcfg_msca_init(e, stIspInitPar);
}
