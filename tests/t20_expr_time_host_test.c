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
	/* UNIT_US without it (jxf23): line rate of the timing.  1125 lines at 25 fps = 28125 lines/s:
	 * 10 ms = 281.25 -> 281, 40 ms = 1125 (one frame), 20 us -> 0.56 -> 1 */
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 10000, 0, 1125, fps25, &l) == 0 && l == 281);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 40000, 0, 1125, fps25, &l) == 0 && l == 1125);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 20, 0, 1125, fps25, &l) == 0 && l == 1);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 1, 0, 1125, fps25, &l) == 0 && l == 1);
	/* fractional fps 30/2 = 15: 1125 * 15 = 16875 lines/s, 65535 us -> 1105.9 -> 1106 (no overflow) */
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 65535, 0, 1125, fps15, &l) == 0 && l == 1106);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 65535, 0, 0xffff, 60 << 16 | 1, &l) == 0 && l == 257690);	/* 64-bit intermediate */
	/* no line time and no timing: refused, as the SDK */
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 1000, 0, 0, fps25, &l) == -1);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 1000, 0, 1125, 0, &l) == -1);
	CHECK(t20_expr_time_to_lines(T20_EXPR_UNIT_US, 1000, 0, 1125, 25 << 16, &l) == -1);

	printf("t20_expr_time_host_test: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
