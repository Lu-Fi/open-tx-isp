/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AWB accessors on the vendor's awbInfo[ch] block (binary oracle block r11B):
 * tisp_awb_set_lum_th_freq, tisp_awb_set_regional_threshold, tisp_awb_show_para_update,
 * tisp_awb_get_zone, tisp_awb_get_global_statis, tisp_awb_api_get/set_ct_trend_offset,
 * tisp_g/s_awb_ct_trend_offset, tisp_g_awb_statis_attr, tisp_g_awb_global_statis_attr.
 *
 * awbInfo[ch] points to { par, st, 2 words, show buffer }; par is the AWB parameter block, st the run
 * state, whose fields used here sit at st + 0x10000 + x (the vendor adds 0x10000 to the pointer).
 */
#ifndef TX_ISP_T33_R11B_H
#define TX_ISP_T33_R11B_H

#include "tx_isp_t33_blk.h"
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

struct t33_awb_ctx {
	u8 *par;
	u8 *st;
	u32 rsvd[2];
	u8 *show;
};

typedef u32 (*t33_awbi_intp_fn)(u32 i, u32 f, const u8 *t);
typedef int (*t33_awbi_regw_fn)(u32 ch, u32 blk, u32 slot, u32 val);
typedef void (*t33_awbi_trig_fn)(u32 v, u32 ch);

static inline u32 t33_awbi_h(const u8 *p, u32 o)
{
	return p[o] | (u32)p[o + 1] << 8;
}

static inline void t33_awbi_put16(u8 *p, u32 o, u32 v)
{
	p[o] = (u8)v;
	p[o + 1] = (u8)(v >> 8);
}

static inline void t33_awbi_put32(u8 *p, u32 o, u32 v)
{
	p[o] = (u8)v;
	p[o + 1] = (u8)(v >> 8);
	p[o + 2] = (u8)(v >> 16);
	p[o + 3] = (u8)(v >> 24);
}

/* tisp_awb_set_lum_th_freq */
static inline int t33_awb_set_lum_th_freq(struct t33_awb_ctx *const *info, u32 ch, t33_awbi_intp_fn intp,
					  t33_awbi_regw_fn wr, t33_awbi_trig_fn trig)
{
	static const u16 dst[11] = { 3268, 3270, 3272, 3282, 3284, 3286, 3288, 3294, 3296, 3298, 3300 };
	u8 *par = info[ch]->par;
	const u8 *st = info[ch]->st;
	u32 gain = t33_le32(st + 0x10000 + 1076), i = gain >> 16, f = gain & 0xffff, k;

	for (k = 0; k < 11; k++)
		t33_awbi_put16(par, dst[k], intp(i, f, par + 4764 + 22 * k));
	wr(ch, 29, 10, t33_awbi_h(par, 3268) | t33_awbi_h(par, 3270) << 8 | t33_awbi_h(par, 3272) << 16 |
	   t33_awbi_h(par, 3274) << 24);
	wr(ch, 29, 40, (t33_awbi_h(par, 3280) & 3) << 16 | (t33_awbi_h(par, 3278) & 1));
	wr(ch, 29, 41, (t33_awbi_h(par, 3282) & 0xf) | (t33_awbi_h(par, 3284) & 0xf) << 8 |
	   (t33_awbi_h(par, 3286) & 0xf) << 16 | (t33_awbi_h(par, 3288) & 0xf) << 24);
	wr(ch, 29, 43, (t33_awbi_h(par, 3294) & 0xf) | (t33_awbi_h(par, 3296) & 0xf) << 8 |
	   (t33_awbi_h(par, 3298) & 0xf) << 16 | (t33_awbi_h(par, 3300) & 0xf) << 24);
	trig(1, ch);
	return 0;
}

/* tisp_awb_set_regional_threshold */
static inline int t33_awb_set_regional_threshold(struct t33_awb_ctx *const *info, u32 ch, t33_awbi_intp_fn intp,
						 t33_awbi_regw_fn wr, t33_awbi_trig_fn trig)
{
	static const u16 st_rows[6][2] = {
		{ 37992, 5242 }, { 37994, 5264 }, { 37996, 5286 }, { 38012, 5462 }, { 38014, 5484 }, { 38016, 5506 },
	};
	static const u16 st_cp[8] = { 37998, 38000, 38002, 38004, 38018, 38020, 38022, 38024 };
	u8 *par = info[ch]->par, *st = info[ch]->st;
	u32 gain = t33_le32(st + 0x10000 + 1076), i = gain >> 16, f = gain & 0xffff, k;
	u32 w0, w1, w2, w3;

	for (k = 0; k < 8; k++)
		t33_awbi_put16(par, 3252 + 2 * k, intp(i, f, par + 4588 + 22 * k));
	for (k = 0; k < 6; k++)
		t33_awbi_put16(st, st_rows[k][0], intp(i, f, par + st_rows[k][1]));
	for (k = 0; k < 8; k++)
		t33_awbi_put16(st, st_cp[k], t33_awbi_h(par, 3500 + 2 * k));
	w0 = t33_le32(par + 3252);
	w1 = t33_le32(par + 3256);
	w2 = t33_le32(par + 3260);
	w3 = t33_le32(par + 3264);
	if (!st[0x10000 + 1081]) {
		wr(ch, 29, 6, w0);
		wr(ch, 29, 7, w1);
		wr(ch, 29, 8, w2);
		wr(ch, 29, 9, w3);
		trig(1, ch);
	}
	return 0;
}

