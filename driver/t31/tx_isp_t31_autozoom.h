/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T31_AUTOZOOM_H
#define TX_ISP_T31_AUTOZOOM_H

/*
 * MSCA channel attribute (OEM ds0/ds1/ds2_attr, 13 words, as
 * tisp_channel_attr_set reads it):
 *   0 scaler enable, 1/2 scaler output width/height (input size when off),
 *   3 crop enable, 4/5 crop x/y, 6/7 crop width/height (scaler size when
 *   off), 8..12 the frame crop (channel 0 copy only).
 * The channel writes crop size when cropping, else the scaler size.
 */
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#else
#include <stdint.h>
#include <errno.h>
#endif

#define T31_AUTOZOOM_WORDS 9U   /* 0x24-byte request: channel + 8 words */

/* The size a channel attribute makes the channel write; -EINVAL for a
 * zero size or a crop window outside the scaler output. */
static inline int t31_msca_out_size(const uint32_t *a, uint32_t in_w,
				    uint32_t in_h, uint32_t *w, uint32_t *h)
{
	uint32_t sw = a[0] ? a[1] : in_w;
	uint32_t sh = a[0] ? a[2] : in_h;

	if (!sw || !sh)
		return -EINVAL;
	if (a[3]) {
		if (!a[6] || !a[7] || a[6] > sw || a[4] > sw - a[6] ||
		    a[7] > sh || a[5] > sh - a[7])
			return -EINVAL;
		*w = a[6];
		*h = a[7];
	} else {
		*w = sw;
		*h = sh;
	}
	return 0;
}

/*
 * OEM tisp_s_autozoom_control (0x641f0): request words 1..8 become words
 * 0..7 of the channel's attribute (the frame-crop words stay).  Beyond the
 * OEM: the new attribute must make the channel write the same size as the
 * current one, because the channel's frame buffers are sized for it (the
 * OEM would let a caller overrun them).  Returns 0 and fills next, or
 * -EINVAL.
 */
static inline int t31_autozoom_attr(const uint32_t *cur, const uint32_t *req,
				    uint32_t in_w, uint32_t in_h,
				    uint32_t *next)
{
	uint32_t cw, ch, nw, nh;
	unsigned int i;

	if (t31_msca_out_size(cur, in_w, in_h, &cw, &ch))
		return -EINVAL;
	for (i = 0; i < 13U; i++)
		next[i] = cur[i];
	for (i = 0; i < 8U; i++)
		next[i] = req[1U + i];
	if (t31_msca_out_size(next, in_w, in_h, &nw, &nh))
		return -EINVAL;
	if (nw != cw || nh != ch)
		return -EINVAL;
	return 0;
}

#endif
