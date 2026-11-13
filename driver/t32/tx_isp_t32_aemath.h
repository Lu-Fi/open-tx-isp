/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE fixed point maths: the real functions behind the maths hooks of the
 * ported AE paths (struct t32_ae_init_ops, t32_ae_ops, t32_ae_ev_ops, ...),
 * so that the AE chain can run in the driver.
 *   t32_aem_div32     fix_point_div_32   0x1dbc8  = t32_awb_fixdiv (tx_isp_t32_awbct.h)
 *   t32_aem_mult2_32  fix_point_mult2_32 0x1db58  = t32_awb_fixmul (tx_isp_t32_awb.h)
 *   t32_aem_mult3_32  fix_point_mult3_32 0x1db9c  mult2_32(mult2_32(a, b), c)
 *   t32_aem_mult2_64  fix_point_mult2_64 0x1d774  the flow of fix_point_mult2 (t32_fixp_mult2), but the fraction
 *                                                  mask is ~0 >> (64 - q) mod 64 also for q = 0: there it is all
 *                                                  ones and the result 4 * a * b (vendor quirk, kept)
 *   t32_aem_mult3_64  fix_point_mult3_64 0x1d914  mult2_64(mult2_64(a, b), c)
 *   t32_aem_exp2      tisp_math_exp2     0x1d180  2^(v / 2^in_q) in out_q fraction bits, 33-entry table
 *                                                  t32_data_beea4 (data.c:1996), linear between the entries
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by tests/t32_oracle (t32_aemath_oracle).
 * Shift counts as the vendor: 32 bit shifts mod 32, the libgcc 64 bit shift (__lshrdi3) mod 64.
 */
#ifndef TX_ISP_T32_AEMATH_H
#define TX_ISP_T32_AEMATH_H

#include "tx_isp_t32_awbct.h"

/* tisp_math_exp2 table: 2^(i / 32) in Q30, i = 0..32 */
static const u32 t32_aem_exp2_tab[33] = {
	0x40000000, 0x4166c34c, 0x42d561b4, 0x444c0740, 0x45cae0f2, 0x47521cc6, 0x48e1e9ba, 0x4a7a77d4,
	0x4c1bf829, 0x4dc69cdd, 0x4f7a9930, 0x51382182, 0x52ff6b55, 0x54d0ad5a, 0x56ac1f75, 0x5891fac1,
	0x5a82799a, 0x5c7dd7a4, 0x5e8451d0, 0x60962665, 0x62b39509, 0x64dcdec3, 0x6712460b, 0x69540ec9,
	0x6ba27e65, 0x6dfddbcc, 0x70666f76, 0x72dc8374, 0x75606374, 0x77f25cce, 0x7a92be8b, 0x7d41d96e,
	0x80000000,
};

static inline u32 t32_aem_div32(u32 sh, u32 a, u32 b)
{
	return t32_awb_fixdiv(sh, a, b);
}

static inline u32 t32_aem_mult2_32(u32 sh, u32 a, u32 b)
{
	return t32_awb_fixmul(sh, a, b);
}

static inline u32 t32_aem_mult3_32(u32 sh, u32 a, u32 b, u32 c)
{
	return t32_awb_fixmul(sh, t32_awb_fixmul(sh, a, b), c);
}

static inline unsigned long long t32_aem_mult2_64(u32 sh, unsigned long long a, unsigned long long b)
{
	unsigned long long mask = ~0ULL >> ((64 - sh) & 63);
	unsigned long long ai = a >> (sh & 63), bi = b >> (sh & 63), af = a & mask, bf = b & mask;

	return ((ai * bi) << (sh & 63)) + ai * bf + af * bi + ((af * bf) >> (sh & 63));
}

static inline unsigned long long t32_aem_mult3_64(u32 sh, unsigned long long a, unsigned long long b,
						  unsigned long long c)
{
	return t32_aem_mult2_64(sh, t32_aem_mult2_64(sh, a, b), c);
}

static inline u32 t32_aem_exp2(u32 v, u32 in_q, u32 out_q)
{
	u32 q = in_q & 0xff, oq = out_q & 0xff;
	u32 frac = v & ((1U << (q & 31)) - 1), ip = v >> (q & 31);
	u32 s, i, lo, f;

	if (q < 6)
		return t32_aem_exp2_tab[frac << ((5 - q) & 31)] >> ((30 - oq - ip) & 31);
	s = q - 5;
	i = frac >> (s & 31);
	lo = t32_aem_exp2_tab[i];
	f = frac & ((1U << (s & 31)) - 1);
	lo += (u32)(((unsigned long long)(t32_aem_exp2_tab[i + 1] - lo) * f) >> (s & 63));
	return lo >> ((30 - oq - ip) & 31);
}

#endif /* TX_ISP_T32_AEMATH_H */
