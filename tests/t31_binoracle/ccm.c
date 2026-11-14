/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "ccm": tiziano_ct_ccm_interpolation / tiziano_ct_bcsh_interpolation (driver/t31/tx_isp_t31_intp.h) vs the vendor
 * tx-isp-t31.ko instructions (spec: ccm.conf; openimp #533).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t31_intp.h"

uint32_t o_flag, o_last, o_real;
int32_t o_out[9], o_d[9], o_t[9], o_a[9];
uint32_t b_flag, b_last; int b_real;
int32_t b_out[9], b_d[9], b_t[9], b_a[9];

uint32_t bo_tiziano_ct_ccm_interpolation(uint32_t ct, uint32_t thr)
{
	t31_ct_ccm_interp(ct, thr, &o_flag, &o_last, o_real, o_out, o_d, o_t, o_a);
	return 0;
}

uint32_t bo_tiziano_ct_bcsh_interpolation(uint32_t ct)
{
	t31_ct_bcsh_interp(ct, &b_flag, &b_last, b_real, b_out, b_d, b_t, b_a);
	return 0;
}
