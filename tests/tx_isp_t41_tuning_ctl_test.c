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

static void coefft_wb(void)
{
	const unsigned short neutral[3] = {1024, 1024, 1024};
	const unsigned short edge[3] = {0, 2047, 1};
	const unsigned short wide[3] = {1024, 2048, 1024};

	assert(t41_bcsh_offset_rgb_ok(neutral));
	assert(t41_bcsh_offset_rgb_ok(edge));
	assert(!t41_bcsh_offset_rgb_ok(wide));
	assert(!t41_bcsh_offset_rgb_ok(NULL));
	assert(T41_BCSH_RGB_OFFSET == 280);  /* stock params+280/282/284 */
}


static unsigned int get32(const unsigned char *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24;
}

static void gamma_attr(void)
{
	static unsigned char info[T41_GAMMA_INFO_BYTES], par[0x238];
	unsigned char attr[T41_GAMMA_ATTR_BYTES], out[T41_GAMMA_ATTR_BYTES];
	unsigned char srgb[536], rec[260], hdr[260];
	unsigned int i;

	assert(sizeof(attr) == 4 + 2 * 129 + 2);
	memset(info, 0xee, sizeof(info));
	memset(par, 0x11, sizeof(par));
	memset(srgb, 0xa1, sizeof(srgb));
	memset(rec, 0xa2, sizeof(rec));
	memset(hdr, 0xa3, sizeof(hdr));
	memset(attr, 0, sizeof(attr));

	/* type 0: attribute stored, manual flag cleared, nothing else */
	info[T41_GAMMA_INFO_MANUAL] = 1;
	assert(t41_gamma_attr_set(info, sizeof(info), par, sizeof(par), attr,
				  srgb, rec, hdr) == 0);
	assert(info[T41_GAMMA_INFO_MANUAL] == 0);
	assert(info[T41_GAMMA_INFO_TABLE] == 0xee && par[300] == 0x11);

	/* presets: the stock driver maps type 2 to its rec709 and 3 to its
	 * hdr table; params curve, strengths and strength 255 follow */
	for (i = 1; i <= 3; ++i) {
		attr[0] = i;
		assert(t41_gamma_attr_set(info, sizeof(info), par, sizeof(par),
					  attr, srgb, rec, hdr) == 1);
		assert(info[T41_GAMMA_INFO_MANUAL] == 1);
		assert(info[T41_GAMMA_INFO_TABLE] == (i == 1 ? 0xa1 : i == 2 ? 0xa2 : 0xa3));
		assert(info[T41_GAMMA_INFO_TABLE + 257] == info[T41_GAMMA_INFO_TABLE]);
		assert(info[T41_GAMMA_INFO_TABLE + 258] == 0xee);	/* 258 bytes only */
		assert(par[300] == info[T41_GAMMA_INFO_TABLE] && par[300 + 257] == par[300]);
		assert(par[299] == 0x11);
		assert(par[558] == 255 && par[567] == 255 && par[557] == par[300]);
		assert(get32(info + T41_GAMMA_INFO_STRENGTH) == 255);
		assert(get32(info + T41_GAMMA_INFO_ATTR) == i);
	}
	/* user table */
	attr[0] = 4;
	for (i = 0; i < 129; ++i) {
		attr[4 + 2 * i] = i;
		attr[5 + 2 * i] = i & 0xf;
	}
	assert(t41_gamma_attr_set(info, sizeof(info), par, sizeof(par), attr,
				  srgb, rec, hdr) == 1);
	assert(info[T41_GAMMA_INFO_TABLE + 2] == 1 && info[T41_GAMMA_INFO_TABLE + 3] == 1);
	assert(par[300 + 4] == 2);
	/* the stored attribute is the caller's, byte for byte */
	assert(!memcmp(info + 4, attr, sizeof(attr)));
	/* get: stored type, the programmed curve in the table */
	memset(info + T41_GAMMA_INFO_TABLE, 0x5c, 258);
	assert(t41_gamma_attr_get(info, sizeof(info), out) == 0);
	assert(get32(out) == 4 && out[4] == 0x5c && out[4 + 257] == 0x5c);
	assert(out[261] == 0x5c && out[262] == attr[262] && out[263] == attr[263]);
	/* back to the calibrated curve */
	attr[0] = 0;
	assert(t41_gamma_attr_set(info, sizeof(info), par, sizeof(par), attr,
				  srgb, rec, hdr) == 0);
	assert(info[T41_GAMMA_INFO_MANUAL] == 0);

	/* refused: type > 4, a 13-bit user entry, short buffers, NULL */
	attr[0] = 5;
	assert(t41_gamma_attr_set(info, sizeof(info), par, sizeof(par), attr,
				  srgb, rec, hdr) == -1);
	attr[0] = 4;
	attr[4 + 2 * 100] = 0x00;
	attr[5 + 2 * 100] = 0x10;		/* 4096 */
	assert(t41_gamma_attr_set(info, sizeof(info), par, sizeof(par), attr,
				  srgb, rec, hdr) == -1);
	assert(info[T41_GAMMA_INFO_MANUAL] == 0);
	attr[5 + 2 * 100] = 0x0f;		/* 4095 is the 12-bit maximum */
	assert(t41_gamma_attr_set(info, sizeof(info), par, sizeof(par), attr,
				  srgb, rec, hdr) == 1);
	assert(t41_gamma_attr_set(info, T41_GAMMA_INFO_BYTES - 1, par,
				  sizeof(par), attr, srgb, rec, hdr) == -1);
	assert(t41_gamma_attr_set(info, sizeof(info), par, 567, attr, srgb,
				  rec, hdr) == -1);
	assert(t41_gamma_attr_set(info, sizeof(info), NULL, sizeof(par), attr,
				  srgb, rec, hdr) == -1);
	assert(t41_gamma_attr_get(info, sizeof(info) - 1, out) == -1);
}

