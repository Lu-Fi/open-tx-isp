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
 * BEYOND VENDOR (#281): lce_hist_method_16bit takes its centre bin n from 8 bits and writes its
 * 32 entry stack arrays up to index n (n >= 32 overflows them) and reads 1024 / n from a 32 entry
 * table; ours clamps n to 31 (n <= 31 bit-exact).  Vendor behaviour kept: its divides are signed (a ramp sum of 0
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
	u32 n = (p->n & 0xff) > 31 ? 31 : p->n & 0xff, recip = t32_lce_hm16_recip[n];	/* BEYOND VENDOR (#281): n clamped */
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

/*
 * Tisp_lce_soft_t41 (0x563a0): the scalar form of Tisp_lce_soft_t41_simd (t32_lce_t41 of tx_isp_t32_lce.h),
 * same working buffer layout (w, T32_LCE_T41_*), par = the parameter block, mode = the LCE mode byte.
 * Global part: the 576 tile histograms are added onto the u32 histogram at A + 277 (not cleared first),
 * lce_std_hist_transform (unsigned, par +4 / +8), judge (par[284] == 1: par[285]), lce_hist_method ->
 * pdf -> cdf, optional self light correction (par[294] == 1), converge towards the target (not in mode 4),
 * new target.  Per tile (18 rows x 32): tisp_lce_std_hist_transform with the quadrant gain (par +68,
 * quadrant by par[12] / par[13]) and shift 9, 16 bit judge (par[229] == 1: par[230]), threshold byte at
 * T32_LCE_T41_THR, lce_hist_method_16bit, lce_pdf_to_cdf_16bit, optional local test correction (par[239]),
 * the selected tile (par[110] / par[111]) is copied to A + 637 / + 701 / + 721, converge_16bit against the
 * tile's previous row (not in mode 4), new previous row, and the result row is the blend
 * round7((128 - par[112]) (16 bit) * row + par[112] * global cdf) clipped to 255.
 * The vendor presets the judge byte to 15; both judges always overwrite it (the preset is dropped).
 * The rodata shifts d6c66 / d6c67 are both 20.  Supported: thresholds (par[285], par[230]) 0..31 (the vendor
 * overflows the stack arrays of the hist methods above; ours clamps, beyond vendor #260 / #281).  The vendor's 64 byte stack clear
 * and 72 byte parameter block clear are kept as local zeroing.
 */
#define T32_LCE41_SH	20U	/* rodata d6c66 and d6c67 */

static inline void t32_lce_soft_t41(const u8 *par, u8 *w, u32 mode)
{
	u8 *tgt = w + T32_LCE_T41_A, *histb = w + T32_LCE_T41_A + 277, *pdfb = w + T32_LCE_T41_A + 405;
	u8 *cdf = w + T32_LCE_T41_A + 533, *cur = w + T32_LCE_T41_A + 553;
	u32 hist[32], pdf[32], hm[18], row, t, g, i;
	u16 h16[32], over = 0;
	u8 thr, c16[20], r16[20];

	mode &= 0xff;
	memset(hm, 0, sizeof(hm));
	memset(w + T32_LCE_T41_A + 21, 0, 128);
	memset(w + T32_LCE_T41_A + 149, 0, 128);
	memcpy(hist, histb, 128);
	for (g = 0; g < 18; g++)
		for (t = 0; t < 32; t++)
			for (i = 0; i < 32; i++)
				hist[i] += (u32)w[g * 2048 + t * 64 + 2 * i] | (u32)w[g * 2048 + t * 64 + 2 * i + 1] << 8;
	t32_lce_std_hist_transform32(hist, t32_lce_t41_rd32(par + 4), t32_lce_t41_rd32(par + 8));
	memcpy(histb, hist, 128);
	t32_lce_hist_filter_and_judge(hist, &thr);
	if (par[284] == 1)
		thr = par[285];
	w[T32_LCE_T41_A - 2] = thr;
	hm[1] = thr;
	hm[3] = par[289];
	hm[4] = par[290];
	hm[5] = par[291];
	hm[6] = par[292];
	hm[7] = par[293];
	hm[8] = par[289] << 3;
	hm[9] = par[290] << 3;
	hm[10] = (par[289] + par[290]) << 3;
	hm[11] = par[291] + par[292];
	hm[12] = T32_LCE41_SH - 6;
	t32_lce_hist_method32(hist, pdf, (const struct t32_lce_hm16_par *)hm, T32_LCE41_SH, T32_LCE41_SH);
	memcpy(pdfb, pdf, 128);
	t32_lce_pdf_to_cdf_raw(pdf, cdf);
	if (par[294] == 1) {
		hm[2] = par[297];
		hm[3] = par[298];
		t32_lce_self_light_correct_local(cdf, cur, (const u8 *)hm, par + 299, par + 319);
	}
	if (mode != 4)
		t32_lce_16bit_data_converge(par[339], par[340], par[341], tgt, cur);
	memcpy(tgt, cur, 20);

	for (row = 0; row < 18; row++) {
		for (t = 0; t < 32; t++) {
			u32 tile = (row * 32 + t) & 0xffff, quad = (row < par[12] ? 0 : 2) + (t < par[13] ? 0 : 1);
			u32 a = (u32)(128 - par[112]) & 0xffff, b = par[112];
			u16 *th = (u16 *)(w + tile * 64);
			u8 *prev = w + T32_LCE_T41_PREV + 20 * tile, *out = w + T32_LCE_T41_OUT + 20 * tile;

			memcpy(h16, th, 64);
			t32_lce_std_hist_transform(h16, t32_lce_t41_rd32(par + 68 + 4 * quad), 9, &over);
			memcpy(th, h16, 64);
			t32_lce_hist_filter_and_judge_16bit(h16, &thr);
			if (par[229] == 1)
				thr = par[230];
			w[T32_LCE_T41_THR + tile] = thr;
			hm[1] = thr;
			hm[3] = par[234];
			hm[4] = par[235];
			hm[5] = par[236];
			hm[6] = par[237];
			hm[7] = par[238];
			hm[8] = par[234] << 3;
			hm[9] = par[235] << 3;
			hm[10] = (par[234] + par[235]) << 3;
			hm[11] = par[236] + par[237];
			hm[12] = T32_LCE41_SH - 6;
			t32_lce_hist_method_16bit(h16, h16, (const struct t32_lce_hm16_par *)hm);
			t32_lce_pdf_to_cdf_16bit(h16, c16);
			if (par[239]) {
				hm[2] = par[242];
				hm[3] = par[243];
				t32_lce_self_light_correct_local_test(c16, r16, (const u8 *)hm, par + 244, par + 264);
			} else {
				memcpy(r16, c16, 20);
			}
			if (par[110] == row && par[111] == t) {
				memcpy(w + T32_LCE_T41_A + 637, h16, 64);
				memcpy(w + T32_LCE_T41_A + 701, c16, 20);
				memcpy(w + T32_LCE_T41_A + 721, r16, 20);
			}
			if (mode != 4)
				t32_lce_16bit_data_converge_16bit(par[119], par[120], par[121], prev, r16);
			memcpy(prev, r16, 20);
			for (i = 0; i < 20; i++) {
				u32 v = t32_lce_round7((a * r16[i] + b * cur[i]) & 0xffff);

				out[i] = (u8)(v < 256 ? v : 255);
			}
		}
	}
}

#endif /* TX_ISP_T32_LCE41_H */
