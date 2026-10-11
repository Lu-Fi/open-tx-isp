/* SPDX-License-Identifier: MIT */
/*
 * T41 AutoZoom (0x08000077), MaskBlock (0x08000074) and ScalerLv
 * (0x080000a6): request decoding against the stock libt41-firmware 1.2.6
 * routines (tisp_s/g_autozoom_control, tisp_s/g_mscaler_mask_block_attr,
 * tisp_msca_set_mask, tisp_set_scaler_level_control_set) and the
 * live/deferred decision of the open driver.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_msca_ctl.h"

static void put32(unsigned char *p, int v) { t41_msca_wr32(p, (unsigned int)v); }

static void zoom_set(unsigned char *req, unsigned int ch, int en, int l,
		     int t, int w, int h)
{
	put32(req + (0 * 3 + ch) * 4, en);
	put32(req + (1 * 3 + ch) * 4, l);
	put32(req + (2 * 3 + ch) * 4, t);
	put32(req + (3 * 3 + ch) * 4, w);
	put32(req + (4 * 3 + ch) * 4, h);
}

static void test_zoom(void)
{
	unsigned char desc[78], req[60], out[60];
	unsigned int changed;

	memset(desc, 0, sizeof(desc));
	memset(req, 0, sizeof(req));
	/* IMPISPAutoZoom: en[3], left[3], top[3], width[3], height[3] */
	zoom_set(req, 0, 1, 480, 270, 1920, 1080);
	zoom_set(req, 1, 0, 9, 9, 9, 9);
	zoom_set(req, 2, 1, 0, 0, 1280, 720);
	assert(t41_msca_zoom_check(req, 2880, 1620) == 0);
	changed = t41_msca_zoom_store(desc, req, 2880, 1620);
	assert(changed == 7U);
	/* output 0: lock + window at +8..+16, stride 26 bytes */
	assert(t41_msca_rd16(desc + 8) == 1);
	assert(t41_msca_rd16(desc + 10) == 480);
	assert(t41_msca_rd16(desc + 12) == 270);
	assert(t41_msca_rd16(desc + 14) == 1920);
	assert(t41_msca_rd16(desc + 16) == 1080);
	/* output 1 disabled: full input, lock byte untouched (stock) */
	assert(t41_msca_rd16(desc + 26 + 8) == 0);
	assert(t41_msca_rd16(desc + 26 + 10) == 0);
	assert(t41_msca_rd16(desc + 26 + 12) == 0);
	assert(t41_msca_rd16(desc + 26 + 14) == 2880);
	assert(t41_msca_rd16(desc + 26 + 16) == 1620);
	assert(t41_msca_rd16(desc + 52 + 8) == 1);
	assert(t41_msca_rd16(desc + 52 + 14) == 1280);
	/* the bytes outside +8..+16 are not touched */
	assert(desc[0] == 0 && desc[4] == 0 && desc[18] == 0 && desc[24] == 0);

	/* same request again: nothing changes */
	assert(t41_msca_zoom_store(desc, req, 2880, 1620) == 0);

	/* disable output 0: window becomes the full input, lock stays 1 */
	zoom_set(req, 0, 0, 0, 0, 0, 0);
	assert(t41_msca_zoom_store(desc, req, 2880, 1620) == 1U);
	assert(t41_msca_rd16(desc + 8) == 1);
	assert(t41_msca_rd16(desc + 14) == 2880);

	/* get: left/top/width/height per output, en words untouched */
	memset(out, 0xa5, sizeof(out));
	t41_msca_zoom_get(desc, out);
	assert(t41_msca_rds32(out + 0) == (int)0xa5a5a5a5);
	assert(t41_msca_rds32(out + (3 * 3 + 0) * 4) == 2880);
	assert(t41_msca_rds32(out + (4 * 3 + 0) * 4) == 1620);
	assert(t41_msca_rds32(out + (3 * 3 + 2) * 4) == 1280);
	assert(t41_msca_rds32(out + (4 * 3 + 2) * 4) == 720);
	assert(t41_msca_rds32(out + (1 * 3 + 1) * 4) == 0);

	/* window checks (open driver; stock programs anything) */
	memset(req, 0, sizeof(req));
	zoom_set(req, 0, 1, 0, 0, 2880, 1620);
	assert(t41_msca_zoom_check(req, 2880, 1620) == 0);
	zoom_set(req, 0, 1, 1, 0, 2880, 1620);
	assert(t41_msca_zoom_check(req, 2880, 1620) == -22);
	zoom_set(req, 0, 1, 0, 1, 2880, 1620);
	assert(t41_msca_zoom_check(req, 2880, 1620) == -22);
	zoom_set(req, 0, 1, -1, 0, 100, 100);
	assert(t41_msca_zoom_check(req, 2880, 1620) == -22);
	zoom_set(req, 0, 1, 0, 0, 0, 100);
	assert(t41_msca_zoom_check(req, 2880, 1620) == -22);
	zoom_set(req, 0, 1, 0x1000, 0, 16, 16);	/* > 12-bit x */
	assert(t41_msca_zoom_check(req, 8192, 8192) == -22);
	zoom_set(req, 0, 1, 0, 0, 16, 16);
	assert(t41_msca_zoom_check(req, 0, 0) == -22);	/* sensor unknown */
	/* a disabled output's garbage is not checked */
	zoom_set(req, 0, 0, -5, -5, -5, -5);
	assert(t41_msca_zoom_check(req, 0, 0) == 0);
	/* the 2x zoom prudynt-style: window smaller than the output */
	zoom_set(req, 0, 1, 720, 405, 1440, 810);
	assert(t41_msca_zoom_check(req, 2880, 1620) == 0);
}

