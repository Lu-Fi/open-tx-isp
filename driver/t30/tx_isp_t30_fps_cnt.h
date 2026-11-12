/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T30 frame-rate counter of the APICAL CMOS state (host-testable,
 * tests/t30_fps_cnt_host_test.c).  Source: tx-isp-t30 H20190523a (SDK ko)
 *   _init_fps_cnt        0x24928: [0] = 0, [4] = (timer frequency / 15) << 4
 *   _process_fps_cnt     0x248bc: now = system_timer_timestamp();
 *                          if (update && !flag) [4] = ([4] - [0]) - ([4] >> 4) + now;
 *                          [0] = now; flag = 0;      (the store is on EVERY path)
 *   cmos_get_fps         0x24978: [4] ? (freq << 12) / [4] & 0xffff : 3840
 *   cmos_get_lines_per_second 0x24dd0: max(fps, 1792) * (ISP +0x50 u16) >> 8
 *   ae_exposure (get)    0x21728: expr_us = system item 24 (integration time)
 *                          * 1000000 / lines per second   (GetEVAttr expr_us)
 * The lifted _process_fps_cnt stored the timestamp only on the "flag set" path,
 * so [0] stayed 0, [4] grew with the absolute time (~16 x now), the frame
 * rate read ~1 fps and the clamp made lines per second constant: expr_us
 * never followed the exposure (Lu-Fi/openimp#273).
 *
 * c[0] = last timestamp (us), c[1] = filtered frame period x 16 (us), flag = byte at +8.
 */
#ifndef TX_ISP_T30_FPS_CNT_H
#define TX_ISP_T30_FPS_CNT_H

static inline void t30_fps_cnt_init(u32 *c, u8 *flag, u32 timer_hz)
{
	c[0] = 0;
	c[1] = (timer_hz / 15) << 4;
	*flag = 0;
}

static inline void t30_fps_cnt_process(u32 *c, u8 *flag, u32 now, u32 update)
{
	if (update && !*flag)
		c[1] = (c[1] - c[0]) - (c[1] >> 4) + now;
	c[0] = now;
	*flag = 0;
}

/* cmos_get_fps: frame rate in Q8 */
static inline u32 t30_fps_cnt_fps(u32 timer_hz, u32 period16)
{
	if (!period16)
		return 0xf00;
#ifdef __KERNEL__
	return (u32)div64_u64((u64)timer_hz << 12, period16) & 0xffff;
#else
	return (u32)(((u64)timer_hz << 12) / period16) & 0xffff;
#endif
}

/* cmos_get_lines_per_second */
static inline u32 t30_fps_lines_per_second(u32 fps_q8, u32 total_lines)
{
	if (fps_q8 < 1792)
		fps_q8 = 1792;
	return (fps_q8 * total_lines) >> 8;
}

/* ae_exposure get: expr_us for the integration time in lines */
static inline u32 t30_expr_us(u32 integration_time, u32 lines_per_second)
{
	if (!lines_per_second)
		return 0;
	return (u32)(((u64)integration_time * 1000000ULL) / lines_per_second);
}

#endif /* TX_ISP_T30_FPS_CNT_H */
