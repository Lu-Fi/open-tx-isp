/* Host test: T20/T10 SetExpr(MANUAL) time -> integration lines
 * (driver/t20/sdk/tx-isp-expr-time.h, issue openimp#130). */
#include <stdio.h>
#include "../driver/t20/sdk/tx-isp-expr-time.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main(void)
{
	unsigned int l = 12345, fps25 = 25 << 16 | 1, fps15 = 30 << 16 | 2;

	/* UNIT_LINE: the time is the line count (the SDK sent 0) */
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_LINE, 240, 0, 0, 0, &l) == 0 && l == 240);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_LINE, 30, 29, 1125, fps25, &l) == 0 && l == 30);
	/* UNIT_US with the sensor's line time: the SDK division */
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 10000, 29, 1125, fps25, &l) == 0 && l == 344);
	/* UNIT_US without it (jxf23): the fallback line time GetExpr reports, 1e6 / (vts x fps) rounded
	 * (openimp #744).  1125 at 25 fps = 28125 lines/s = 35.56 us -> 36: 10 ms = 277, 40 ms = 1111, 20 us -> 1 */
	CHECK(t20_expr_fallback_line_us(1125, fps25) == 36);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 10000, 0, 1125, fps25, &l) == 0 && l == 277);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 40000, 0, 1125, fps25, &l) == 0 && l == 1111);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 20, 0, 1125, fps25, &l) == 0 && l == 1);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 1, 0, 1125, fps25, &l) == 0 && l == 1);
	/* fractional fps 30/2 = 15: 1125 * 15 = 16875 lines/s = 59.26 us -> 59, 65535 us -> 1110 */
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 65535, 0, 1125, fps15, &l) == 0 && l == 1110);
	/* line shorter than 0.5 us: 1 us (never 0), 64-bit intermediate */
	CHECK(t20_expr_fallback_line_us(0xffff, 60 << 16 | 1) == 1);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 65535, 0, 0xffff, 60 << 16 | 1, &l) == 0 && l == 65535);
	/* #744 jxf23 15 fps (vts 2250): 29.63 us -> 30 (the SDK's "30, // 29.6us", vendor-compatible jxf23 build),
	 * and GetExpr lines x line_us -> SetExpr comes back exactly (was 674 -> 660, below 2 antiflicker periods) */
	{
		unsigned int fps = 15 << 16 | 1, us = t20_expr_fallback_line_us(2250, fps), n, rt = 0;

		CHECK(us == 30);
		for (n = 2; n <= 2246; n++)
			if (t20_expr_time_to_lines(T20_EXPR_UNIT_US, n * us, 0, 2250, fps, &l) != 0 || l != n)
				rt++;
		CHECK(rt == 0);
		CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 674 * 30, 0, 2250, fps, &l) == 0 && l == 674);
	}
	/* no line time and no timing: refused, as the SDK */
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 1000, 0, 0, fps25, &l) == -1);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 1000, 0, 1125, 0, &l) == -1);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 1000, 0, 1125, 25 << 16, &l) == -1);

	printf("t20_expr_time_host_test: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
