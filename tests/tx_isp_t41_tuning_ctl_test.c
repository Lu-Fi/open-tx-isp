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

/* stock tisp_ae_set/get_converge_step offsets: u32 +1636/+1640, u16 +1884/+1886 */
static void ae_converge(void)
{
	unsigned short in[4] = { 3, 0x1234, 7, 0xbeef }, out[4];

	memset(params, 0xa5, sizeof(params));
	assert(t41_ae_converge_set(params, sizeof(params), in) == 0);
	assert(params[1636] == 3 && params[1637] == 0 && params[1638] == 0 && params[1639] == 0);
	assert(params[1640] == 0x34 && params[1641] == 0x12 && params[1643] == 0);
	assert(params[1884] == 7 && params[1886] == 0xef && params[1887] == 0xbe);
	assert(params[1888] == 0xa5 && params[1635] == 0xa5);
	assert(t41_ae_converge_get(params, sizeof(params), out) == 0);
	assert(!memcmp(in, out, sizeof(in)));
	in[2] = 0;
	assert(t41_ae_converge_set(params, sizeof(params), in) == -1);
	assert(params[1884] == 7);
	assert(t41_ae_converge_set(params, 1887, in) == -1);
	assert(t41_ae_converge_get(NULL, sizeof(params), out) == -1);
}

/* stock tisp_ae_get_ev_list (+0x5d0 u64) / at_list (+0x76e u16), 128 bytes */
static void ae_lists(void)
{
	unsigned char out[T41_AE_LIST_BYTES];
	unsigned int i;

	memset(params, 0, sizeof(params));
	for (i = 0; i < 15; ++i) {
		put32(params + 0x5d0 + i * 8, 1000 * (i + 1));
		put32(params + 0x5d4 + i * 8, i);
		put16(params + 0x76e + i * 2, 40 + i);
	}
	memset(out, 0xff, sizeof(out));
	assert(t41_ae_list_get(params, sizeof(params), 0, out) == 0);
	assert(out[0] == 1 && out[1] == 0 && out[4] == 0 && out[7] == 0);
	for (i = 0; i < 15; ++i) {
		unsigned long long v;

		memcpy(&v, out + 8 + i * 8, 8);
		assert(v == ((unsigned long long)i << 32 | 1000 * (i + 1)));
	}
	assert(t41_ae_list_get(params, sizeof(params), 1, out) == 0);
	for (i = 0; i < 15; ++i) {
		unsigned long long v;

		memcpy(&v, out + 8 + i * 8, 8);
		assert(v == 40 + i);
	}
	assert(t41_ae_list_get(params, 0x76e + 29, 1, out) == -1);
}

static unsigned char tmo[T41_TMO_PARAM_BYTES];

static void tmo_curve(void)
{
	unsigned char req[T41_TMO_CURVE_BYTES];
	unsigned int i;

	assert(t41_tmo_curve_offset(0) == 4950 && t41_tmo_curve_offset(2) == 1416 &&
	       t41_tmo_curve_offset(9) == 4230 && t41_tmo_curve_offset(10) == -1);
	memset(tmo, 0, sizeof(tmo));
	memset(req, 0, sizeof(req));
	req[1] = 3;
	for (i = 0; i < 201; ++i)
		put16(req + 2 + i * 2, i * 40);
	assert(t41_tmo_curve_set(tmo, sizeof(tmo), req) == 0);
	assert(tmo[1818 + 2] == 40 && tmo[1818 + 400] == (8000 & 0xff));
	assert(tmo[1817] == 0 && tmo[1818 + 402] == 0);
	memset(req + 2, 0, 402);
	assert(t41_tmo_curve_get(tmo, sizeof(tmo), req) == 0);
	assert(req[2 + 2] == 40 && req[1] == 3);
	put16(req + 2 + 7 * 2, 8192);
	assert(t41_tmo_curve_set(tmo, sizeof(tmo), req) == -1);
	assert(tmo[1818 + 14] == (280 & 0xff));
	req[1] = 10;
	put16(req + 2 + 7 * 2, 1);
	assert(t41_tmo_curve_set(tmo, sizeof(tmo), req) == -1);
	assert(t41_tmo_curve_get(tmo, sizeof(tmo), req) == -1);
	req[1] = 0;
	assert(t41_tmo_curve_set(tmo, sizeof(tmo) - 1, req) == -1);
}

