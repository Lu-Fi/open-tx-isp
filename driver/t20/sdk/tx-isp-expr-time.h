/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SetExpr(MANUAL) time -> integration lines for the T20/T10 Apical setter
 * (IMAGE_TUNING_CID_EXPR_ATTR).  The SDK code took the time only for
 * UNIT_US and divided by attr->one_line_expr_in_us, which jxf23 and most
 * other T20 sensors leave 0 (-EINVAL); UNIT_LINE sent 0 lines.  Here
 * UNIT_LINE takes the time as lines, UNIT_US uses one_line_expr_in_us when
 * the sensor sets it (as the SDK) and otherwise the fallback line time
 * t20_expr_fallback_line_us() that GetExpr reports for such a sensor.
 * Shared with tests/t20_expr_time_host_test.c.
 *
 * openimp #744: the setter used the exact line rate (jxf23 15 fps: 1e6 /
 * (2250 * 15) = 29.63 us) while GetExpr reported the truncated 29 us, so
 * a caller's lines x 29 us came back 2.2 % short (674 -> 660 lines).  The
 * OEM antiflicker quantisation (get_quantised_integration_time, vendor
 * tx-isp-t20 3.12.0 0x1f238: floor(time / quantum) x quantum, quantum =
 * fps x vts / 100 = 337 lines at 50 Hz) then dropped every manual time
 * just below a multiple of the quantum to one period less: 670 -> 337
 * lines (half the exposure, mean Y 85 vs vendor 125 at gain 4, 5000 K).
 * Now both directions use ONE integer line time, as the SDK does with a
 * sensor that declares it (time / one_line_expr_in_us), rounded to the
 * nearest us like the SDK's own 29.6 us sensors ("30, // 29.6us") and the
 * vendor-compatible jxf23 build (one_line_expr_in_us 30): lines -> us ->
 * lines is exact again.
 */
#ifndef TX_ISP_EXPR_TIME_H
#define TX_ISP_EXPR_TIME_H

#ifdef __KERNEL__
#include <linux/math64.h>
#define T20_EXPR_DIV_U64(a, b) div_u64((a), (b))
#else
#include <stdint.h>
#define T20_EXPR_DIV_U64(a, b) ((a) / (b))
#endif

#define T20_EXPR_UNIT_LINE	0	/* ISP_CORE_EXPR_UNIT_LINE */
#define T20_EXPR_UNIT_US	1	/* ISP_CORE_EXPR_UNIT_US */

/* Line time in us for a sensor without one_line_expr_in_us: 1e6 / (vts x
 * fps) rounded to the nearest us, at least 1, at most 0xffff; 0 = no
 * timing.  fps = num << 16 | den. */
static inline unsigned int t20_expr_fallback_line_us(unsigned int total_height,
						     unsigned int fps)
{
	uint64_t num = fps >> 16, den = fps & 0xffff, lps2, us;

	if (!total_height || !num || !den)
		return 0;
	/* 2 x lines per second, so the rounding below stays in integers */
	lps2 = T20_EXPR_DIV_U64((uint64_t)total_height * num * 2, den);
	if (!lps2)
		return 0;
	us = T20_EXPR_DIV_U64(2000000ULL + lps2 / 2, lps2);
	if (!us)
		us = 1;
	return us > 0xffff ? 0xffff : (unsigned int)us;
}

/* fps = num << 16 | den (core->vin.fps).  0 on success, -1 when a time in
 * microseconds cannot be converted (no line time and no timing). */
static inline int t20_expr_time_to_lines(unsigned int unit, unsigned int time,
					 unsigned int one_line_us,
					 unsigned int total_height,
					 unsigned int fps, unsigned int *lines)
{
	if (unit != T20_EXPR_UNIT_US) {
		*lines = time;
		return 0;
	}
	if (!one_line_us)
		one_line_us = t20_expr_fallback_line_us(total_height, fps);
	if (!one_line_us)
		return -1;
	*lines = time / one_line_us;	/* the SDK division */
	if (!*lines && time)
		*lines = 1;
	return 0;
}

#endif /* TX_ISP_EXPR_TIME_H */
