/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "bcshup": tiziano_bcsh_update (driver/t31/tx_isp_t31_bcshup.h) vs the vendor
 * tx-isp-t31.ko instructions (spec: bcshup.conf; openimp #533).  The o_* globals mirror the vendor ones.
 */
#include <stdint.h>
uint32_t o_ev, o_ct, o_svalue[4], o_c[5], o_bcsh[19], o_h[9], o_ccm[9], o_off2yuv[3];
uint32_t *o_evl, *o_smins, *o_smaxs, *o_sminm, *o_smaxm, *o_offrgb;
uint32_t o_cxl[9], o_cxh[9], o_cyl[9], o_cyh[9];
int o_real;

#define T31_BU_COMP2ORIG()	tiziano_bcsh_Tccm_Comp2Orig()
#define T31_BU_CTINTP(ct)	tiziano_ct_bcsh_interpolation(ct)
#define T31_BU_RGB2YUV()	tiziano_bcsh_Tccm_RGB2YUV(o_h, o_ccm)
#define T31_BU_TOFFSET()	tiziano_bcsh_Toffset_RGB2YUV(o_off2yuv, o_offrgb)
#define T31_BU_TRANSIT()	tiziano_bcsh_TransitParam()
#define T31_BU_LUT()		tiziano_bcsh_lut_parameter(o_bcsh[0], o_bcsh[1], o_bcsh[2], o_bcsh[3])
#define T31_BU_REAL_CLEAR()	(o_real = 0)
#include "tx_isp_t31_bcshup.h"

uint32_t bo_tiziano_bcsh_update(void)
{
	const uint32_t *const src[8] = { o_smins, o_smaxs, o_sminm, o_smaxm, o_cxl, o_cxh, o_cyl, o_cyh };
	uint32_t *const dst[8] = { &o_svalue[0], &o_svalue[1], &o_svalue[2], &o_svalue[3], &o_c[1], &o_c[2], &o_c[3], &o_c[4] };

	return t31_bcsh_update(o_ev, o_ct, o_evl, src, dst);
}
