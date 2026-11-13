/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LCE histogram helpers (32-bin histograms of the local contrast enhancement):
 *
 *   t32_lce_compress_data_part_1  lce_compress_data_part_1 0x4beb0: dst[i] = src[i] >> 5 (u32 -> u16, 32 bins);
 *                                 returns the last unshortened src[31] >> 5
 *   t32_lce_pdf_to_cdf_16bit      lce_pdf_to_cdf_16bit 0x53bb8: 20-byte CDF of a 32-bin u16 PDF; the first 8 CDF
 *                                 points follow single bins, the last 12 pairs of bins; before each point the
 *                                 running sum is clamped to 32767, the point is sum >> 7 (so cdf[0] = 0);
 *                                 returns the final (unclamped) sum
 *   t32_lce_head_tail_search      lce_head_tail_search 0x53ab0 (u32 bins), lce_head_tail_search_16bit 0x53a20
 *                                 (u16 bins): head = first bin where the running sum from bin 0 exceeds
 *                                 (lo & 0xff) << 6 (0 if none), tail = first bin from bin 31 down to head where
 *                                 the sum from the top exceeds (hi & 0xff) << 6 (31 if none); out[0] = head,
 *                                 out[1] = tail; returns head
 *
 *   t32_lce_compress_data         lce_compress_data 0x585e0: mode 0: 32 u16 bins >> 2 to u8; mode 1:
 *                                 lce_compress_data_part_1 (u32 bins); other: 32 u16 bins >> 5 to u16;
 *                                 returns the last unshortened value
 *   t32_lce_converge_16bit        lce_16bit_data_converge_16bit 0x53dd8: moves each of the 20 target points
 *                                 (5th argument, updated in place) toward the current curve point: unchanged
 *                                 inside cur +- lo, else cur +- the gain-scaled distance ((d * gain + 64) >> 7
 *                                 style rounding, 16 bits), clamped to [lo, hi]; gain, lo, hi are signed bytes.
 *                                 The vendor's v0/v1 at return are leftovers (void function), not compared.
 *
 *   t32_lce_std_hist_transform    lce_std_hist_transform 0x531c4: bins = bins * mul >> shift; if the (signed)
 *                                 sum is below 0x8000 the deficit is spread: deficit / 32 (arithmetic) on all
 *                                 bins, one more on the first deficit % 32 bins (a negative signed sum gives
 *                                 a wrapped deficit, vendor flow kept); void, first argument unused
 *   t32_lce_self_light_correct_local_test  lce_self_light_correct_local_test 0x53d54: per point of the 20-point
 *                                 curve, out = base + (gain * (in - base) >> 7) with the up gain table when
 *                                 in > base (cfg byte 8 enables), the down table when in < base (cfg byte 12),
 *                                 else out = base; base = the fixed column table t32_data_d6b50
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_lcehs_oracle, t32_lcecv_oracle, t32_lcest_oracle).
 */
#ifndef TX_ISP_T32_LCEHIST_H
#define TX_ISP_T32_LCEHIST_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_LCE_BINS	32

static inline u32 t32_lce_compress_data_part_1(const u32 *src, unsigned short *dst)
{
	u32 v = 0;
	int i;

	for (i = 0; i < T32_LCE_BINS; i++) {
		v = src[i] >> 5;
		dst[i] = (unsigned short)v;
	}
	return v;
}

static inline u32 t32_lce_cdf_clamp(u32 sum)
{
	return sum >> 15 ? 32767 : sum;
}

static inline u32 t32_lce_pdf_to_cdf_16bit(const unsigned short *pdf, unsigned char *cdf)
{
	u32 sum = 0;
	int i;

	for (i = 0; i < 8; i++) {
		sum = t32_lce_cdf_clamp(sum);
		cdf[i] = (unsigned char)(sum >> 7);
		sum += pdf[i];
	}
	for (i = 0; i < 12; i++) {
		sum = t32_lce_cdf_clamp(sum);
		cdf[8 + i] = (unsigned char)(sum >> 7);
		sum += pdf[8 + 2 * i] + pdf[9 + 2 * i];
	}
	return sum;
}

/* bin(i) of a u32 (w16 = 0) or u16 (w16 = 1) histogram */
static inline u32 t32_lce_bin(const void *hist, int w16, int i)
{
	return w16 ? ((const unsigned short *)hist)[i] : ((const u32 *)hist)[i];
}

static inline u32 t32_lce_head_tail_search_w(const void *hist, int w16, u32 lo, u32 hi, u32 *out)
{
	u32 lo_th = (lo & 0xff) << 6, hi_th = (hi & 0xff) << 6, acc = 0, head = 0, tail = 31;
	int i;

	for (i = 0; i < T32_LCE_BINS; i++) {
		acc += t32_lce_bin(hist, w16, i);
		if (acc > lo_th) {
			head = i;
			break;
		}
	}
	acc = 0;
	for (i = T32_LCE_BINS - 1; i >= (int)head; i--) {
		acc += t32_lce_bin(hist, w16, i);
		if (acc > hi_th) {
			tail = i;
			break;
		}
	}
	out[0] = head;
	out[1] = tail;
	return head;
}

