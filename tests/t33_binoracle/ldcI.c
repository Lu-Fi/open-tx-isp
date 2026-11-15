/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "ldcI": tisp_ldc_t33_intp_handle (driver/t32/tx_isp_t33_ldcintp.h t33_ldc_intp_handle)
 * with the vendor calling convention (spec: ldcI.conf; openimp #590).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_ldcintp.h"

extern u32 system_reg_read(u32 reg);
extern void system_reg_write(u32 reg, u32 val);

u8 stIspInitPar[912];
u32 ldcIntpInfo[2];

static const struct t33_ldcintp_hw hw = { system_reg_read, system_reg_write };

u32 bo_tisp_ldc_t33_intp_handle(void)
{
	return t33_ldc_intp_handle(stIspInitPar, ldcIntpInfo, &hw);
}
