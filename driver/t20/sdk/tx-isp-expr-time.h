/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SetExpr(MANUAL) time -> integration lines for the T20/T10 Apical setter
 * (IMAGE_TUNING_CID_EXPR_ATTR).  The SDK code took the time only for
 * UNIT_US and divided by attr->one_line_expr_in_us, which jxf23 and most
 * other T20 sensors leave 0 (-EINVAL); UNIT_LINE sent 0 lines.  Here
 * UNIT_LINE takes the time as lines, UNIT_US uses one_line_expr_in_us when
 * the sensor sets it (as the SDK) and otherwise the line rate of the
 * current timing, total_height * fps (the GetExpr fallback's convention).
 * Shared with tests/t20_expr_time_host_test.c.
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

/* fps = num << 16 | den (core->vin.fps).  0 on success, -1 when a time in
 * microseconds cannot be converted (no line time and no timing). */
static inline int t20_expr_time_to_lines(unsigned int unit, unsigned int time,
					 unsigned int one_line_us,
					 unsigned int total_height,
					 unsigned int fps, unsigned int *lines)
{
	uint64_t num = fps >> 16, den = fps & 0xffff;

	if (unit != T20_EXPR_UNIT_US) {
		*lines = time;
		return 0;
	}
	if (one_line_us) {
		*lines = time / one_line_us;
		return 0;
	}
	if (!total_height || !num || !den)
		return -1;
	/* lines = time[us] * total_height * num / (den * 1e6), rounded */
	*lines = (unsigned int)T20_EXPR_DIV_U64((uint64_t)time * total_height * num +
						 den * 500000ULL, den * 1000000ULL);
	if (!*lines && time)
		*lines = 1;
	return 0;
}

#endif /* TX_ISP_EXPR_TIME_H */
