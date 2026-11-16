/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "r12E": sensor ops of the ISP core (vendor calling convention). */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r12E.h"

u8 *g_ispcore = (u8 *)0;

u32 bo_sensor_alloc_analog_gain(u32 gain, u8 *blk, u32 unused)
{
	(void)unused;
	return t33_sensor_alloc_again(g_ispcore, gain, blk);
}

void bo_sensor_set_analog_gain(u32 gain, u8 *blk)
{
	t33_sensor_set_again(g_ispcore, gain, blk);
}

void bo_sensor_set_integration_time(u32 value, u8 *blk)
{
	t33_sensor_set_itime(g_ispcore, value, blk);
}

u32 bo_sensor_fps_control(u32 unused, u8 *blk)
{
	(void)unused;
	return t33_sensor_fps_ctl(g_ispcore, blk);
}