/* stock tisp_s_statis_config_attr: AE 24 @0, AWB 20 @24, AF 28 @44 */
static void statis_config(void)
{
	unsigned char req[T41_STATIS_CONFIG_BYTES], rec[T41_STATIS_CONFIG_BYTES], loc[10];

	memset(req, 0, sizeof(req));
	assert(t41_statis_config_check(req) == 0);
	put32(req + 0, 1); put32(req + 24, 1); put32(req + 44, 1);
	put32(req + 28, 11); put32(req + 32, 21); req[36] = 15; req[37] = 14;
	req[20] = 13; req[23] = 0xc0; req[60] = 1; put16(req + 70, 58);
	assert(t41_statis_config_check(req) == 7);
	t41_statis_awb_location(req, loc);
	assert(loc[0] == 11 && loc[4] == 21 && loc[8] == 15 && loc[9] == 14);
	memset(rec, 0x5a, sizeof(rec));
	t41_statis_config_store(rec, req);
	assert(!memcmp(rec, req, sizeof(rec)));
	/* a disabled part only clears its enable */
	memset(rec, 0x5a, sizeof(rec));
	put32(req + 24, 0);
	assert(t41_statis_config_check(req) == 5);
	t41_statis_config_store(rec, req);
	assert(rec[24] == 0 && rec[27] == 0 && rec[28] == 0x5a && rec[0] == 1);
	put32(req + 44, 2);
	assert(t41_statis_config_check(req) == -1);
	put32(req + 44, 0); put32(req + 16, 2);
	assert(t41_statis_config_check(req) == -1);
	assert(t41_statis_config_check(NULL) == -1);
}

static unsigned int get32(const unsigned char *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24;
}

/* issue 90: GetAeStatistics reply from tisp_ae_hist_last + the AE state */
static void ae_stats_reply(void)
{
	static unsigned char hist[T41_AE_HIST_LAST_BYTES], state[0x2618],
		out[T41_AE_STATS_REPLY_BYTES + 8];
	unsigned int i;

	for (i = 0; i < 256; ++i)
		put32(hist + 4 * i, i * 1000 + 7);
	for (i = 0; i < 5; ++i)
		put32(hist + 1024 + 4 * i, 0xabcd0000U | (i * 13107));
	for (i = 0; i < 225; ++i)
		put32(state + T41_AE_STATE_YZONE + 4 * i, i + 3);
	memset(out, 0xee, sizeof(out));
	assert(!t41_ae_stats_reply(out, hist, state, sizeof(state)));
	for (i = 0; i < 5; ++i)
		assert((unsigned int)(out[2 * i] | out[2 * i + 1] << 8) == i * 13107);
	for (i = 0; i < 256; ++i)
		assert(get32(out + 10 + 4 * i) == i * 1000 + 7);
	for (i = 0; i < 225; ++i)
		assert(get32(out + 1034 + 4 * i) == i + 3);
	assert(out[T41_AE_STATS_REPLY_BYTES] == 0xee);	/* 1934 bytes, no more */
	assert(t41_ae_stats_reply(out, hist, state, T41_AE_STATE_YZONE + 899) == -1);
	assert(t41_ae_stats_reply(NULL, hist, state, sizeof(state)) == -1);
}

static void face_req(unsigned char *r, unsigned int en, unsigned int l,
		unsigned int t, unsigned int rt, unsigned int b, unsigned int tg)
{
	put32(r, en); put32(r + 4, l); put32(r + 8, t);
	put32(r + 12, rt); put32(r + 16, b); put32(r + 20, tg);
}

