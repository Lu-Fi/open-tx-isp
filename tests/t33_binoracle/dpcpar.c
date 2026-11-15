/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "dpcpar": the DPC / CTR / CAC parameter updates on dpcInfo
 * (driver/t32/tx_isp_t33_dpcpar.h) with the vendor calling convention (spec: dpcpar.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_dpcpar.h"

u32 *dpcInfo[4];

void bo_tisp_dpc_bin_para_update(u32 ch) { t33_dpc_bin_para_v(dpcInfo[ch & 0xff]); }
void bo_func_dpc_para_update(u32 ch) { t33_dpc_para_v(dpcInfo[ch & 0xff]); }
void bo_func_ctr_para_update(u32 ch) { t33_ctr_para_v(dpcInfo[ch & 0xff]); }
void bo_func_cac_para_update(u32 ch) { t33_cac_para_v(dpcInfo[ch & 0xff]); }
