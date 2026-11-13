/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tisp fixed point helpers (vendor fix-point-calc.h of the T32 SDK, firmware 2.2.0) and the two AE library
 * helpers built on them:
 *
 *   t32_fixp_add / _add_64     fix_point_add 0x1d264, fix_point_add_64 0x1d6d0     a + b (64 bit, wraps)
 *   t32_fixp_sub / _sub_64     fix_point_sub 0x1d280, fix_point_sub_64 0x1d6ec     a - b, warns when a < b
 *   t32_fixp_add_32            fix_point_add_32 0x1db00                            a + b (32 bit)
 *   t32_fixp_sub_32            fix_point_sub_32 0x1db08                            a - b, warns when a < b
 *   t32_fixp_mult2             fix_point_mult2 0x1d308 (needed by mult3)           a * b >> q, split in parts
 *   t32_fixp_mult3             fix_point_mult3 0x1d4b8                             mult2(mult2(a, b), c)
 *   t32_fixp_intp              fix_point_intp 0x1dcd4                              see below
 *   t32_ae_lib_div_64          tisp_ae_lib_div_64 0x2a210                          do_div with a 32 bit divisor
 *   t32_ae_lib_bilinear_intp   tisp_ae_lib_bilinear_intp 0x2a274                   y0 +- |y1-y0| |x-x0| / |x1-x0|
 *
 * The pointpos argument of add/sub is unused by the vendor (kept for the call shape).  The vendor prints an error
 * (isp_printf level 2) for a negative difference and for a degenerate interpolation; ours counts it in *warn (may be
 * NULL).  The 64 bit shifts are the libgcc helpers __ashldi3 / __lshrdi3, modelled with the shift count taken mod 64
 * (the vendor never passes a count >= 64 for pointpos < 64; larger pointpos are outside the vendor's use).
 *
 * fix_point_intp(q, x0, x1, y0, y1): the vendor code (checked against the T32 2.2.0 firmware object, 0x1c704 there)
 * divides the constant 0 by (x1 - x0) with div64_u64 and runs the fraction loop on a zero remainder, so it returns
 * y0 when y0 == y1, 0 otherwise; for x1 == x0 and y0 != y1 it warns and returns 0; x1 < x0 warns first.  Its loop
 * counter is 8 bit: q >= 256 never ends (vendor hang, not reproduced: ours returns as for q < 256).
 *
 * tisp_ae_lib_div_64(n, d): 0 for d == 0, n / (u32)d otherwise (do_div truncates the divisor to its low word).
 * A divisor whose low word is 0 but high word is not is a vendor divide by zero (MIPS divu / __div64_32 by 0):
 * ours returns 0 there, the oracle counts and does not compare those cases.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_fixp_oracle).
 */
#ifndef TX_ISP_T32_FIXP_H
#define TX_ISP_T32_FIXP_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

static inline void t32_fixp_warn(u32 *warn)
{
	if (warn)
		(*warn)++;
}

/* fix_point_add / fix_point_add_64: the 64 bit sum, pointpos unused */
static inline uint64_t t32_fixp_add(u32 q, uint64_t a, uint64_t b)
{
	(void)q;
	return a + b;
}

/* fix_point_sub / fix_point_sub_64: the 64 bit difference, an error print when a < b */
static inline uint64_t t32_fixp_sub(u32 q, uint64_t a, uint64_t b, u32 *warn)
{
	(void)q;
	if (a < b)
		t32_fixp_warn(warn);
	return a - b;
}

/* fix_point_add_32 */
static inline u32 t32_fixp_add_32(u32 q, u32 a, u32 b)
{
	(void)q;
	return a + b;
}

/* fix_point_sub_32 */
static inline u32 t32_fixp_sub_32(u32 q, u32 a, u32 b, u32 *warn)
{
	(void)q;
	if (a < b)
		t32_fixp_warn(warn);
	return a - b;
}

/*
 * fix_point_mult2: a * b in fixed point with q fraction bits, built from the integer parts (>> q) and the
 * fractions (low q bits): (ai * bi << q) + ai * bf + af * bi + (af * bf >> q), every step mod 2^64.
 */
static inline uint64_t t32_fixp_mult2(u32 q, uint64_t a, uint64_t b)
{
	uint64_t mask = q ? ~0ULL >> ((64 - q) & 63) : 0;
	uint64_t ai = a >> (q & 63), bi = b >> (q & 63);
	uint64_t af = a & mask, bf = b & mask;

	return ((ai * bi) << (q & 63)) + ai * bf + af * bi + ((af * bf) >> (q & 63));
}

/* fix_point_mult3: (a * b) * c */
static inline uint64_t t32_fixp_mult3(u32 q, uint64_t a, uint64_t b, uint64_t c)
{
	return t32_fixp_mult2(q, t32_fixp_mult2(q, a, b), c);
}

/* fix_point_intp (see the header comment) */
static inline u32 t32_fixp_intp(u32 q, u32 x0, u32 x1, u32 y0, u32 y1, u32 *warn)
{
	(void)q;
	if (x1 < x0)
		t32_fixp_warn(warn);
	if (y0 == y1)
		return y0;
	if (x1 == x0)
		t32_fixp_warn(warn);
	return 0;
}

/* tisp_ae_lib_div_64 */
static inline uint64_t t32_ae_lib_div_64(uint64_t n, uint64_t d)
{
	if (!(u32)d)		/* d == 0 (vendor: 0), or the refused vendor divide by zero */
		return 0;
	return n / (u32)d;
}

static inline uint64_t t32_fixp_absdiff(uint64_t a, uint64_t b)
{
	return a < b ? b - a : a - b;
}

/* tisp_ae_lib_bilinear_intp: y on the line (x0, y0) .. (x1, y1) at x, the distances taken unsigned */
static inline u32 t32_ae_lib_bilinear_intp(uint64_t x, uint64_t x0, uint64_t x1, u32 y0, u32 y1)
{
	u32 dy = y1 < y0 ? y0 - y1 : y1 - y0;
	u32 q = (u32)t32_ae_lib_div_64(dy * t32_fixp_absdiff(x, x0), t32_fixp_absdiff(x1, x0));

	return y1 < y0 ? y0 - q : y0 + q;
}

#endif
