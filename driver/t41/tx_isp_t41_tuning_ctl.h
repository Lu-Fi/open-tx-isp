/* SPDX-License-Identifier: MIT */
/*
 * T41 tuning-control payload helpers (isp-m0 0xc0105435 envelope), kept
 * free of kernel state so the host tests can check them against the stock
 * tx_isp_core_ops_g_ctrl/s_ctrl layouts (libt41-firmware 1.2.6).
 */
#ifndef TX_ISP_T41_TUNING_CTL_H
#define TX_ISP_T41_TUNING_CTL_H
#include "tx_isp_t41_tmo.h"
#include "tx_isp_t41_ae.h"

/*
 * IMPISPSENSORAttr { hts, vts, fps, width, height } (stock g_ctrl 0x08000033):
 * hts/vts are the u16 total_width/total_height of the sensor attribute
 * (attr+180/+182), fps is the packed num<<16|den of the core video slot
 * (video+68), width/height the sensor output size (video+60/+64).
 */
#define T41_SENSOR_ATTR_WORDS 5U

static inline int t41_sensor_attr_fill(const unsigned char *video,
		const unsigned char *attr, unsigned int *out)
{
	if (!video || !attr || !out)
		return -1;
	out[0] = t41_tmo_le16(attr + 180);
	out[1] = t41_tmo_le16(attr + 182);
	out[2] = t41_tmo_le32(video + 68);
	out[3] = t41_tmo_le32(video + 60);
	out[4] = t41_tmo_le32(video + 64);
	return 0;
}

/*
 * Packed sensor frame rate num<<16|den (stock s_ctrl 0x08000070 passes it
 * inline).  0: valid; -1: zero numerator/denominator; -2: faster than the
 * reference mode rate (the sensor mode cannot shorten its frame below the
 * mode's VTS, so such a request can only be refused by the sensor).
 * ref == 0 disables the upper bound.  num, den <= 0xffff, so the cross
 * products fit 32 bits and need no 64-bit helper.
 */
static inline int t41_sensor_fps_check(unsigned int fps, unsigned int ref)
{
	unsigned int num = fps >> 16, den = fps & 0xffffU;
	unsigned int ref_num = ref >> 16, ref_den = ref & 0xffffU;

	if (!num || !den)
		return -1;
	if (ref_num && ref_den && num * ref_den > ref_num * den)
		return -2;
	return 0;
}

/*
 * IMPISPAEWeightAttr { u32 roi_enable; u32 weight_enable; u8 ae_roi[225];
 * u8 ae_weight[225]; } - stock copies 460 bytes.  api_ae_set_weight: an
 * enabled ROI goes to AE params+0x4ee and 8 - roi to state+0x2528; an
 * enabled weight table goes to params+0x82e.  api_ae_get_weight returns the
 * stored enables and both tables from the params.
 *
 * The open zone meter (t41_ae_weight_mean) needs at least one non-zero
 * weight, and a ROI with at least one zone below and one above zero (it
 * divides by the foreground and the background area).  Values are the
 * documented 0..8.  Such input is refused instead of stalling the meter.
 */
#define T41_AE_WEIGHT_ATTR_BYTES 460U
#define T41_AE_WEIGHT_ROI 8U
#define T41_AE_WEIGHT_TABLE 233U
#define T41_AE_PARAM_ROI 0x4eeU
#define T41_AE_PARAM_WEIGHT 0x82eU
#define T41_AE_STATE_ROI_INV 0x2528U
#define T41_AE_WEIGHT_ZONES 225U

static inline int t41_ae_weight_table_ok(const unsigned char *t, int roi)
{
	unsigned int i, low = 0, high = 0;

	for (i = 0; i < T41_AE_WEIGHT_ZONES; ++i) {
		if (t[i] > 8)
			return 0;
		low |= t[i] < 8;
		high |= t[i] > 0;
	}
	return roi ? low && high : high;
}

