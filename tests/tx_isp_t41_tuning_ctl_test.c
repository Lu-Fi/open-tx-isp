/* SPDX-License-Identifier: MIT */
/* T41 tuning-control payloads against the stock g_ctrl/s_ctrl layouts. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_tuning_ctl.h"

static void put16(unsigned char *p, unsigned int v) { p[0] = v; p[1] = v >> 8; }
static void put32(unsigned char *p, unsigned int v)
{
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void sensor_attr(void)
{
	unsigned char video[96] = {0}, attr[512] = {0};
	unsigned int out[T41_SENSOR_ATTR_WORDS];

	put16(attr + 180, 2400);           /* total_width (hts) */
	put16(attr + 182, 1500);           /* total_height (vts) */
	put32(video + 60, 2560);
	put32(video + 64, 1440);
	put32(video + 68, (25U << 16) | 1);
	assert(!t41_sensor_attr_fill(video, attr, out));
	assert(out[0] == 2400 && out[1] == 1500);
	assert(out[2] == ((25U << 16) | 1));
	assert(out[3] == 2560 && out[4] == 1440);
	assert(t41_sensor_attr_fill(NULL, attr, out) == -1);
	assert(t41_sensor_attr_fill(video, NULL, out) == -1);
	assert(sizeof(out) == 20);         /* IMPISPSENSORAttr, 5 x u32 */
}

static void sensor_fps(void)
{
	const unsigned int mode = (25U << 16) | 1;

	assert(t41_sensor_fps_check((15U << 16) | 1, mode) == 0);
	assert(t41_sensor_fps_check((25U << 16) | 1, mode) == 0);
	assert(t41_sensor_fps_check((50U << 16) | 2, mode) == 0);
	assert(t41_sensor_fps_check((30U << 16) | 1, mode) == -2);
	assert(t41_sensor_fps_check((51U << 16) | 2, mode) == -2);
	assert(t41_sensor_fps_check(25U << 16, mode) == -1);
	assert(t41_sensor_fps_check(1, mode) == -1);
	/* no reference: any non-zero rate, extreme fields without overflow */
	assert(t41_sensor_fps_check((60U << 16) | 1, 0) == 0);
	assert(t41_sensor_fps_check(0xffffffffU, 0xffffffffU) == 0);
	assert(t41_sensor_fps_check(0xffff0001U, 0x0001ffffU) == -2);
}

int main(void)
{
	sensor_attr();
	sensor_fps();
	puts("t41 tuning ctl tests passed");
	return 0;
}
