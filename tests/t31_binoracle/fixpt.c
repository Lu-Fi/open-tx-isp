/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "fixpt": the fixed-point helpers the T31 tuning code uses (driver/t31/include/
 * tx_isp_fixpt.h, tisp_math_exp2 of tx_isp_core.c, both on driver/t31/tx_isp_t31_fixvendor.h) against the vendor
 * tx-isp-t31.ko fix_point_mult2_32 / fix_point_mult3_32 / fix_point_div_32 / tisp_math_exp2 over the full u32 input
 * range (spec: fixpt.conf; openimp #710).
 */
#include <stdint.h>
#include "tx_isp_t31_fixvendor.h"

typedef uint32_t u32;

uint32_t bo_fix_point_mult2_32(u32 q, u32 a, u32 b) { return t31_vendor_fixmul_u32(q, a, b); }
uint32_t bo_fix_point_mult3_32(u32 q, u32 a, u32 b, u32 c) { return t31_vendor_fixmul3_u32(q, a, b, c); }
uint32_t bo_fix_point_div_32(u32 q, u32 n, u32 d) { return t31_vendor_fixdiv_u32(q, n, d); }
uint32_t bo_tisp_math_exp2(u32 v, u32 s, u32 b) { return t31_vendor_exp2_u32(v, s, b); }
