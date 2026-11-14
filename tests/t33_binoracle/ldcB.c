/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "ldcB": LDC par getter and IPC frame-down interrupt. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_ldcB.h"

extern void system_reg_write(u32 reg, u32 val);

u8 o_ldcParExist[8];
u8 o_stLdcPar[640];
u8 o_sensorNum;
signed char o_wdmaVinum;
u32 o_stDmaCfg[92];

int bo_lib_tisp_ldc_get_par(u32 idx, u8 *out)
{
	return t33_ldc_get_par(o_ldcParExist, o_stLdcPar, idx, out);
}

int bo_tisp_ipc_fra_down_interrupt(u32 idx)
{
	return t33_ipc_fra_down(idx, o_sensorNum, &o_wdmaVinum, o_stDmaCfg, system_reg_write);
}