static inline int t41_ae_weight_set(unsigned char *params,
		unsigned int param_bytes, unsigned char *state,
		unsigned int state_bytes, unsigned char *enables,
		const unsigned char *attr)
{
	unsigned int roi, weight, i;

	if (!params || !state || !enables || !attr ||
	    param_bytes < T41_AE_PARAM_WEIGHT + T41_AE_WEIGHT_ZONES ||
	    state_bytes < T41_AE_STATE_ROI_INV + T41_AE_WEIGHT_ZONES)
		return -1;
	roi = t41_tmo_le32(attr);
	weight = t41_tmo_le32(attr + 4);
	if (roi > 1 || weight > 1)
		return -1;
	if (roi && !t41_ae_weight_table_ok(attr + T41_AE_WEIGHT_ROI, 1))
		return -1;
	if (weight && !t41_ae_weight_table_ok(attr + T41_AE_WEIGHT_TABLE, 0))
		return -1;
	enables[0] = roi;
	enables[1] = weight;
	if (roi) {
		for (i = 0; i < T41_AE_WEIGHT_ZONES; ++i) {
			params[T41_AE_PARAM_ROI + i] = attr[T41_AE_WEIGHT_ROI + i];
			state[T41_AE_STATE_ROI_INV + i] =
				8 - attr[T41_AE_WEIGHT_ROI + i];
		}
	}
	if (weight)
		for (i = 0; i < T41_AE_WEIGHT_ZONES; ++i)
			params[T41_AE_PARAM_WEIGHT + i] =
				attr[T41_AE_WEIGHT_TABLE + i];
	return 0;
}

static inline int t41_ae_weight_get(const unsigned char *params,
		unsigned int param_bytes, const unsigned char *enables,
		unsigned char *attr)
{
	unsigned int i;

	if (!params || !enables || !attr ||
	    param_bytes < T41_AE_PARAM_WEIGHT + T41_AE_WEIGHT_ZONES)
		return -1;
	for (i = 0; i < T41_AE_WEIGHT_ATTR_BYTES; ++i)
		attr[i] = 0;
	attr[0] = enables[0];
	attr[4] = enables[1];
	for (i = 0; i < T41_AE_WEIGHT_ZONES; ++i) {
		attr[T41_AE_WEIGHT_ROI + i] = params[T41_AE_PARAM_ROI + i];
		attr[T41_AE_WEIGHT_TABLE + i] = params[T41_AE_PARAM_WEIGHT + i];
	}
	return 0;
}

/*
 * IMPISPCoefftWb { u16 r, g, b } (stock tisp_bcsh_api_set_offset_rgb):
 * kept at bcsh_info+304 and written to the BCSH params +0x118..0x11c, the
 * RGB bias (value - 1024) of the BCSH matrix.  The hardware field is 11
 * bits, so larger values would wrap.
 */
#define T41_BCSH_RGB_OFFSET 0x118U

static inline int t41_bcsh_offset_rgb_ok(const unsigned short *rgb)
{
	return rgb && rgb[0] <= 2047 && rgb[1] <= 2047 && rgb[2] <= 2047;
}

/*
 * IMPISPAeConvergeStep { u16 ae_step[4]; } (stock 0x08000028,
 * tisp_ae_set/get_converge_step): steps 0/1 are u32 in the AE params at
 * +0x664/+0x668, steps 2/3 u16 at +0x75c/+0x75e.  Only the recovered stock
 * AE (t41_safe_ae_controller=0) reads them; the bounded open AE keeps its
 * own step.  A zero step is refused (stock stores it unchecked).
 */
#define T41_AE_CONVERGE_BYTES 8U
#define T41_AE_PARAM_CONVERGE_A 0x664U
#define T41_AE_PARAM_CONVERGE_B 0x75cU

static inline int t41_ae_converge_set(unsigned char *params,
		unsigned int param_bytes, const unsigned short *step)
{
	unsigned int i;

	if (!params || !step || param_bytes < T41_AE_PARAM_CONVERGE_B + 4)
		return -1;
	for (i = 0; i < 4; ++i)
		if (!step[i])
			return -1;
	for (i = 0; i < 2; ++i) {
		unsigned char *w = params + T41_AE_PARAM_CONVERGE_A + i * 4;
		unsigned char *h = params + T41_AE_PARAM_CONVERGE_B + i * 2;

		w[0] = step[i]; w[1] = step[i] >> 8; w[2] = 0; w[3] = 0;
		h[0] = step[2 + i]; h[1] = step[2 + i] >> 8;
	}
	return 0;
}