struct pair { unsigned int value, reg; };
struct sink { struct pair p[64]; int n; int fail_at; };

static int sink_write(void *ctx, unsigned int value, unsigned int reg)
{
	struct sink *s = ctx;

	if (s->fail_at >= 0 && s->n == s->fail_at)
		return -5;
	assert(s->n < 64);
	s->p[s->n].value = value;
	s->p[s->n].reg = reg;
	s->n++;
	return 0;
}

static void mask_req(unsigned char *r, unsigned int ch, unsigned int pi,
		     unsigned int en, unsigned int top, unsigned int left,
		     unsigned int w, unsigned int h, unsigned int y,
		     unsigned int u, unsigned int v)
{
	memset(r, 0, 24);
	r[0] = (unsigned char)ch;
	r[1] = (unsigned char)pi;
	r[2] = (unsigned char)en;
	t41_msca_wr16(r + 4, top);
	t41_msca_wr16(r + 6, left);
	t41_msca_wr16(r + 8, w);
	t41_msca_wr16(r + 10, h);
	put32(r + 12, 1);		/* IMPISP_MASK_TYPE_YUV */
	r[16] = 1; r[17] = 2; r[18] = 3;	/* r,g,b: ignored by the kernel */
	r[19] = (unsigned char)y;
	r[20] = (unsigned char)u;
	r[21] = (unsigned char)v;
}

