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

static unsigned char params[0x910], state[0x2618];

static void ae_weight(void)
{
	unsigned char attr[T41_AE_WEIGHT_ATTR_BYTES], out[T41_AE_WEIGHT_ATTR_BYTES];
	unsigned char enables[2] = {0, 0};
	unsigned int i;

	memset(params, 0x55, sizeof(params));
	memset(state, 0x55, sizeof(state));
	memset(attr, 0, sizeof(attr));
	/* Disabled tables: only the enables change (stock api_ae_set_weight). */
	assert(!t41_ae_weight_set(params, sizeof(params), state, sizeof(state), enables, attr));
	assert(params[0x4ee] == 0x55 && params[0x82e] == 0x55 && state[0x2528] == 0x55);
	put32(attr, 1);
	put32(attr + 4, 1);
	for (i = 0; i < 225; ++i) {
		attr[8 + i] = i % 9;               /* roi 0..8 */
		attr[233 + i] = 8 - i % 9;         /* weight 0..8 */
	}
	assert(!t41_ae_weight_set(params, sizeof(params), state, sizeof(state), enables, attr));
	assert(enables[0] == 1 && enables[1] == 1);
	for (i = 0; i < 225; ++i) {
		assert(params[0x4ee + i] == i % 9);
		assert(state[0x2528 + i] == 8 - i % 9);
		assert(params[0x82e + i] == 8 - i % 9);
	}
	assert(params[0x4ee - 1] == 0x55 && params[0x82e + 225] == 0x55);
	assert(state[0x2528 - 1] == 0x55 && state[0x2528 + 225] == 0x55);
	assert(!t41_ae_weight_get(params, sizeof(params), enables, out));
	assert(!memcmp(out, attr, 458) && out[458] == 0 && out[459] == 0);

	/* Refused, nothing written: out of range, bad enable, meter-breaking. */
	attr[233] = 9;
	assert(t41_ae_weight_set(params, sizeof(params), state, sizeof(state), enables, attr) == -1);
	attr[233] = 8;
	put32(attr, 2);
	assert(t41_ae_weight_set(params, sizeof(params), state, sizeof(state), enables, attr) == -1);
	put32(attr, 0);
	memset(attr + 233, 0, 225);           /* all-zero weights */
	assert(t41_ae_weight_set(params, sizeof(params), state, sizeof(state), enables, attr) == -1);
	put32(attr, 1);
	put32(attr + 4, 0);
	memset(attr + 8, 8, 225);             /* ROI covers everything: no background */
	assert(t41_ae_weight_set(params, sizeof(params), state, sizeof(state), enables, attr) == -1);
	memset(attr + 8, 0, 225);             /* empty ROI: no foreground */
	assert(t41_ae_weight_set(params, sizeof(params), state, sizeof(state), enables, attr) == -1);
	assert(enables[0] == 1 && enables[1] == 1 && params[0x82e] == 8);
	assert(t41_ae_weight_set(params, 0x82e, state, sizeof(state), enables, attr) == -1);
}

int main(void)
{
	ae_weight();
	sensor_attr();
	sensor_fps();
	puts("t41 tuning ctl tests passed");
	return 0;
}
