/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "wmean2": ae0_weight_mean2 (driver/t31/tx_isp_tuning.c, extracted at build time by
 * extract.py) with the vendor 25-argument calling convention, against the vendor tx-isp-t31.ko instructions
 * (spec: wmean2.conf; openimp #553).  The vendor's argument 4 and 7 are not read by the vendor; ours has no slot.
 */
#include <stddef.h>
#include <stdint.h>
#include "tx_isp_t31_fixvendor.h"

typedef uint32_t u32;
void *memcpy(void *d, const void *s, unsigned int n);
#define do_div(n, base) ({ (n) = (n) / (uint64_t)(uint32_t)(base); 0; })
static inline u32 fix_point_mult2_32(u32 q, u32 a, u32 b) { return t31_vendor_fixmul_u32(q, a, b); }
static inline u32 fix_point_div_32(u32 q, u32 n, u32 d) { return t31_vendor_fixdiv_u32(q, n, d); }

#pragma GCC diagnostic ignored "-Wunused-variable"
#include "t31_extract.h"

void bo_ae0_weight_mean2(uint32_t *r, uint32_t *g, uint32_t *b, uint32_t unused4, uint32_t *dark,
			 uint32_t *bright, uint32_t unused7, uint32_t *out_zones, void *zone_copy, void *ae_param,
			 uint32_t *zone_wt, void *awb_cfg, void *corr_cfg, int32_t *r_corr, int32_t *b_corr,
			 int32_t pa, int32_t pb, int32_t *q_ptr, int32_t mix_r, int32_t mix_b, uint32_t *wmean,
			 int32_t *ratios, int32_t *total, uint32_t *b_ratio, uint32_t *r_ratio)
{
	(void)unused4; (void)unused7;
	ae0_weight_mean2(r, g, b, dark, bright, out_zones, zone_copy, ae_param, zone_wt, awb_cfg, corr_cfg,
			 r_corr, b_corr, pa, pb, q_ptr, mix_r, mix_b, wmean, ratios, total, b_ratio, r_ratio);
}
