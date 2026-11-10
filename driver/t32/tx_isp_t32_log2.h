/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tisp log2 helpers (vendor fix-point-calc.h of the T32 SDK, firmware 2.2.0):
 *
 *   t32_log2_int_to_fixed          tisp_log2_int_to_fixed 0x1e0ac
 *   t32_log2_fixed_to_fixed        tisp_log2_fixed_to_fixed 0x1e1b0
 *   t32_log2_int_to_fixed_64       tisp_log2_int_to_fixed_64 0x1e1e8
 *   t32_log2_fixed_to_fixed_64     tisp_log2_fixed_to_fixed_64 0x1e3ec
 *
 * log2 of an integer v in fixed point: the integer part is the index of the top set bit; v is normalised to
 * 1.15 (top bit at bit 15) and squared fp times, each square giving one fraction bit (square >= 2.0: bit 1 and
 * >> 16, else >> 15).  The result is (msb << fp) + fraction, then << out with the next out bits of the last
 * mantissa appended ((m & 0x7fff) >> (15 - out)).  v = 0 gives 0.  The fixed_to_fixed forms subtract
 * in_fp << out_fp (the log2 of the input scale): log2(v / 2^in) with out_fp fraction bits.
 *
 * Shift counts follow the vendor machine: 32 bit shifts take the count mod 32 (MIPS sllv/srlv), the 64 bit ones
 * (__ashldi3 / __lshrdi3 of the _64 form) are modelled mod 64.  The vendor returns a 32 bit value (the _64
 * form's high return word is not a result); for out > 15 the _64 form calls __lshrdi3 with a negative count
 * (undefined in libgcc), the oracle does not compare those cases.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_log2_oracle).
 */
#ifndef TX_ISP_T32_LOG2_H
#define TX_ISP_T32_LOG2_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

/* index of the top set bit of a non-zero v (the vendor's 16/8/4/2/1 search) */
static inline u32 t32_log2_msb32(u32 v)
{
	u32 n = 0;

	if (v >= 0x10000U) {
		v >>= 16;
		n += 16;
	}
	if (v >= 0x100U) {
		v >>= 8;
		n += 8;
	}
	if (v >= 0x10U) {
		v >>= 4;
		n += 4;
	}
	if (v >= 4U) {
		v >>= 2;
		n += 2;
	}
	return v != 1 ? n + 1 : n;
}

/* tisp_log2_int_to_fixed(v, fp, out) */
static inline u32 t32_log2_int_to_fixed(u32 v, u32 fp, u32 out)
{
	u32 msb, m, frac = 0;
	int i;

	fp &= 0xff;
	out &= 0xff;
	if (!v)
		return 0;
	msb = t32_log2_msb32(v);
	m = msb < 16 ? v << ((15 - msb) & 31) : v >> ((msb - 15) & 31);
	for (i = 0; i < (int)fp; i++) {
		m *= m;
		frac <<= 1;
		if (m & 0x80000000U) {
			frac += 1;
			m >>= 16;
		} else {
			m >>= 15;
		}
	}
	return ((msb << (fp & 31)) + frac) << (out & 31) | (m & 0x7fff) >> ((15 - out) & 31);
}

/* tisp_log2_fixed_to_fixed(v, in_fp, out_fp) */
static inline u32 t32_log2_fixed_to_fixed(u32 v, u32 in_fp, u32 out_fp)
{
	out_fp &= 0xff;
	return t32_log2_int_to_fixed(v, out_fp, 0) - (in_fp << (out_fp & 31));
}

/* tisp_log2_int_to_fixed_64(v, fp, out) */
static inline u32 t32_log2_int_to_fixed_64(uint64_t v, u32 fp, u32 out)
{
	uint64_t m;
	u32 msb, frac = 0;
	int i;

	fp &= 0xff;
	out &= 0xff;
	if (!v)
		return 0;
	msb = (u32)(v >> 32) ? 32 + t32_log2_msb32((u32)(v >> 32)) : t32_log2_msb32((u32)v);
	m = (int)msb < 16 ? v << ((15 - msb) & 63) : v >> ((msb - 15) & 63);
	for (i = 0; i < (int)fp; i++) {
		m *= m;
		frac <<= 1;
		if (m >> 31) {
			frac += 1;
			m >>= 16;
		} else {
			m >>= 15;
		}
	}
	/* the vendor sums and shifts in 64 bit; for out <= 15 only the low word reaches the result */
	return ((msb << (fp & 31)) + frac) << out | ((u32)m & 0x7fff) >> (15 - out);
}

/* tisp_log2_fixed_to_fixed_64(v, in_fp, out_fp) */
static inline u32 t32_log2_fixed_to_fixed_64(uint64_t v, u32 in_fp, u32 out_fp)
{
	out_fp &= 0xff;
	return t32_log2_int_to_fixed_64(v, out_fp, 0) - (in_fp << (out_fp & 31));
}

#endif
