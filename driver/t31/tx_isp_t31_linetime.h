/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T31_LINETIME_H
#define TX_ISP_T31_LINETIME_H

/*
 * T31 line time of ispcore_sync_sensor_attr (stock tx-isp-t31.ko 0x7757c, openimp #733).
 * The stock function stores the line time it derives from the active frame rate into the
 * sensor's own attribute (video_in->attr + 0xbc = one_line_expr_in_us, u16) before it builds
 * the sensor-info block for tiziano_sync_sensor_attr:
 *   ((fps & 0xffff) * 1000000) / (fps >> 16) / total_height        (u32 arithmetic, u16 store)
 * fps is the packed frame rate (numerator << 16 | denominator), total_height the attribute's
 * +0xb2.  The stock code divides without checks (a zero numerator or height traps there); this
 * returns 0 for those and the caller keeps the old field.  Plain data, so the host test runs it.
 */
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint16_t u16;
typedef uint32_t u32;
#endif

static inline u16 t31_sync_line_time(u32 fps, u32 total_height)
{
	u32 num = fps >> 16;

	total_height &= 0xffff;
	if (!num || !total_height)
		return 0;
	return (u16)(((fps & 0xffff) * 1000000U) / num / total_height);
}

#endif /* TX_ISP_T31_LINETIME_H */