/* lce_head_tail_search(hist u32[32], lo, hi, out) */
static inline u32 t32_lce_head_tail_search(const u32 *hist, u32 lo, u32 hi, u32 *out)
{
	return t32_lce_head_tail_search_w(hist, 0, lo, hi, out);
}

/* lce_head_tail_search_16bit(hist u16[32], lo, hi, out) */
static inline u32 t32_lce_head_tail_search_16bit(const unsigned short *hist, u32 lo, u32 hi, u32 *out)
{
	return t32_lce_head_tail_search_w(hist, 1, lo, hi, out);
}

/* ---- group 2: compress modes, curve convergence ---- */

/* lce_compress_data(mode, src, dst) */
static inline u32 t32_lce_compress_data(u32 mode, const void *src, void *dst)
{
	const unsigned short *s16 = src;
	u32 v = 0;
	int i;

	if (mode == 1)
		return t32_lce_compress_data_part_1(src, dst);
	for (i = 0; i < T32_LCE_BINS; i++) {
		if (mode == 0) {
			v = s16[i] >> 2;
			((unsigned char *)dst)[i] = (unsigned char)v;
		} else {
			v = s16[i] >> 5;
			((unsigned short *)dst)[i] = (unsigned short)v;
		}
	}
	return v;
}

/* gain-scaled distance: low 32 bits of d * gain, >> 7 rounded by bit 6, 16 bits */
static inline int t32_lce_conv_step(u32 d, int gain)
{
	u32 p = (d & 0xffff) * (u32)gain;

	return (int)((((p >> 6) & 1) + (p >> 7)) & 0xffff);	/* vendor sra; equal after the 16-bit mask */
}

/* lce_16bit_data_converge_16bit(gain, lo, hi, cur[20], tgt[20]) */
static inline void t32_lce_converge_16bit(u32 gain, u32 lo, u32 hi, const unsigned char *cur, unsigned char *tgt)
{
	int g = (signed char)gain, l = (signed char)lo, h = (signed char)hi, i, c, t, d;

	for (i = 0; i < 20; i++) {
		c = cur[i];
		t = tgt[i];
		if (c + l < t) {
			d = t32_lce_conv_step((u32)(t - c), g);
			if (d < l)
				tgt[i] = (unsigned char)(c + l);
			else if (h < d)
				tgt[i] = (unsigned char)(c + h);
			else
				tgt[i] = (unsigned char)(c + d);
		} else if (l + t < c) {
			d = t32_lce_conv_step((u32)(c - t), g);
			if (d < l)
				tgt[i] = (unsigned char)(c - l);
			else if (h < d)
				tgt[i] = (unsigned char)(c - h);
			else
				tgt[i] = (unsigned char)(c - d);
		}
	}
}

/* ---- group 3: histogram normalisation, local light correction ---- */

/* lce_std_hist_transform(unused, hist u32[32], mul, shift) */
static inline void t32_lce_std_hist_transform(u32 *hist, u32 mul, u32 shift)
{
	u32 sum = 0, deficit, rem;
	int i, q;

	for (i = 0; i < T32_LCE_BINS; i++) {
		hist[i] = hist[i] * mul >> (shift & 31);
		sum += hist[i];
	}
	if ((int32_t)sum >= 0x8000)
		return;
	deficit = 0x8000 - sum;
	q = (int32_t)deficit >> 5;
	rem = deficit & 0x1f;
	for (i = 0; i < (int)rem; i++)
		hist[i] += 1;
	for (i = 0; i < T32_LCE_BINS; i++)
		hist[i] += (u32)q;
}

/* t32_data_d6b50: column base of the 20 curve points */
static const unsigned char t32_lce_curve_base[20] = {
	0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x50,
	0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
};

#define T32_LCE_CFG_UP_EN	8	/* byte offsets in the config block */
#define T32_LCE_CFG_DOWN_EN	12

/* lce_self_light_correct_local_test(in[20], out[20], cfg, up[20], down[20]) */
static inline void t32_lce_self_light_correct_local_test(const unsigned char *in, unsigned char *out,
							 const unsigned char *cfg, const unsigned char *up,
							 const unsigned char *down)
{
	int i, b, d, v;

	for (i = 0; i < 20; i++) {
		b = t32_lce_curve_base[i];
		d = in[i] - b;
		v = 0;
		if (d > 0 && cfg[T32_LCE_CFG_UP_EN])
			v = up[i] * d >> 7;
		else if (d < 0 && cfg[T32_LCE_CFG_DOWN_EN])
			v = down[i] * d >> 7;
		out[i] = (unsigned char)(b + v);
	}
}

#endif