/* tisp_awb_show_para_update: six 20-byte rows of par into the show buffer */
static inline void t33_awb_show_para_update(struct t33_awb_ctx *const *info, u32 ch)
{
	const u8 *par = info[ch]->par;
	u8 *dst = info[ch]->show;

	memcpy(dst, par + 5006, 20);
	memcpy(dst + 20, par + 5026, 20);
	memcpy(dst + 40, par + 5046, 20);
	memcpy(dst + 60, par + 5066, 20);
	memcpy(dst + 80, par + 5086, 20);
	memcpy(dst + 100, par + 5106, 20);
}

/* tisp_awb_get_zone: three 4 KiB zone tables of the state */
static inline void t33_awb_get_zone(struct t33_awb_ctx *const *info, u32 ch, u8 *out)
{
	const u8 *st = info[ch]->st;

	memcpy(out, st + 0x10000 + 1112, 4096);
	memcpy(out + 4096, st + 0x10000 + 5208, 4096);
	memcpy(out + 8192, st + 0x10000 + 9304, 4096);
}

/* tisp_awb_get_global_statis */
static inline int t33_awb_get_global_statis(struct t33_awb_ctx *const *info, u32 ch, u8 *out)
{
	const u8 *st = info[ch]->st;

	t33_awbi_put32(out, 0, t33_awbi_h(st, 0x10000 + 1104));
	t33_awbi_put32(out, 4, t33_awbi_h(st, 0x10000 + 1106));
	t33_awbi_put32(out, 8, t33_awbi_h(st, 0x10000 + 1092));
	t33_awbi_put32(out, 12, t33_awbi_h(st, 0x10000 + 1094));
	return 0;
}

/* tisp_awb_api_get_ct_trend_offset (the vendor returns the last word it loaded) */
static inline u32 t33_awb_api_get_ct_trend_offset(struct t33_awb_ctx *const *info, u32 ch, u8 *out)
{
	const u8 *par = info[ch & 0xff]->par;
	u32 k, v;

	for (k = 0; k < 6; k++)
		t33_awbi_put32(out, 4 + 4 * k, t33_le32(par + 3064 + 4 * k));
	t33_awbi_put16(out, 0, t33_le32(par + 3088));
	v = t33_le32(par + 3092);
	t33_awbi_put16(out, 2, v);
	return v;
}

/* tisp_awb_api_set_ct_trend_offset */
static inline void t33_awb_api_set_ct_trend_offset(struct t33_awb_ctx *const *info, u32 ch, const u8 *in)
{
	u32 idx = ch & 0xff, k;
	u8 *par = info[idx]->par;

	for (k = 0; k < 6; k++)
		t33_awbi_put32(par, 3064 + 4 * k, t33_le32(in + 4 + 4 * k));
	t33_awbi_put32(par, 3088, t33_awbi_h(in, 0));
	t33_awbi_put32(par, 3092, t33_awbi_h(in, 2));
	info[idx]->st[0x10000 + 1080] = 1;
}

/* tisp_g_awb_ct_trend_offset */
static inline int t33_g_awb_ct_trend_offset(struct t33_awb_ctx *const *info, u32 ch, u8 *out)
{
	t33_awb_api_get_ct_trend_offset(info, ch & 0xff, out);
	return 0;
}

/* tisp_s_awb_ct_trend_offset */
static inline int t33_s_awb_ct_trend_offset(struct t33_awb_ctx *const *info, u32 ch, const u8 *in)
{
	t33_awb_api_set_ct_trend_offset(info, ch & 0xff, in);
	return 0;
}

/* tisp_g_awb_statis_attr */
static inline int t33_g_awb_statis_attr(struct t33_awb_ctx *const *info, u32 ch, u8 *out)
{
	t33_awb_get_zone(info, ch, out);
	return 0;
}

/* tisp_g_awb_global_statis_attr */
static inline int t33_g_awb_global_statis_attr(struct t33_awb_ctx *const *info, u32 ch, u8 *out)
{
	t33_awb_get_global_statis(info, ch, out);
	return 0;
}

#endif /* TX_ISP_T33_R11B_H */
