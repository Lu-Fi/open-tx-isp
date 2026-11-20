/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 fixed-point helpers bit-exact to the vendor tx-isp-t31.ko (openimp #710): fix_point_mult2_32 (0x11014),
 * fix_point_mult3_32 (0x11058), fix_point_div_32 (0x11084) and tisp_math_exp2 (0x107d0).  Inside the ranges the AE / ADR use (q 1..16, operands
 * below 2^16 / 2^20, exp2 with shift 16 and base 10) they equal the shared tx_isp_math.h helpers; outside them the
 * vendor wraps at 32 bits, gives 4ab for q = 0, stops the divide loop early at d == 2r and walks the exp2 table
 * with shift amounts taken & 31.  Proof: tests/t31_binoracle fixpt block over the full u32 input range.
 */
#ifndef TX_ISP_T31_FIXVENDOR_H
#define TX_ISP_T31_FIXVENDOR_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

static inline uint32_t t31_vendor_fixmul_u32(uint32_t q, uint32_t a, uint32_t b)
{
	uint32_t s = q & 31U, mask = 0xffffffffU >> ((32U - s) & 31U);
	uint32_t ai = a >> s, bi = b >> s, af = a & mask, bf = b & mask;

	return ((ai * bi) << s) + af * bi + ai * bf + ((af * bf) >> s);
}

/* vendor fix_point_mult3_32 (0x11058): mult2 of mult2 */
static inline uint32_t t31_vendor_fixmul3_u32(uint32_t q, uint32_t a, uint32_t b, uint32_t c)
{
	return t31_vendor_fixmul_u32(q, t31_vendor_fixmul_u32(q, a, b), c);
}

static inline uint32_t t31_vendor_fixdiv_u32(uint32_t q, uint32_t n, uint32_t d)
{
	uint32_t q0, r, f = 0, i = 0;

	if (d == 0)	/* the vendor traps (teq); never called with 0 */
		return 0;
	q0 = n / d;
	r = n - d * q0;
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
	return f | (uint32_t)((uint64_t)q0 << (q & 63U));
}

static inline uint32_t t31_vendor_exp2_u32(uint32_t v, uint32_t s, uint32_t b)
{
	static const uint32_t t[33] = {
		0x40000000U, 0x4166c34cU, 0x42d561b4U, 0x444c0740U, 0x45cae0f2U, 0x47521cc6U, 0x48e1e9baU, 0x4a7a77d4U,
		0x4c1bf829U, 0x4dc69cddU, 0x4f7a9930U, 0x51382182U, 0x52ff6b55U, 0x54d0ad5aU, 0x56ac1f75U, 0x5891fac1U,
		0x5a82799aU, 0x5c7dd7a4U, 0x5e8451d0U, 0x60962665U, 0x62b39509U, 0x64dcdec3U, 0x6712460bU, 0x69540ec9U,
		0x6ba27e65U, 0x6dfddbccU, 0x70666f76U, 0x72dc8374U, 0x75606374U, 0x77f25cceU, 0x7a92be8bU, 0x7d41d96eU,
		0x80000000U,
	};
	uint32_t sh = s & 0xff, frac = v & ((1U << (sh & 31U)) - 1U), ip = v >> (sh & 31U), ob = b & 0xff, m, lo;

	if (sh < 6)
		return t[frac << ((5 - sh) & 31U)] >> ((30 - ob - ip) & 31U);
	m = sh - 5;
	lo = t[frac >> (m & 31U)];
	return (lo + (uint32_t)((uint64_t)(t[(frac >> (m & 31U)) + 1] - lo) * (frac & ((1U << (m & 31U)) - 1U)) >> m)) >>
	       ((30 - ob - ip) & 31U);
}

#endif