static inline int t41_ae_converge_get(const unsigned char *params,
		unsigned int param_bytes, unsigned short *step)
{
	if (!params || !step || param_bytes < T41_AE_PARAM_CONVERGE_B + 4)
		return -1;
	step[0] = (unsigned short)t41_tmo_le32(params + T41_AE_PARAM_CONVERGE_A);
	step[1] = (unsigned short)t41_tmo_le32(params + T41_AE_PARAM_CONVERGE_A + 4);
	step[2] = t41_tmo_le16(params + T41_AE_PARAM_CONVERGE_B);
	step[3] = t41_tmo_le16(params + T41_AE_PARAM_CONVERGE_B + 2);
	return 0;
}

/*
 * IMPISPAeEvList / IMPISPAeAtList { u32 enable; u64 v[15]; } (128 bytes,
 * stock 0x08000038/0x08000039, tisp_ae_get_ev_list/at_list): the EV knots
 * (u64 at AE params +0x5d0) and the target luma per knot (u16 at +0x76e),
 * the tables the open AE target interpolation reads.  Stock leaves the
 * enable word as uninitialised kernel stack; here it is 1 (table in use).
 */
#define T41_AE_LIST_BYTES 128U
#define T41_AE_PARAM_EV_LIST 0x5d0U
#define T41_AE_PARAM_AT_LIST 0x76eU

static inline int t41_ae_list_get(const unsigned char *params,
		unsigned int param_bytes, int at_list, unsigned char *out)
{
	unsigned int i, j;

	if (!params || !out || param_bytes < T41_AE_PARAM_AT_LIST + 30 ||
	    param_bytes < T41_AE_PARAM_EV_LIST + 120)
		return -1;
	for (i = 0; i < T41_AE_LIST_BYTES; ++i)
		out[i] = 0;
	out[0] = 1;
	for (i = 0; i < 15; ++i) {
		unsigned char *v = out + 8 + i * 8;

		if (at_list) {
			v[0] = params[T41_AE_PARAM_AT_LIST + i * 2];
			v[1] = params[T41_AE_PARAM_AT_LIST + i * 2 + 1];
		} else {
			for (j = 0; j < 8; ++j)
				v[j] = params[T41_AE_PARAM_EV_LIST + i * 8 + j];
		}
	}
	return 0;
}

/*
 * IMPISPTmoCurve { u8 transfer_flag; u8 id; u16 value[201]; } (404 bytes,
 * stock 0x08000063, tisp_tmo_api_get/set_curve): curve id 0..9 is one of
 * the ten EV curves in the TMO params (the offsets t41_tmo_curve reads);
 * the per-frame TMO map interpolates them, so a value above 8191 (which
 * t41_tmo_curve rejects for the whole table) is refused here.  Stock
 * ignores an id >= 10 silently and reads the GET id from uninitialised
 * kernel stack; here the caller's id is used and a bad id is -1.
 */
#define T41_TMO_CURVE_BYTES 404U

static inline int t41_tmo_curve_offset(unsigned int id)
{
	static const unsigned short offsets[10] = {
		4950, 5416, 1416, 1818, 2220, 2622, 3024, 3426, 3828, 4230,
	};

	return id < 10 ? (int)offsets[id] : -1;
}

static inline int t41_tmo_curve_set(unsigned char *params,
		unsigned int param_bytes, const unsigned char *req)
{
	unsigned int i;
	int off;

	if (!params || !req || param_bytes != T41_TMO_PARAM_BYTES)
		return -1;
	off = t41_tmo_curve_offset(req[1]);
	if (off < 0)
		return -1;
	for (i = 0; i < T41_TMO_CURVE_ENTRIES; ++i)
		if (t41_tmo_le16(req + 2 + i * 2) > 8191)
			return -1;
	for (i = 0; i < T41_TMO_CURVE_ENTRIES * 2; ++i)
		params[off + i] = req[2 + i];
	return 0;
}

static inline int t41_tmo_curve_get(const unsigned char *params,
		unsigned int param_bytes, unsigned char *req)
{
	unsigned int i;
	int off;

	if (!params || !req || param_bytes != T41_TMO_PARAM_BYTES)
		return -1;
	off = t41_tmo_curve_offset(req[1]);
	if (off < 0)
		return -1;
	for (i = 0; i < T41_TMO_CURVE_ENTRIES * 2; ++i)
		req[2 + i] = params[off + i];
	return 0;
}

