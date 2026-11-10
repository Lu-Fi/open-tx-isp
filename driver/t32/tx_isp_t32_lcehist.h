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
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_lcehs_oracle).
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

#endif
