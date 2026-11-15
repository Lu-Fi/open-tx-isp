/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "bcshlut": tiziano_bcsh_lut_parameter (driver/t31/tx_isp_t31_bcshlut.h) vs the vendor
 * tx-isp-t31.ko instructions (spec: bcshlut.conf; openimp #550).  The o_* globals mirror the vendor ones.
 */
#include <stdint.h>
extern int system_reg_write(uint32_t reg, uint32_t val);
#define T31_BL_W(r, v)	system_reg_write((r), (v))
#include "tx_isp_t31_bcshlut.h"

uint32_t o_ev, o_clip0[4];
uint32_t *o_evl, *o_hlspn;

uint32_t bo_tiziano_bcsh_lut_parameter(const uint32_t *clip, const uint32_t *clip1, const uint32_t *clip2,
				       const uint32_t *off0, const uint32_t *off1, const uint32_t *c,
				       const uint32_t *cs0, const uint32_t *cs1, const uint32_t *cs2,
				       const uint32_t *sthres, const uint32_t *svalue, const uint32_t *sstep,
				       const uint32_t *hmat, const uint32_t *hdp, const uint32_t *hdps,
				       const uint32_t *hbp, const uint32_t *hbps, const uint32_t *hlsp,
				       const uint32_t *hlsps)
{
	t31_bcsh_lut(clip, clip1, clip2, off0, off1, c, cs0, cs1, cs2, sthres, svalue, sstep, hmat, hdp, hdps,
		     hbp, hbps, hlsp, hlsps, o_ev, o_clip0, o_evl, o_hlspn);
	return 0;
}