static void test_mask(void)
{
	struct t41_msca_mask_state st;
	struct sink sk;
	unsigned char r[24];
	int n;

	memset(&st, 0, sizeof(st));
	mask_req(r, 1, 2, 1, 100, 200, 64, 32, 16, 128, 129);
	assert(t41_msca_mask_set(&st, r) == 0);
	assert(st.dirty == 1U << 6);
	assert(st.block[6].en == 1 && st.block[6].left == 200 &&
	       st.block[6].top == 100 && st.block[6].width == 64 &&
	       st.block[6].height == 32 && st.block[6].color == 0x108081U);

	/* out of range (stock would write past its table) */
	mask_req(r, 3, 0, 1, 0, 0, 1, 1, 0, 0, 0);
	assert(t41_msca_mask_set(&st, r) == -22);
	mask_req(r, 0, 4, 1, 0, 0, 1, 1, 0, 0, 0);
	assert(t41_msca_mask_set(&st, r) == -22);
	assert(st.dirty == 1U << 6);

	/* get: en always 0, window + yuv of an enabled block */
	memset(r, 0xee, sizeof(r));
	r[0] = 1; r[1] = 2;
	assert(t41_msca_mask_get(&st, r) == 0);
	assert(r[2] == 0);
	assert(t41_msca_rd16(r + 4) == 100 && t41_msca_rd16(r + 6) == 200);
	assert(t41_msca_rd16(r + 8) == 64 && t41_msca_rd16(r + 10) == 32);
	assert(r[19] == 16 && r[20] == 128 && r[21] == 129);
	assert(r[12] == 0xee && r[16] == 0xee);	/* type, rgb: libimp's */
	/* a disabled block reads zeros */
	memset(r, 0xee, sizeof(r));
	r[0] = 0; r[1] = 0;
	assert(t41_msca_mask_get(&st, r) == 0);
	assert(t41_msca_rds32(r + 4) == 0 && t41_msca_rds32(r + 8) == 0);
	assert(r[19] == 0 && r[20] == 0 && r[21] == 0);

	/* emit for output 0 only: nothing dirty there, only the 0xf0014 word */
	memset(&sk, 0, sizeof(sk));
	sk.fail_at = -1;
	n = t41_msca_mask_emit(&st, 1U, sink_write, &sk);
	assert(n == 1 && sk.n == 1);
	assert(sk.p[0].reg == 0xf0014U && sk.p[0].value == 0);
	assert(st.dirty == 1U << 6);

	/* emit output 1: the stock pair order of tisp_msca_set_mask */
	memset(&sk, 0, sizeof(sk));
	sk.fail_at = -1;
	n = t41_msca_mask_emit(&st, 2U, sink_write, &sk);
	assert(n == 4 && sk.n == 4);
	assert(sk.p[0].reg == 0xf0238U + 2 * 12 && sk.p[0].value == (200U << 16 | 100U));
	assert(sk.p[1].reg == 0xf0240U + 2 * 12 && sk.p[1].value == 0x108081U);
	assert(sk.p[2].reg == 0xf023cU + 2 * 12 && sk.p[2].value == (64U << 16 | 32U));
	assert(sk.p[3].reg == 0xf0014U && sk.p[3].value == 2U);
	assert(st.dirty == 0 && st.global_en == 2U);

	/* disable: window/colour kept, size 0 written, global bit sticky */
	mask_req(r, 1, 2, 0, 1, 1, 1, 1, 1, 1, 1);
	assert(t41_msca_mask_set(&st, r) == 0);
	assert(st.block[6].en == 0 && st.block[6].left == 200 &&
	       st.block[6].color == 0x108081U);
	memset(&sk, 0, sizeof(sk));
	sk.fail_at = -1;
	n = t41_msca_mask_emit(&st, 7U, sink_write, &sk);
	assert(n == 6);	/* 0xf0014 for ch0, block + 0xf0014 for ch1, 0xf0014 for ch2 */
	assert(sk.p[0].reg == 0xf0014U);
	assert(sk.p[1].reg == 0xf0250U && sk.p[1].value == (200U << 16 | 100U));
	assert(sk.p[3].reg == 0xf0254U && sk.p[3].value == 0);
	assert(sk.p[4].reg == 0xf0014U && sk.p[4].value == 2U);
	assert(sk.p[5].reg == 0xf0014U && sk.p[5].value == 2U);

	/* a failing write keeps the blocks dirty */
	mask_req(r, 2, 3, 1, 8, 8, 8, 8, 0, 0, 0);
	assert(t41_msca_mask_set(&st, r) == 0);
	memset(&sk, 0, sizeof(sk));
	sk.fail_at = 1;
	assert(t41_msca_mask_emit(&st, 4U, sink_write, &sk) == -5);
	assert(st.dirty == 1U << 11);
	memset(&sk, 0, sizeof(sk));
	sk.fail_at = -1;
	assert(t41_msca_mask_emit(&st, 4U, sink_write, &sk) == 4);
	assert(sk.p[0].reg == 0xf0338U + 3 * 12);
	assert(sk.p[3].value == 6U && st.global_en == 6U);

	/* output start: enabled blocks of the starting output only */
	t41_msca_mask_mark_enabled(&st, 4U);
	assert(st.dirty == 1U << 11);
	st.dirty = 0;
	t41_msca_mask_mark_enabled(&st, 2U);	/* block 6 is disabled */
	assert(st.dirty == 0);
}

static void lv_req(unsigned char *r, unsigned int ch, int mode,
		   unsigned int level)
{
	memset(r, 0, 12);
	r[0] = (unsigned char)ch;
	put32(r + 4, mode);
	r[8] = (unsigned char)level;
}

