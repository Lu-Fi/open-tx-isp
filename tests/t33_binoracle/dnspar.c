/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "dnspar": func_dmsc_para_update (driver/t32/tx_isp_t33_dmsc.h: the copies, then
 * t33_dmsc_para with isp_dmsc_update_value_ks / func_dmsc_isp_itp_update_ks / cti_func_slope_calc) on the DMSC
 * block state (the vendor's dmscInfo[0] block), built for mipsel and run against the vendor tx-isp-t33.ko
 * instructions (spec: dnspar.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_dmsc.h"

void bo_dmsc_para(u32 *v)
{
	t33_vb_copy(v, t33_dmsc_copy, ARRAY_SIZE(t33_dmsc_copy));
	t33_dmsc_para(v);
}
