/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 fixed-point multiply / divide helpers (vendor fix_point_div*, fix_point_mult2*, fix_point_mult3*).
 */
#ifndef TX_ISP_T33_R13C_H
#define TX_ISP_T33_R13C_H

#include "tx_isp_t33_blk.h"

/* the vendor prints an error through isp_printf when the divisor is 0; a user may hook it */
#ifndef T33_FP_DIV_ZERO
#define T33_FP_DIV_ZERO(line)	do { } while (0)
#endif

/* fix_point_mult2 / fix_point_mult2_64: (a * b) >> s in pieces: high parts shifted back, low parts kept */
static inline unsigned long long t33_fp_mult2(u32 s, unsigned long long a, unsigned long long b)
{
	unsigned long long mask = s ? ~0ULL >> (64 - s) : 0;
	unsigned long long ah = a >> s, bh = b >> s, al = a & mask, bl = b & mask;

	return ((ah * bh) << s) + ah * bl + al * bh + ((al * bl) >> s);
}

static inline unsigned long long t33_fix_point_mult2_64(u32 s, unsigned long long a, unsigned long long b)
{
	return t33_fp_mult2(s, a, b);
}

/* fix_point_mult2_32: the same in 32 bits (shift counts taken modulo 32 like the vendor's srlv/sllv) */
static inline u32 t33_fix_point_mult2_32(u32 s, u32 a, u32 b)
{
	u32 sh = s & 31, mask = 0xffffffffU >> ((0U - s) & 31);
	u32 ah = a >> sh, bh = b >> sh, al = a & mask, bl = b & mask;

	return ((ah * bh) << sh) + (al * bh + ah * bl) + ((al * bl) >> sh);
}

/* fix_point_mult3 / _64: (a * b) * c with the two-step fix_point_mult2 */
static inline unsigned long long t33_fix_point_mult3(u32 s, unsigned long long a, unsigned long long b,
						     unsigned long long c)
{
	return t33_fp_mult2(s, t33_fp_mult2(s, a, b), c);
}

static inline unsigned long long t33_fix_point_mult3_64(u32 s, unsigned long long a, unsigned long long b,
							unsigned long long c)
{
	return t33_fp_mult2(s, t33_fp_mult2(s, a, b), c);
}

static inline u32 t33_fix_point_mult3_32(u32 s, u32 a, u32 b, u32 c)
{
	return t33_fix_point_mult2_32(s, t33_fix_point_mult2_32(s, a, b), c);
}

/* a / b with s fractional bits (shift-subtract of the remainder), 64 bit */
static inline unsigned long long t33_fp_div_core(u32 s, unsigned long long a, unsigned long long b)
{
	unsigned long long q = b ? t32_tp_div64(a, b) : 0;
	unsigned long long rem = a - q * b, rem2, frac = 0;
	u32 i;

	for (i = 0; i < s; i++) {
		rem2 = rem << 1;
		frac <<= 1;
		if (rem2 > b) {
			rem = rem2 - b;
			frac |= 1;
		} else if (rem2 == b) {
			return (q << s) | ((frac | 1) << (s - 1 - i));
		} else {
			rem = rem2;
		}
	}
	return (q << s) | frac;
}

/* fix_point_div (reports a zero divisor) */
static inline unsigned long long t33_fix_point_div(u32 s, unsigned long long a, unsigned long long b)
{
	if (!b)
		T33_FP_DIV_ZERO(68);
	return t33_fp_div_core(s, a, b);
}

/* fix_point_div_64 */
static inline unsigned long long t33_fix_point_div_64(u32 s, unsigned long long a, unsigned long long b)
{
	return t33_fp_div_core(s, a, b);
}

/* fix_point_div_32: 32-bit remainder loop, low word of the result */
static inline u32 t33_fix_point_div_32(u32 s, u32 a, u32 b)
{
	u32 q, rem, rem2, frac = 0, i;

	if (!b)
		T33_FP_DIV_ZERO(230);
	q = b ? a / b : 0;
	rem = a - b * q;
	for (i = 0; i < s; i++) {
		rem2 = rem << 1;
		frac <<= 1;
		if (b < rem2) {
			rem = rem2 - b;
			frac |= 1;
		} else if (b == rem2) {
			frac |= 1;
			frac <<= ((s - 1 - i) & 31);
			break;
		} else {
			rem = rem2;
		}
	}
	return (u32)((unsigned long long)q << s) | frac;
}

#endif /* TX_ISP_T33_R13C_H */