static void ccm_attr(void)
{
	static unsigned char info[T41_CCM_INFO_BYTES];
	unsigned char block[T41_CCM_BLOCK_BYTES], out[T41_CCM_BLOCK_BYTES];
	short m[9];
	unsigned int i;

	memset(info, 0x77, sizeof(info));
	memset(block, 0, sizeof(block));
	block[0] = 1;			/* manual */
	block[1] = 0;			/* no saturation */
	/* libimp words: value * 1024 as 13 bits, bit 13 = negative */
	put32(block + 4, 1024);
	put32(block + 8, 0x2000 | ((0x2000 - 512) & 0x1fff));
	put32(block + 12, 8190);
	put32(block + 16, 32);		/* exactly half of the 1/64 step */
	put32(block + 20, 31);
	put32(block + 24, 0);
	put32(block + 28, 100000);	/* far above 13 bits: clipped */
	put32(block + 32, 0xffffffffU);	/* -1 as a raw word */
	put32(block + 36, 0x80000000U);
	assert(sizeof(block) == 40);
	assert(t41_ccm_block_set(info, sizeof(info), block) == 0);
	assert(info[T41_CCM_INFO_MANUAL] == 1);
	assert(info[T41_CCM_INFO_MANUAL + 1] == 0x77 && info[191] == 0x77);
	assert(info[95] == 0x77 && info[136] == 0x77);
	assert(t41_ccm_block_get(info, sizeof(info), out) == 0);
	assert(!memcmp(out, block, sizeof(block)));
	/* stock tisp_round_int64(word, 6): (word + 32) >> 6, half up */
	t41_ccm_manual_matrix(info, m);
	assert(m[0] == 16);
	assert(m[1] == ((0x2000 | ((0x2000 - 512) & 0x1fff)) + 32) / 64);	/* 160 */
	assert(m[2] == 128);		/* 8190/64 = 127.97 -> 128 */
	assert(m[3] == 1 && m[4] == 0 && m[5] == 0);
	assert(m[6] == 1563);		/* 100000/64 = 1562.5, half up */
	assert(m[7] == 0);		/* -1 >> 6 = -1, + ((-1 >> 5) & 1) = 0 */
	assert(m[8] == -8192);		/* INT_MIN clipped */
	/* any other first byte clears the flag; the block is still stored */
	block[0] = 2;
	assert(t41_ccm_block_set(info, sizeof(info), block) == 0);
	assert(info[T41_CCM_INFO_MANUAL] == 0 && info[96] == 2);
	block[0] = 0;
	assert(t41_ccm_block_set(info, sizeof(info), block) == 0);
	assert(info[T41_CCM_INFO_MANUAL] == 0);
	/* clip at the top */
	block[0] = 1;
	put32(block + 4, 600000);
	assert(t41_ccm_block_set(info, sizeof(info), block) == 0);
	t41_ccm_manual_matrix(info, m);
	assert(m[0] == 8191);
	for (i = 1; i < 9; ++i)
		assert(m[i] >= -8192 && m[i] <= 8191);
	assert(t41_ccm_block_set(info, sizeof(info) - 1, block) == -1);
	assert(t41_ccm_block_get(NULL, sizeof(info), out) == -1);
}

static void ccm_route(void)
{
	/* stock tisp_s_ccm_attr: BCSH bypassed (bit 15) -> CCM block, -1 only
	 * with the CCM bypassed too (bit 9); BCSH active and CCM bypassed ->
	 * BCSH matrix only; both active -> CCM block and -1 */
	assert(t41_ccm_route(0x8000) == 0);
	assert(t41_ccm_route(0x8000 | 0x200) == -1);
	assert(t41_ccm_route(0x200) == 1);
	assert(t41_ccm_route(0) == -1);
	assert(t41_ccm_route(0xfc000000U) == -1);
	assert(t41_ccm_route(0xfc008000U) == 0);
}

int main(void)
{
	coefft_wb();
	ae_weight();
	sensor_attr();
	sensor_fps();
	gamma_attr();
	ccm_attr();
	ccm_route();
	puts("t41 tuning ctl tests passed");
	return 0;
}
