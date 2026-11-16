/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) fixed-point helpers (binary oracle block r13B): the log2 family, 64-bit rounding / clipping,
 * the tone-map ratio and log2 step counter and the 64-bit AE divide.  The log2 functions have the same flow as
 * the shared T32 port (tx_isp_t32_log2.h), which the oracle checks against the T33 vendor code.
 */
#ifndef TX_ISP_T33_R13B_H
#define TX_ISP_T33_R13B_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t32_log2.h"

/* tisp_log2_int_to_fixed */
static inline u32 t33_log2_i2f(u32 v, u32 fp, u32 out)
{
	return t32_log2_int_to_fixed(v, fp, out);
}

/* tisp_log2_fixed_to_fixed */
static inline u32 t33_log2_f2f(u32 v, u32 in_fp, u32 out_fp)
{
	return t32_log2_fixed_to_fixed(v, in_fp, out_fp);
}

/* tisp_log2_int_to_fixed_64 */
static inline u32 t33_log2_i2f_64(unsigned long long v, u32 fp, u32 out)
{
	return t32_log2_int_to_fixed_64(v, fp, out);
}

/* tisp_log2_fixed_to_fixed_64 */
static inline u32 t33_log2_f2f_64(unsigned long long v, u32 in_fp, u32 out_fp)
{
	return t32_log2_fixed_to_fixed_64(v, in_fp, out_fp);
}

/* tisp_round_int64: x >> n rounded by the last dropped bit; n outside 1..62 returns x */
static inline long long t33_round64(long long x, u32 n)
{
	if (n - 1 >= 62)
		return x;
	return (x >> n) + ((x >> (n - 1)) & 1);
}

/* tisp_ratio: blend weight w (0..255, 128 = a) between a (below) and b (above) */
static inline u32 t33_ratio(u32 w, u32 a, u32 b)
{
	u32 x = w & 0xff;

	if (x == 128)
		return a;
	if (x & 0x80)
		return a + (((x - 128) * (b - a)) >> 7);
	return (x * a) >> 7;
}

/* tisp_clip: signed 64-bit x limited to lo, then to hi */
static inline long long t33_clip64(long long x, long long lo, long long hi)
{
	if (x < lo)
		x = lo;
	if (x > hi)
		x = hi;
	return x;
}

/* tisp_tmo_log2: number of doublings (at most 17) of the step until a + step (16 bit) reaches b */
static inline u8 t33_tmo_log2(u32 a, u32 b)
{
	u32 n = 0, v = 1;

	a &= 0xffff;
	b &= 0xffff;
	if (a >= b)
		return 0;
	for (;;) {
		v = (v << 1) & 0xffff;
		n++;
		if (((a + v) & 0xffff) >= b)
			break;
		if (n == 17)
			break;
	}
	return (u8)n;
}

/* tisp_ae_lib_div_64: n / d, 0 for d = 0; only the low word of d divides (the vendor's __div64_32) */
#ifdef __KERNEL__
#include <asm/div64.h>
#else
extern u32 __div64_32(unsigned long long *n, u32 base);
#endif

static inline unsigned long long t33_ae_div64(unsigned long long n, u32 dlo, u32 dhi)
{
	if (!(dlo | dhi))
		return 0;
	if (!(n >> 32))
		return (u32)n / dlo;
	__div64_32(&n, dlo);	/* kernel 64 / 32 division, n = quotient */
	return n;
}

/*
 * Vendor rodata tables the two functions below read (the driver supplies the real contents):
 * __pow2_lut (33 words, 2^(i/32) in Q30) and lut_9 / lut_13 / lut_17 / lut_25 (258 s16 each, one per size).
 */
extern u32 t33_pow2_lut[33];
extern short t33_sin_lut9[258];
extern short t33_sin_lut13[258];
extern short t33_sin_lut17[258];
extern short t33_sin_lut25[258];

/* tisp_math_exp2: 2^(x / 2^fb) in Q(out), the fraction from the 33 point table (interpolated for fb >= 6) */
static inline u32 t33_math_exp2(u32 x, u32 fb, u32 out)
{
	u32 f, ip, v, lo, hi, s, i, r;

	fb &= 0xff;
	out &= 0xff;
	f = ((1U << (fb & 31)) - 1) & x;
	ip = x >> (fb & 31);
	if (fb < 6) {
		v = t33_pow2_lut[f << (5 - fb)];
		return v >> ((30 - out - ip) & 31);
	}
	s = fb - 5;
	i = f >> (s & 31);
	lo = t33_pow2_lut[i];
	hi = t33_pow2_lut[i + 1];
	r = f & ((1U << (s & 31)) - 1);
	v = lo + (u32)(((unsigned long long)(hi - lo) * r) >> s);
	return v >> ((30 - out - ip) & 31);
}

/*
 * tisp_sin: cnt + 1 samples (cnt = 8, 12, 16 or 24, each with its own table) at the step 8 * n / cnt, linearly
 * interpolated between table entries 1024 apart, rounded.  Other cnt: the vendor prints an error and then
 * reads through a null table (not reproduced; we return).
 */
static inline void t33_sin(u32 n, u32 cnt, short *out)
{
	const short *lut;
	u32 s, j, idx = 0;
	int lim = 1024, base = 0, a, v;

	if (cnt == 8)
		lut = t33_sin_lut9;
	else if (cnt == 12)
		lut = t33_sin_lut13;
	else if (cnt == 16)
		lut = t33_sin_lut17;
	else if (cnt == 24)
		lut = t33_sin_lut25;
	else
		return;
	s = (n << 3) / cnt;
	for (j = 0; j <= cnt; j++) {
		a = (int)(j * s);
		while (a >= lim) {
			base = lim;
			idx++;
			lim += 1024;
		}
		a -= base;
		v = lut[idx + 1] * a + lut[idx] * (1024 - a);
		out[j] = (short)((v >> 10) + ((v >> 9) & 1));
	}
}

#endif /* TX_ISP_T33_R13B_H */