/*
 * IMPISPStatisConfig (72 bytes, stock 0x08000000, tisp_s/g_statis_config_attr
 * on tisp_tattr+568): AE {en; start_h, start_v; node_h, node_v; domain;
 * thresh[4]} at 0, AWB {en; location; mode} at 24, AF {en; location;
 * shift, delta, theta, hilight, alpha, belta} at 44.  Stock stores an
 * enabled part whole and only clears the enable of a disabled part.
 * Returns -1 for an invalid request, else a bit mask of the enabled parts
 * (1 AE, 2 AWB, 4 AF).  t41_statis_awb_location() builds the 10-byte
 * T41_AWB_LOCATION request {start_h, start_v, node_h, node_v}.
 */
#define T41_STATIS_CONFIG_BYTES 72U
#define T41_STATIS_AE 0U
#define T41_STATIS_AWB 24U
#define T41_STATIS_AF 44U

static inline int t41_statis_config_check(const unsigned char *req)
{
	unsigned int en_ae, en_awb, en_af;

	if (!req)
		return -1;
	en_ae = t41_tmo_le32(req + T41_STATIS_AE);
	en_awb = t41_tmo_le32(req + T41_STATIS_AWB);
	en_af = t41_tmo_le32(req + T41_STATIS_AF);
	if (en_ae > 1 || en_awb > 1 || en_af > 1)
		return -1;
	if (en_ae && t41_tmo_le32(req + T41_STATIS_AE + 16) > 1)
		return -1;
	if (en_awb && t41_tmo_le32(req + T41_STATIS_AWB + 16) > 1)
		return -1;
	return (int)(en_ae | en_awb << 1 | en_af << 2);
}

static inline void t41_statis_awb_location(const unsigned char *req,
		unsigned char *loc)
{
	unsigned int i;

	for (i = 0; i < 10; ++i)
		loc[i] = req[T41_STATIS_AWB + 4 + i];
}

static inline void t41_statis_config_store(unsigned char *rec,
		const unsigned char *req)
{
	static const unsigned char part[3][2] = {
		{ T41_STATIS_AE, 24 }, { T41_STATIS_AWB, 20 }, { T41_STATIS_AF, 28 },
	};
	unsigned int p, i;

	for (p = 0; p < 3; ++p) {
		const unsigned char *src = req + part[p][0];
		unsigned char *dst = rec + part[p][0];

		if (t41_tmo_le32(src) == 1) {
			for (i = 0; i < part[p][1]; ++i)
				dst[i] = src[i];
		} else {
			dst[0] = dst[1] = dst[2] = dst[3] = 0;
		}
	}
}

/*
 * AE tuning API (issue 90), as the stock T41 driver (libt41-firmware 1.2.5
 * and the module the open driver was recovered from: tisp_g_ae_statis_attr,
 * api_ae_set_hist_bin, tisp_ae_sepc_area / tisp_ae_api_get_faceae,
 * tisp_ae_api_set/get_exp_list).  Plain data only; the driver supplies the
 * per-channel blocks: tisp_ae_hist_last (1052 bytes per channel, written by
 * the vendor AE's api_ae_long_process every frame), the AE params/state of
 * ae_info[ch] and the AE cache of ae_cache[ch].
 */
#define T41_AE_HIST_LAST_BYTES 1052U
#define T41_AE_HIST_LAST_HIST5 1024U	/* five u32, 65535 = whole frame */
#define T41_AE_HIST_LAST_THRESH 1044U	/* u8[4] histThresh */
#define T41_AE_STATE_YZONE 7648U	/* u32[225] zone Y (tisp_ae_weight_mean) */
#define T41_AE_STATE_WIDTH 8548U	/* u16 frame width / height */
#define T41_AE_STATE_HEIGHT 8550U
#define T41_AE_STATS_REPLY_BYTES 1934U	/* IMPISPAEStatisInfo */

/* IMPISPAEStatisInfo { u16 hist5[5]; u32 hist256[256]; u32 zone[15][15]; }:
 * stock copies the 256 bins as they are, the low half of each 5-bin word
 * and the 900 zone bytes of the AE state. */