/* issue 90: Set/GetFaceAe (stock tisp_ae_sepc_area / tisp_ae_api_get_faceae) */
static void face_ae(void)
{
	unsigned char req[T41_FACE_AE_BYTES], out[T41_FACE_AE_BYTES];
	static unsigned char cache[T41_AE_CACHE_BYTES];
	struct t41_face_ae f;

	/* clamping as stock: left >= 20, top >= 10, right <= w-20, bottom <= h-10, target 10..200 */
	face_req(req, 1, 5, 2, 3000, 1700, 250);
	assert(!t41_face_ae_plan(req, 2880, 1620, &f));
	assert(f.roi[0] == 20 && f.roi[1] == 10 && f.roi[2] == 2860 && f.roi[3] == 1610);
	assert(f.roi_ok && f.target == 200 && f.left == 5 && f.right == 3000);
	face_req(req, 1, 1000, 500, 1400, 900, 3);
	assert(!t41_face_ae_plan(req, 2880, 1620, &f) && f.target == 10);
	assert(f.roi[0] == 1000 && f.roi[3] == 900);
	/* stores: changed enable sets the update flag; rect as given, clamped target */
	memset(cache, 0, sizeof(cache));
	t41_face_ae_store_enable(cache, &f);
	assert(get32(cache + T41_AE_CACHE_FACE_EN) == 1 && cache[T41_AE_CACHE_TRIG] == 1);
	cache[T41_AE_CACHE_TRIG] = 0;
	t41_face_ae_store_enable(cache, &f);
	assert(cache[T41_AE_CACHE_TRIG] == 0);		/* unchanged enable: no flag */
	t41_face_ae_store(cache, &f);
	assert(cache[T41_AE_CACHE_FACE_TARGET] == 10);
	assert(get32(cache + 1496) == 1000 && get32(cache + 1500) == 500 &&
	       get32(cache + 1504) == 1400 && get32(cache + 1508) == 900 &&
	       get32(cache + 1512) == 10);
	t41_face_ae_report(out, &f);
	assert(get32(out) == 1 && get32(out + 4) == 1000 && get32(out + 16) == 900 &&
	       get32(out + 20) == 10);
	/* beyond stock: bad enable, empty enabled window refused */
	face_req(req, 2, 100, 100, 200, 200, 50);
	assert(t41_face_ae_plan(req, 2880, 1620, &f) == -1);
	face_req(req, 1, 300, 100, 200, 200, 50);
	assert(t41_face_ae_plan(req, 2880, 1620, &f) == -1);
	face_req(req, 1, 100, 100, 200, 200, 50);
	assert(t41_face_ae_plan(req, 0, 0, &f) == -1);
	/* disable with a zero window is fine (no ROI programmed); GET zeros the rest */
	face_req(req, 0, 0, 0, 0, 0, 0);
	assert(!t41_face_ae_plan(req, 2880, 1620, &f) && !f.roi_ok && f.target == 10);
	t41_face_ae_report(out, &f);
	assert(get32(out) == 0 && get32(out + 4) == 0 && get32(out + 20) == 0);
}

/* issue 90: Set/GetAeExpList (stock tisp_ae_api_set/get_exp_list) */
static void ae_explist(void)
{
	static unsigned char params[0x910], state[0x2618], cache[T41_AE_CACHE_BYTES];
	unsigned char req[T41_AE_EXPLIST_BYTES], out[T41_AE_EXPLIST_BYTES];
	unsigned int i, k;

	memset(req, 0, sizeof(req));
	put32(req, 1);
	for (i = 0; i < 16; ++i)
		for (k = 0; k < 5; ++k)
			put32(req + 4 + 20 * i + 4 * k, 100 * i + k + 1);
	memset(params, 0x11, sizeof(params));
	assert(!t41_ae_explist_set(params, sizeof(params), state, sizeof(state), cache, req));
	assert(get32(params + 1732) == 1 && get32(state + 7536) == 1 && get32(state + 7540) == 0);
	assert(cache[T41_AE_CACHE_TRIG] == 1);
	for (i = 0; i < 16; ++i)
		for (k = 0; k < 4; ++k)
			assert(get32(params + 580 + 16 * i + 4 * k) == 100 * i + k + 1);
	assert(params[580 + 256] == 0x11);		/* column 4 not stored */
	assert(!t41_ae_explist_get(params, sizeof(params), out));
	assert(get32(out) == 1);
	for (i = 0; i < 16; ++i) {
		assert(get32(out + 4 + 20 * i) == 0 && get32(out + 4 + 20 * i + 16) == 0);
		for (k = 1; k < 4; ++k)
			assert(get32(out + 4 + 20 * i + 4 * k) == 100 * i + k + 1);
	}
	/* mode 0: only the mode (and the state flags) change */
	cache[T41_AE_CACHE_TRIG] = 0;
	put32(req, 0); put32(req + 8, 9999);
	assert(!t41_ae_explist_set(params, sizeof(params), state, sizeof(state), cache, req));
	assert(get32(params + 1732) == 0 && get32(params + 584) == 2 && !cache[T41_AE_CACHE_TRIG]);
	put32(req, 2);
	assert(t41_ae_explist_set(params, sizeof(params), state, sizeof(state), cache, req) == -1);
	assert(t41_ae_explist_set(params, 1735, state, sizeof(state), cache, req) == -1);
}

int main(void)
{
	ae_stats_reply();
	face_ae();
	ae_explist();
	ae_converge();
	ae_lists();
	tmo_curve();
	statis_config();
	coefft_wb();
	ae_weight();
	sensor_attr();
	sensor_fps();
	puts("t41 tuning ctl tests passed");
	return 0;
}
