/* Host test: T30 frame-rate counter and GetEVAttr expr_us (driver/t30/tx_isp_t30_fps_cnt.h,
 * Lu-Fi/openimp#273).  Vendor behaviour (SDK ko _process_fps_cnt 0x248bc, cmos_get_fps 0x24978,
 * cmos_get_lines_per_second 0x24dd0, ae_exposure 0x21728): the filter keeps the last timestamp on every call,
 * converges to the real frame period, and expr_us = integration time * 1e6 / lines per second follows it. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#include "../driver/t30/tx_isp_t30_fps_cnt.h"

#define CK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

/* the lifted body before the fix: the timestamp was stored only when the flag was set */
static void old_process(u32 *c, u8 *flag, u32 now, u32 update)
{
	if (!update)
		return;
	if (*flag) { c[0] = now; *flag = 0; return; }
	c[1] = (c[1] - c[0]) - (c[1] >> 4) + now;
}

int main(void)
{
	const u32 hz = 1000000, vts = 1917, it = 765;
	u32 c[3], o[3], now, i, fps, lps;
	u8 flag = 0, oflag = 0;
	int bad = 0;

	/* init: period 16 x (hz / 15) = 15 fps (cmos_get_fps = 3840 = 0xf00) */
	t30_fps_cnt_init(c, &flag, hz);
	CK(c[0] == 0 && c[1] == (hz / 15) << 4);
	CK(t30_fps_cnt_fps(hz, c[1]) == 0xf00);
	CK(t30_fps_cnt_fps(hz, 0) == 0xf00);
	t30_fps_cnt_init(o, &oflag, hz);

	/* vendor start: the first period is the absolute timestamp, so fps is below the 1792 clamp and the
	 * lines per second sit at 7 fps x vts (b53 vendor: expr_us 56250 for 756 lines = 13440 lines/s) */
	now = 3000000;
	t30_fps_cnt_process(c, &flag, now, 1);
	CK(c[0] == now);
	CK(t30_fps_cnt_fps(hz, c[1]) < 1792);
	CK(t30_fps_lines_per_second(t30_fps_cnt_fps(hz, c[1]), 1920) == 13440);
	CK(t30_expr_us(756, 13440) == 56250);

	/* 25 fps frames: the filter converges (vendor reads 15960 us for 765 lines after ~3 s) */
	for (i = 0; i < 400; i++) {
		now += 40000;
		t30_fps_cnt_process(c, &flag, now, 1);
	}
	fps = t30_fps_cnt_fps(hz, c[1]);
	CK(fps >= 6400 - 40 && fps <= 6400 + 40);
	lps = t30_fps_lines_per_second(fps, vts);
	CK(lps > 47000 && lps < 48500);
	CK(t30_expr_us(it, lps) > 15700 && t30_expr_us(it, lps) < 16300);

	/* expr_us follows the exposure at the settled rate */
	CK(t30_expr_us(2 * it, lps) > t30_expr_us(it, lps) * 19 / 10);
	CK(t30_expr_us(0, lps) == 0 && t30_expr_us(it, 0) == 0);

	/* a frame without update (exposure above the limit) keeps the period but moves the timestamp */
	{
		u32 p = c[1];

		now += 40000;
		t30_fps_cnt_process(c, &flag, now, 0);
		CK(c[1] == p && c[0] == now);
	}
	/* flag set: no update, timestamp stored, flag cleared */
	flag = 1;
	{
		u32 p = c[1];

		now += 40000;
		t30_fps_cnt_process(c, &flag, now, 1);
		CK(c[1] == p && c[0] == now && flag == 0);
	}

	/* the unfixed lifted body never converged: last stays 0, period ~ 16 x absolute time, fps ~ 1 */
	now = 3000000;
	for (i = 0; i < 400; i++) {
		now += 40000;
		old_process(o, &oflag, now, 1);
	}
	CK(t30_fps_cnt_fps(hz, o[1]) < 1792);
	CK(t30_fps_lines_per_second(t30_fps_cnt_fps(hz, o[1]), vts) == 7 * vts);

	printf(bad ? "t30_fps_cnt_host_test FAILED (%d)\n" : "t30_fps_cnt_host_test PASS\n", bad);
	return bad != 0;
}
