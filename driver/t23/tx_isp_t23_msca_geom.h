/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T23_MSCA_GEOM_H
#define TX_ISP_T23_MSCA_GEOM_H

/*
 * Geometry rules of one T23 MSCA output channel (docs/MSCA_GEOMETRY.md).
 *
 * The channel record (msca + ch * 56) describes the path
 *   input window (+0x10 left, +0x14 top, +0x18 w, +0x1c h; the front crop)
 *   -> scaler output (+0x20 w, +0x24 h)
 *   -> output crop inside the scaler output (+0x28 l, +0x2c t, +0x30 w, +0x34 h)
 * and the output crop is what the MSCA writes into the frame buffer, which
 * user space sized for the channel's format (pix width x height).
 *
 * The stock module programs whatever it is given.  Three cases stall the
 * MSCA output for good (no channel completes another frame, every later
 * STREAMOFF times out, only a reboot helps; the front crop window even
 * survives the session because the channel record keeps it locked):
 *   - the scaler output is larger than the input window (no upscaling),
 *   - the output crop leaves the scaler output,
 *   - the output crop is larger than the frame buffer (overrun).
 * These are refused with -EINVAL instead (beyond stock).
 *
 * Pure helpers, shared with the host test tests/tx_isp_t23_msca_geom_test.c.
 */

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint32_t u32;
#endif

struct t23_msca_geom {
	u32 win_left, win_top, win_width, win_height;	/* input window */
	u32 scl_width, scl_height;			/* scaler output */
	u32 crop_left, crop_top, crop_width, crop_height; /* output crop */
};

#define T23_MSCA_GEOM_OK		0
#define T23_MSCA_GEOM_EMPTY		1	/* a zero size */
#define T23_MSCA_GEOM_WINDOW		2	/* window outside the sensor */
#define T23_MSCA_GEOM_UPSCALE		3	/* scaler output > window */
#define T23_MSCA_GEOM_CROP		4	/* crop outside the scaler output */
#define T23_MSCA_GEOM_BUFFER		5	/* crop larger than the buffer */

/* a + b <= limit without wrapping */
static inline int t23_msca_fits(u32 a, u32 b, u32 limit)
{
	return a <= limit && b <= limit - a;
}

/*
 * Check one channel.  sensor_* is the sensor picture, buf_* the frame
 * buffer size of the channel (0 = not known yet, not checked).
 */
static inline int t23_msca_geom_check(const struct t23_msca_geom *g,
				      u32 sensor_width, u32 sensor_height,
				      u32 buf_width, u32 buf_height)
{
	if (!g->win_width || !g->win_height || !g->scl_width ||
	    !g->scl_height || !g->crop_width || !g->crop_height)
		return T23_MSCA_GEOM_EMPTY;
	if (!t23_msca_fits(g->win_left, g->win_width, sensor_width) ||
	    !t23_msca_fits(g->win_top, g->win_height, sensor_height))
		return T23_MSCA_GEOM_WINDOW;
	if (g->scl_width > g->win_width || g->scl_height > g->win_height)
		return T23_MSCA_GEOM_UPSCALE;
	if (!t23_msca_fits(g->crop_left, g->crop_width, g->scl_width) ||
	    !t23_msca_fits(g->crop_top, g->crop_height, g->scl_height))
		return T23_MSCA_GEOM_CROP;
	if ((buf_width && g->crop_width > buf_width) ||
	    (buf_height && g->crop_height > buf_height))
		return T23_MSCA_GEOM_BUFFER;
	return T23_MSCA_GEOM_OK;
}

static inline const char *t23_msca_geom_reason(int r)
{
	switch (r) {
	case T23_MSCA_GEOM_OK: return "ok";
	case T23_MSCA_GEOM_EMPTY: return "zero size";
	case T23_MSCA_GEOM_WINDOW: return "input window outside the sensor";
	case T23_MSCA_GEOM_UPSCALE: return "scaler output larger than the input window";
	case T23_MSCA_GEOM_CROP: return "crop outside the scaler output";
	case T23_MSCA_GEOM_BUFFER: return "crop larger than the frame buffer";
	}
	return "?";
}

#endif /* TX_ISP_T23_MSCA_GEOM_H */