static void test_scaler_lv(void)
{
	unsigned char params[0xe8], r[12];
	struct t41_msca_scaler_lv lv[3];

	memset(params, 0, sizeof(params));
	memset(lv, 0, sizeof(lv));
	params[0xdf] = params[0xe0] = 1;
	params[0xe1] = params[0xe2] = 1;

	/* FITTING_CURVE level 64 on output 1 */
	lv_req(r, 1, 0, 64);
	assert(t41_msca_scaler_lv_set(params, lv, r) == 1);
	assert(params[0xe1] == 0 && params[0xe2] == 0);
	assert(params[0xdf] == 1 && params[0xe0] == 1);
	assert(lv[1].valid && lv[1].level == 64);
	assert(t41_msca_scale_mode(params) == 0x11U);
	/* 0xf0708 + ch * 8: low 16 bits = level | level << 8, bits 16..24 and
	 * 28..31 kept, 25..27 cleared (stock) */
	assert(t41_msca_scaler_lv_curve(0xfeff1234U, &lv[1]) == 0xf0ff4040U);
	assert(t41_msca_scaler_lv_curve(0xfeff1234U, &lv[0]) == 0xfeff1234U);
	assert(t41_msca_scaler_lv_curve(0x1234U, NULL) == 0x1234U);

	/* level 128 is the maximum */
	lv_req(r, 1, 0, 128);
	assert(t41_msca_scaler_lv_set(params, lv, r) == 1 && lv[1].level == 128);
	lv_req(r, 1, 0, 129);
	assert(t41_msca_scaler_lv_set(params, lv, r) == -22 && lv[1].level == 128);
	/* chx out of range */
	lv_req(r, 3, 0, 1);
	assert(t41_msca_scaler_lv_set(params, lv, r) == -22);

	/* FIXED_WEIGHT: both bytes 1, stored level dropped */
	lv_req(r, 1, 1, 200);
	assert(t41_msca_scaler_lv_set(params, lv, r) == 1);
	assert(params[0xe1] == 1 && params[0xe2] == 1 && !lv[1].valid);
	assert(t41_msca_scale_mode(params) == 0x33U);

	/* other modes: no change, not an error (stock) */
	lv_req(r, 2, 2, 0);
	assert(t41_msca_scaler_lv_set(params, lv, r) == 2);
	assert(params[0xe3] == 0 && params[0xe4] == 0 && !lv[2].valid);
}

static void test_plan(void)
{
	/* not streaming: store only */
	assert(t41_msca_live_plan(0, 2, 1, 640, 360, 768, 432) == T41_MSCA_IDLE);
	/* streaming, default flashed modes: never live */
	assert(t41_msca_live_plan(1, 0, 0, 640, 360, 768, 432) == T41_MSCA_DEFER);
	assert(t41_msca_live_plan(1, 1, 1, 640, 360, 768, 432) == T41_MSCA_DEFER);
	assert(t41_msca_live_plan(1, 2, 0, 640, 360, 768, 432) == T41_MSCA_DEFER);
	/* latch-while-off sequence and inside the envelope */
	assert(t41_msca_live_plan(1, 2, 1, 640, 360, 768, 432) == T41_MSCA_LIVE);
	assert(t41_msca_live_plan(1, 2, 1, 768, 432, 768, 432) == T41_MSCA_LIVE);
	/* the sizes that hung the T41 started mid-stream */
	assert(t41_msca_live_plan(1, 2, 1, 960, 540, 768, 432) == T41_MSCA_DEFER);
	assert(t41_msca_live_plan(1, 2, 1, 1280, 720, 768, 432) == T41_MSCA_DEFER);
	assert(t41_msca_live_plan(1, 2, 1, 1920, 1080, 768, 432) == T41_MSCA_DEFER);
	assert(t41_msca_live_plan(1, 2, 1, 769, 432, 768, 432) == T41_MSCA_DEFER);
	assert(t41_msca_live_plan(1, 2, 1, 768, 433, 768, 432) == T41_MSCA_DEFER);
	/* unknown output size never goes live */
	assert(t41_msca_live_plan(1, 2, 1, 0, 0, 768, 432) == T41_MSCA_DEFER);
}

int main(void)
{
	test_zoom();
	test_mask();
	test_scaler_lv();
	test_plan();
	printf("tx_isp_t41_msca_ctl_test: ok\n");
	return 0;
}
