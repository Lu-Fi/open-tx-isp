/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "r13B": log2 family, round/clip/ratio/tmo_log2/ae div (tx_isp_t33_r13B.h). */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r13B.h"

u32 t33_pow2_lut[33];
short t33_sin_lut9[258];
short t33_sin_lut13[258];
short t33_sin_lut17[258];
short t33_sin_lut25[258];

u32 bo_tisp_math_exp2(u32 x, u32 fb, u32 out) { return t33_math_exp2(x, fb, out); }
void bo_tisp_sin(u32 n, u32 cnt, u8 *out) { t33_sin(n, cnt, (short *)out); }
u32 bo_tisp_log2_fixed_to_fixed(u32 v, u32 in_fp, u32 out_fp) { return t33_log2_f2f(v, in_fp, out_fp); }
u32 bo_tisp_log2_fixed_to_fixed_64(unsigned long long v, u32 in_fp, u32 out_fp) { return t33_log2_f2f_64(v, in_fp, out_fp); }
u32 bo_tisp_log2_int_to_fixed(u32 v, u32 fp, u32 out) { return t33_log2_i2f(v, fp, out); }
u32 bo_tisp_log2_int_to_fixed_64(unsigned long long v, u32 fp, u32 out) { return t33_log2_i2f_64(v, fp, out); }
long long bo_tisp_round_int64(long long x, u32 n) { return t33_round64(x, n); }
u32 bo_tisp_ratio(u32 w, u32 a, u32 b) { return t33_ratio(w, a, b); }
long long bo_tisp_clip(long long x, long long lo, long long hi) { return t33_clip64(x, lo, hi); }
u32 bo_tisp_tmo_log2(u32 a, u32 b) { return t33_tmo_log2(a, b); }
unsigned long long bo_tisp_ae_lib_div_64(unsigned long long n, u32 dlo, u32 dhi) { return t33_ae_div64(n, dlo, dhi); }
