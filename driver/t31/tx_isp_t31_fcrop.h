/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T31_FCROP_H
#define TX_ISP_T31_FCROP_H

/*
 * T31 front crop decisions as plain functions (host test:
 * tests/tx_isp_t31_fcrop_test.c).  The MSCA cannot upscale, so a crop
 * window is only accepted when it covers every running channel's output.
 * Beyond stock.
 */
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#endif

/* window may feed an output of out pixels on one axis; upscale_pct = 0
 * never allows an upscale */
static inline bool t31_fcrop_axis_ok(u32 window, u32 out, u32 upscale_pct)
{
	return (u64)window * (100U + upscale_pct) >= (u64)out * 100U;
}

/* One channel against a window.  running: 0x9804 bits; reg: 0x9900 +
 * ch * 0x100 (w << 16 | h); cache_*: size cached by the last set-format.
 * A stopped channel or one without a known size never vetoes.  The size
 * checked is the larger of register and cache.  Returns true if the window
 * fits; out_w and out_h (if not NULL) report the size used. */
static inline bool t31_fcrop_chan_fits(u32 running, int ch, u32 reg,
				       u32 cache_w, u32 cache_h,
				       u32 win_w, u32 win_h, u32 upscale_pct,
				       u32 *out_w, u32 *out_h)
{
	u32 w = (reg >> 16) > cache_w ? (reg >> 16) : cache_w;
	u32 h = (reg & 0xffff) > cache_h ? (reg & 0xffff) : cache_h;

	if (out_w)
		*out_w = w;
	if (out_h)
		*out_h = h;
	if (!(running & (1U << ch)))
		return true;
	if (!w || !h)
		return true;
	return t31_fcrop_axis_ok(win_w, w, upscale_pct) &&
	       t31_fcrop_axis_ok(win_h, h, upscale_pct);
}

/* Channel set-format with an active crop window (win_w x win_h, the
 * channel-0 cache).  req_scaler: the caller's scaler flag (arg2[0]); with
 * a scaler its output out_w x out_h must fit the window, without one the
 * output is the window 1:1 and only the full-sensor size (isp_w x isp_h)
 * cannot be used.  Returns true if the format does not fit. */
static inline bool t31_fcrop_format_misfit(u32 req_scaler, u32 win_w, u32 win_h,
					   u32 isp_w, u32 isp_h,
					   u32 out_w, u32 out_h, u32 upscale_pct)
{
	if (!req_scaler)
		return isp_w > win_w || isp_h > win_h;
	return !t31_fcrop_axis_ok(win_w, out_w, upscale_pct) ||
	       !t31_fcrop_axis_ok(win_h, out_h, upscale_pct);
}

/* Last close: is there a crop window to drop? */
static inline bool t31_fcrop_release_needed(u32 en, u32 ds0_word8, u32 data_b2e04)
{
	return en || ds0_word8 || data_b2e04;
}

#endif
