/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mscaInit": tisp_msca_t33_init with the vendor calling convention and the
 * module statics it uses as plain globals.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mscaInit.h"

u8 *stMscaAttr[2];
u8 *msca_info[2];
u32 MscaOsdInfo;
u8 *tparamsP[2];
u32 stIspInitPar[1008];
u8 mscaCfgMode;

static void bo_reg_init_stub(void)
{
}

static void bo_irq_stub(void)
{
}

int bo_tisp_msca_t33_init(u8 chn, const u8 *par)
{
	return t33_msca_t33_init(chn, par, stMscaAttr, msca_info, &MscaOsdInfo, tparamsP, stIspInitPar[226],
				 mscaCfgMode, (void *)bo_reg_init_stub, (void *)bo_irq_stub);
}
