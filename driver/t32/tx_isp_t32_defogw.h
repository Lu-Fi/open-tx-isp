/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 defog weight curve helper of the vendor tisp core:
 * defog_wei_interpcot (per-bin rounded mean of 0..31 weights, made
 * non-decreasing).
 *
 * For every one of n samples, bin idx[i] (0..31) collects val[i]; out[k] is
 * the rounded mean of bin k, clipped to 31, 0 for an empty bin, and never
 * below out[k - 1].
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK): a bin
 * index of 32 or more writes past the vendor's stack arrays (callers pass
 * 0..31; ours refuses nothing, the caller must keep the range); the mean
 * is truncated to its low byte before the clip.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_defogw_oracle).
 */
#ifndef TX_ISP_T32_DEFOGW_H
#define TX_ISP_T32_DEFOGW_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_DEFOG_WEI_BINS	32

static inline void t32_defog_wei_interpcot(u32 n, const u8 *idx, const u8 *val, u8 *out)
{
	u32 sum[T32_DEFOG_WEI_BINS], cnt[T32_DEFOG_WEI_BINS], k, q;

	memset(cnt, 0, sizeof(cnt));
	memset(sum, 0, sizeof(sum));
	for (k = 0; k < n; k++) {
		cnt[idx[k]]++;
		sum[idx[k]] += val[k];
	}
	for (k = 0; k < T32_DEFOG_WEI_BINS; k++) {
		if (!cnt[k]) {
			out[k] = 0;
		} else {
			q = ((cnt[k] >> 1) + sum[k]) / cnt[k] & 0xff;
			out[k] = q < 32 ? (u8)q : 31;
		}
		if (k && out[k] < out[k - 1])
			out[k] = out[k - 1];
	}
}

#endif /* TX_ISP_T32_DEFOGW_H */