static inline int t41_ae_stats_reply(unsigned char *out,
		const unsigned char *hist_last, const unsigned char *state,
		unsigned int state_bytes)
{
	unsigned int i;

	if (!out || !hist_last || !state ||
	    state_bytes < T41_AE_STATE_YZONE + 900U)
		return -1;
	for (i = 0; i < 5; ++i) {
		out[2 * i] = hist_last[T41_AE_HIST_LAST_HIST5 + 4 * i];
		out[2 * i + 1] = hist_last[T41_AE_HIST_LAST_HIST5 + 4 * i + 1];
	}
	for (i = 0; i < 1024; ++i)
		out[10 + i] = hist_last[i];
	for (i = 0; i < 900; ++i)
		out[1034 + i] = state[T41_AE_STATE_YZONE + i];
	return 0;
}

/*
 * IMPISPFaceAttr { enable; left; top; right; bottom; target; } (24 bytes,
 * stock 0x08000035).  Stock tisp_ae_sepc_area: a changed enable is stored
 * at cache+1516 with the parameter-update flag cache+1281; the rectangle is
 * clamped to [20, width - 20] x [10, height - 10] and handed to
 * tisp_ae_roi_point (hardware ROI window, ROI area cache+1284 and the face
 * flag cache+1293 from the enable); the target is limited to 10..200 and
 * stored (u8) at cache+1292; the caller's rectangle and the clamped target
 * go to cache+1496..1512.  Beyond stock: an enable other than 0/1, or an
 * enabled face whose clamped rectangle is empty (tisp_ae_roi_point refuses
 * it, stock then still switched the face AE on with the old window), is
 * refused before anything changes.
 */
#define T41_FACE_AE_BYTES 24U
#define T41_AE_CACHE_BYTES 0x688U
#define T41_AE_CACHE_TRIG 1281U
#define T41_AE_CACHE_FACE_TARGET 1292U
#define T41_AE_CACHE_FACE_ON 1293U
#define T41_AE_CACHE_FACE_RECT 1496U
#define T41_AE_CACHE_FACE_EN 1516U

struct t41_face_ae {
	unsigned int enable, left, top, right, bottom, target;
	unsigned int roi[4];	/* clamped left, top, right, bottom */
	int roi_ok;
};

static inline int t41_face_ae_plan(const unsigned char *req,
		unsigned int width, unsigned int height, struct t41_face_ae *f)
{
	unsigned int wmax = width > 20U ? width - 20U : 0U;
	unsigned int hmax = height > 10U ? height - 10U : 0U;

	if (!req || !f)
		return -1;
	f->enable = t41_tmo_le32(req);
	f->left = t41_tmo_le32(req + 4);
	f->top = t41_tmo_le32(req + 8);
	f->right = t41_tmo_le32(req + 12);
	f->bottom = t41_tmo_le32(req + 16);
	f->target = t41_tmo_le32(req + 20);
	if (f->enable > 1U)
		return -1;
	if (f->target > 200U)
		f->target = 200U;
	else if (f->target < 10U)
		f->target = 10U;
	f->roi[0] = f->left < 20U ? 20U : f->left;
	f->roi[1] = f->top < 10U ? 10U : f->top;
	f->roi[2] = f->right < wmax ? f->right : wmax;
	f->roi[3] = f->bottom < hmax ? f->bottom : hmax;
	f->roi_ok = width && height && f->roi[2] > f->roi[0] &&
		f->roi[3] > f->roi[1] && f->roi[2] <= 0xffffU &&
		f->roi[3] <= 0xffffU;
	if (f->enable && !f->roi_ok)
		return -1;
	return 0;
}

/* Before tisp_ae_roi_point (it derives the face flag from cache+1516). */
static inline void t41_face_ae_store_enable(unsigned char *cache,
		const struct t41_face_ae *f)
{
	if (t41_tmo_le32(cache + T41_AE_CACHE_FACE_EN) != f->enable) {
		t41_ae_put32(cache + T41_AE_CACHE_FACE_EN, f->enable);
		cache[T41_AE_CACHE_TRIG] = 1;
	}
}

static inline void t41_face_ae_store(unsigned char *cache,
		const struct t41_face_ae *f)
{
	cache[T41_AE_CACHE_FACE_TARGET] = (unsigned char)f->target;
	t41_ae_put32(cache + T41_AE_CACHE_FACE_RECT, f->left);
	t41_ae_put32(cache + T41_AE_CACHE_FACE_RECT + 4, f->top);
	t41_ae_put32(cache + T41_AE_CACHE_FACE_RECT + 8, f->right);
	t41_ae_put32(cache + T41_AE_CACHE_FACE_RECT + 12, f->bottom);
	t41_ae_put32(cache + T41_AE_CACHE_FACE_RECT + 16, f->target);
}

