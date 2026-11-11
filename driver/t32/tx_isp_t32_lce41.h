/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LCE scalar per frame path: the scalar leaves of Tisp_lce_soft_t41 (0x563a0) that
 * tx_isp_t32_lce.h does not have yet:
 *   lce_pdf_to_cdf_16bit (0x53bb8), lce_self_light_correct_local_test (0x53d54),
 *   lce_16bit_data_converge_16bit (0x53dd8), lce_std_hist_transform (0x531c4),
 *   lce_hist_method_16bit (0x53324).
 * The other leaves (lce_hist_method, lce_hist_filter_and_judge(_16bit), lce_pdf_to_cdf,
 * lce_self_light_correct_local, lce_16bit_data_converge, tisp_lce_std_hist_transform) are the ones
 * of tx_isp_t32_lce.h.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK): lce_hist_method_16bit takes
 * its centre bin n from 8 bits and writes its 32 entry stack arrays up to index n (n >= 32
 * overflows them; ours supports n <= 31 only and refuses nothing, the caller must keep the
 * range) and reads 1024 / n from a 32 entry table; its divides are signed (a ramp sum of 0
 * gives the vendor's divide-by-zero result: -1 or 1 by the sign of the numerator).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_lce41l_oracle).
 */
#ifndef TX_ISP_T32_LCE41_H
#define TX_ISP_T32_LCE41_H

#include "tx_isp_t32_lce.h"

#ifndef __KERNEL__
typedef int32_t s32;	/* identical redefinitions are fine (C11) */
typedef int8_t s8;
#endif

/* the vendor's MIPS div: quotient / remainder of signed 32 bit, divide by zero as the reference models it */
static inline s32 t32_lce41_sdiv(s32 n, s32 d, s32 *rem)
{
	if (d == 0) {
		*rem = n;
		return n < 0 ? 1 : -1;
	}
	if (n == (s32)0x80000000 && d == -1) {
		*rem = 0;
		return n;
	}
	*rem = n % d;
	return n / d;
}

/*
 * lce_pdf_to_cdf_16bit: one pdf row of 32 u16 bins -> 20 cdf bytes; running sum clamped to 32767 when
 * it reaches 32768 (before each output), output = sum >> 7; outputs 0..7 step one bin, 8..19 two bins.
 */
static inline void t32_lce_pdf_to_cdf_16bit(const u16 *pdf, u8 *out)
{
	u32 sum = 0, j;

	for (j = 0; j < 20; j++) {
		if (sum >> 15)
			sum = 32767;
		out[j] = (u8)(sum >> 7);
		sum += j < 8 ? pdf[j] : (u32)pdf[2 * j - 8] + pdf[2 * j - 7];
	}
}

/*
 * lce_self_light_correct_local_test: 20 bytes; d = in - ofs[j]; d > 0 is scaled by up[j] / 128 when
 * en[8] is set, d < 0 by down[j] / 128 when en[12] is set (arithmetic shift), otherwise d counts 0:
 * out = low byte of ofs[j] + scaled d (so a disabled side gives ofs[j], not in).
 */
static inline void t32_lce_self_light_correct_local_test(const u8 *in, u8 *out, const u8 *en, const u8 *up,
							  const u8 *down)
{
	u32 j;

	for (j = 0; j < 20; j++) {
		s32 o = t32_lce_slc_ofs[j], d = (s32)in[j] - o, s = 0;

		if (d > 0 && en[8])
			s = (s32)(up[j] * d) >> 7;
		else if (d < 0 && en[12])
			s = (s32)down[j] * d >> 7;
		out[j] = (u8)(o + s);
	}
}

/*
 * lce_16bit_data_converge_16bit: like t32_lce_16bit_data_converge but gain, lo and hi are sign extended
 * from 8 bits; the step d = round7(|cur - target| (16 bit) * gain) (low 16 bits) is compared signed:
 * d < lo gives lo, else hi < d gives hi.
 */
static inline void t32_lce_16bit_data_converge_16bit(u32 gain, u32 lo, u32 hi, const u8 *target, u8 *cur)
{
	s32 g = (s8)gain, l = (s8)lo, h = (s8)hi;
	u32 i;

	for (i = 0; i < 20; i++) {
		s32 t = target[i], c = cur[i], d;

		if (t + l < c) {
			d = (s32)(t32_lce_round7(((u32)(c - t) & 0xffff) * (u32)g) & 0xffff);
			cur[i] = (u8)(d < l ? t + l : h < d ? t + h : t + d);
		} else if (l + c < t) {
			d = (s32)(t32_lce_round7(((u32)(t - c) & 0xffff) * (u32)g) & 0xffff);
			cur[i] = (u8)(d < l ? t - l : h < d ? t - h : t - d);
		}
	}
}

/*
 * lce_std_hist_transform: 32 u32 bins scaled by mult >> shift (unsigned, 32 bit); when the sum is
 * below 0x8000 (signed) the gap is spread: +1 on the first gap % 32 bins, + gap >> 5 on every bin.
 */
static inline void t32_lce_std_hist_transform32(u32 *h, u32 mult, u32 shift)
{
	u32 sum = 0, gap, add, i;

	for (i = 0; i < 32; i++) {
		h[i] = h[i] * mult >> (shift & 31);
		sum += h[i];
	}
	if (!((s32)sum < 0x8000))
		return;
	gap = 0x8000 - sum;
	add = (u32)((s32)gap >> 5);
	for (i = 0; i < (gap & 0x1f); i++)
		h[i]++;
	for (i = 0; i < 32; i++)
		h[i] += add;
}

/*
 * lce_hist_method_16bit (parameter words as struct t32_lce_hm16_par): tent ramps a (c1, slope sa) and
 * b (c3, slope sb) rising to the centre bin n (e1 / e2 there) and falling (31 - i) behind it,
 * sa = (a * 1024/n) >> 10, sb = (b * 1024/n) >> 10 (signed); g = ((hist - 1024) * k8 >> 7) + 1024,
 * m = (u16)(signed min(a, g)), S = sum m.  S <= 0x8000: m + (0x8000 - S) * b / sum(b) (signed); else
 * (m * (2^20 / ((S << 5 >> 14) + 1))) >> sh (unsigned).  The rest 0x8000 - total is spread
 * (rest << 5 >> 10 on every bin, rest % 32 on bin 31).
 */
static inline void t32_lce_hist_method_16bit(const u16 *hist, u16 *out, const struct t32_lce_hm16_par *p)
{
	u32 n = p->n & 0xff, recip = t32_lce_hm16_recip[n & 31];
	s32 sa = (s32)((p->a & 0xffff) * recip) >> 10, sb = (s32)((p->b & 0xff) * recip) >> 10;
	u32 a[32], b[32], bsum = 0, sum = 0, tot = 0, i;
	s32 rem, q, r;
	u16 m[32], buf[32];

	for (i = 0; i < 32; i++) {
		u32 x = i < n ? i : 31 - i;

		a[i] = i == n ? p->e1 & 0xffff : (p->c1 & 0xffff) + (u32)sa * x;
		b[i] = i == n ? p->e2 & 0xffff : (p->c3 & 0xff) + (u32)sb * x;
		bsum += b[i];
	}
	for (i = 0; i < 32; i++) {
		s32 g = ((s32)((hist[i] - 1024U) * (p->k8 & 0xff)) >> 7) + 1024;

		if ((s32)a[i] < g)
			g = (s32)a[i];
		m[i] = (u16)g;
		sum += m[i];
	}
	rem = (s32)(0x8000U - sum);
	if (rem >= 0) {
		for (i = 0; i < 32; i++) {
			buf[i] = (u16)(t32_lce41_sdiv((s32)((u32)rem * b[i]), (s32)bsum, &r) + m[i]);
			tot += buf[i];
		}
	} else {
		u32 k = (u32)t32_lce41_sdiv((s32)(1U << T32_LCE_HM16_SHIFT), ((s32)(sum << 5) >> 14) + 1, &r);

		for (i = 0; i < 32; i++) {
			buf[i] = (u16)(m[i] * k >> (p->sh & 31));
			tot += buf[i];
		}
	}
	rem = (s32)(0x8000U - tot);
	q = (s32)((u32)rem << 5) >> 10;
	t32_lce41_sdiv(rem, 32, &r);
	for (i = 0; i < 32; i++)
		buf[i] = (u16)(buf[i] + q);
	buf[31] = (u16)(buf[31] + r);
	memcpy(out, buf, sizeof(buf));
}

#endif /* TX_ISP_T32_LCE41_H */
