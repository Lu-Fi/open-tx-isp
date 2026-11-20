/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "adrfpga": Tiziano_adr_fpga with subsection_map / subsection / subsection_up /
 * subsection_light / interpolate_adr_x8_y12 (driver/t31/tx_isp_t31_adrfpga.h) vs the vendor tx-isp-t31.ko
 * instructions (spec: adrfpga.conf; openimp #563 step 2).  The vendor runs its own fix_point_div_32,
 * fix_point_mult2_32 and tisp_math_exp2; ours the shared helpers of tx_isp_math.h / tx_isp_fixpt.h.
 */
#include <stdint.h>
#include <string.h>

typedef uint32_t u32;
static inline void pr_info(const char *f, ...) { (void)f; }
/*
 * T31_ADRF_DRIVER_HELPERS=1: the driver's helpers (tx_isp_math.h, as tx_isp_tuning.c builds them).  Default: bit
 * exact C of the vendor fix_point_mult2_32 (0x11014), fix_point_div_32 (0x11084) and tisp_math_exp2 (0x107d0), so
 * the block compares the ADR logic; the helpers differ from the vendor only outside the ranges the AE / ADR use
 * (32-bit wrap, q = 0, exp2 integer part >= 32; see the helper lines of adrfpga.conf).
 */
#ifdef T31_ADRF_DRIVER_HELPERS
#include "../include/tx_isp/tx_isp_math.h"
static inline uint32_t fix_point_mult2_32(u32 q, u32 a, u32 b) { return tx_isp_fixmul_u32(q, a, b); }
static inline uint32_t fix_point_div_32(u32 q, u32 n, u32 d) { return tx_isp_fixdiv_u32(q, n, d); }
static inline uint32_t tisp_math_exp2(uint32_t v, uint32_t s, uint32_t b) { return tx_isp_exp2_u32(v, s, b); }
#else
static inline uint32_t fix_point_mult2_32(u32 q, u32 a, u32 b)
{
	u32 s = q & 31U, mask = 0xffffffffU >> ((32U - s) & 31U);
	u32 ai = a >> s, bi = b >> s, af = a & mask, bf = b & mask;

	return ((ai * bi) << s) + af * bi + ai * bf + ((af * bf) >> s);
}
static inline uint32_t fix_point_div_32(u32 q, u32 n, u32 d)
{
	u32 q0 = n / d, r = n - d * q0, f = 0, i = 0;

	while (i != q) {
		r <<= 1;
		f <<= 1;
		if (d < r) {
			f |= 1;
			r -= d;
		} else if (d == r) {
			f |= 1;
			f <<= (q - 1 - i) & 31U;
			break;
		}
		i++;
	}
	return f | (u32)((uint64_t)q0 << (q & 63U));
}
static inline uint32_t tisp_math_exp2(uint32_t v, uint32_t s, uint32_t b)
{
	static const u32 t[33] = {
		0x40000000U, 0x4166c34cU, 0x42d561b4U, 0x444c0740U, 0x45cae0f2U, 0x47521cc6U, 0x48e1e9baU, 0x4a7a77d4U,
		0x4c1bf829U, 0x4dc69cddU, 0x4f7a9930U, 0x51382182U, 0x52ff6b55U, 0x54d0ad5aU, 0x56ac1f75U, 0x5891fac1U,
		0x5a82799aU, 0x5c7dd7a4U, 0x5e8451d0U, 0x60962665U, 0x62b39509U, 0x64dcdec3U, 0x6712460bU, 0x69540ec9U,
		0x6ba27e65U, 0x6dfddbccU, 0x70666f76U, 0x72dc8374U, 0x75606374U, 0x77f25cceU, 0x7a92be8bU, 0x7d41d96eU,
		0x80000000U,
	};
	u32 sh = s & 0xff, frac = v & ((1U << (sh & 31U)) - 1U), ip = v >> (sh & 31U), ob = b & 0xff, m, lo;

	if (sh < 6)
		return t[frac << ((5 - sh) & 31U)] >> ((30 - ob - ip) & 31U);
	m = sh - 5;
	lo = t[frac >> (m & 31U)];
	return (lo + (u32)((uint64_t)(t[(frac >> (m & 31U)) + 1] - lo) * (frac & ((1U << (m & 31U)) - 1U)) >> m)) >>
	       ((30 - ob - ip) & 31U);
}
#endif
static void adr_log_curve11(const char *tag, int log_idx, int blk, const uint32_t *vals)
{ (void)tag; (void)log_idx; (void)blk; (void)vals; }
static void adr_log_grid24(const char *tag, int log_idx, const int32_t *vals) { (void)tag; (void)log_idx; (void)vals; }

uint32_t adr_hist_512[512], block_mean_y[24], adr_block_y_24[24], adr_block_hist_120[120];
uint32_t data_980b0[512];		/* vendor adr_prod */
static int adr_grid_debug_budget;
/* the vendor's min_kneepoint_y[10] / ctc_kneepoint_y[8] are the words after the arrays (arg4[10], arg2[8]);
 * the driver keeps them as data_9f0a8 / data_9f0cc: loaded from and stored back to those words around the call */
static int32_t data_9f0a8, data_9f0cc;

#include "tx_isp_t31_adrfpga.h"

void bo_Tiziano_adr_fpga(void *a1, uint32_t *a2, uint32_t *a3, uint32_t *a4, uint32_t *a5, uint32_t *a6,
			 uint32_t *a7, uint32_t *a8, uint32_t *a9, uint32_t *a10, uint8_t *a11, uint8_t *a12,
			 uint32_t *a13, uint32_t *a14, uint32_t *a15, uint32_t *a16)
{
	data_9f0a8 = (int32_t)a4[10];
	data_9f0cc = (int32_t)a2[8];
	Tiziano_adr_fpga(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
	a4[10] = (uint32_t)data_9f0a8;
	a2[8] = (uint32_t)data_9f0cc;
}

uint32_t bo_fix_point_mult2_32(u32 q, u32 a, u32 b) { return fix_point_mult2_32(q, a, b); }
uint32_t bo_fix_point_div_32(u32 q, u32 n, u32 d) { return fix_point_div_32(q, n, d); }
uint32_t bo_tisp_math_exp2(u32 v, u32 s, u32 b) { return tisp_math_exp2(v, s, b); }
int bo_subsection_up(uint32_t *out, const uint32_t *ct_list, const uint32_t *ct_table, uint32_t scale)
{
	return subsection_up(out, ct_list, ct_table, scale);
}
int bo_subsection_light(const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t *out)
{
	return subsection_light(a, b, w, out);
}