/* GET.  Stock tisp_ae_api_get_faceae: zeros for a disabled face, else
 * cache+1496..1512, whose first word the vendor AE's face-history ring
 * (cache+1376..1496) overwrites every frame, and it never fills the enable
 * word (kernel stack).  Here: the last accepted request (rectangle as the
 * caller gave it, clamped target) and the enable. */
static inline void t41_face_ae_report(unsigned char *out,
		const struct t41_face_ae *f)
{
	unsigned int i;

	for (i = 0; i < T41_FACE_AE_BYTES; ++i)
		out[i] = 0;
	t41_ae_put32(out, f->enable);
	if (!f->enable)
		return;
	t41_ae_put32(out + 4, f->left);
	t41_ae_put32(out + 8, f->top);
	t41_ae_put32(out + 12, f->right);
	t41_ae_put32(out + 16, f->bottom);
	t41_ae_put32(out + 20, f->target);
}

/*
 * IMPISPAeExpListAttr { mode; u32 elist[16][5]; } (324 bytes, stock
 * 0x08000027; columns reserve | integration time (us) | again | dgain |
 * reserve).  Stock tisp_ae_api_set_exp_list: state+7536 = 1, +7540 = 0,
 * params+1732 = mode (the vendor AE's tisp_ae_tune switches to
 * tisp_ae_ev_list_alloc_calc while it is 1); for mode 1 columns 0..3 of
 * the 16 rows go to params+580 + 16 * row and the parameter-update flag
 * cache+1281 is set.  Beyond stock: a mode other than 0/1 is refused.
 * GET: stock returns the mode, column 1 from the params and columns 2/3
 * from cache+680 + 16 * row (sensor limit words of the AE cache, not the
 * list); here columns 1..3 are the stored list.
 */
#define T41_AE_EXPLIST_BYTES 324U
#define T41_AE_PARAM_EXPLIST 580U
#define T41_AE_PARAM_EXPLIST_MODE 1732U
#define T41_AE_STATE_EXPLIST_A 7536U
#define T41_AE_STATE_EXPLIST_B 7540U

static inline int t41_ae_explist_set(unsigned char *params,
		unsigned int param_bytes, unsigned char *state,
		unsigned int state_bytes, unsigned char *cache,
		const unsigned char *req)
{
	unsigned int mode, i, k;

	if (!params || !state || !cache || !req ||
	    param_bytes < T41_AE_PARAM_EXPLIST_MODE + 4U ||
	    state_bytes < T41_AE_STATE_EXPLIST_B + 4U)
		return -1;
	mode = t41_tmo_le32(req);
	if (mode > 1U)
		return -1;
	t41_ae_put32(state + T41_AE_STATE_EXPLIST_A, 1);
	t41_ae_put32(state + T41_AE_STATE_EXPLIST_B, 0);
	t41_ae_put32(params + T41_AE_PARAM_EXPLIST_MODE, mode);
	if (mode != 1U)
		return 0;
	for (i = 0; i < 16; ++i)
		for (k = 0; k < 4; ++k)
			t41_ae_put32(params + T41_AE_PARAM_EXPLIST + 16 * i + 4 * k,
				     t41_tmo_le32(req + 4 + 20 * i + 4 * k));
	cache[T41_AE_CACHE_TRIG] = 1;
	return 0;
}

static inline int t41_ae_explist_get(const unsigned char *params,
		unsigned int param_bytes, unsigned char *out)
{
	unsigned int i, k;

	if (!params || !out || param_bytes < T41_AE_PARAM_EXPLIST_MODE + 4U)
		return -1;
	for (i = 0; i < T41_AE_EXPLIST_BYTES; ++i)
		out[i] = 0;
	t41_ae_put32(out, t41_tmo_le32(params + T41_AE_PARAM_EXPLIST_MODE));
	for (i = 0; i < 16; ++i)
		for (k = 1; k < 4; ++k)
			t41_ae_put32(out + 4 + 20 * i + 4 * k,
				     t41_tmo_le32(params + T41_AE_PARAM_EXPLIST +
						  16 * i + 4 * k));
	return 0;
}

#endif
