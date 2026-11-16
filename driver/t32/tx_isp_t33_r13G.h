/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AWB mode / colour temperature / freeze / start / statistics window / face window set-ups:
 * tisp_awb_set_mode, tisp_awb_set_ct, tisp_awb_set_frz, tisp_awb_set_start, tisp_awb_set_statis_localtion,
 * tisp_awb_api_get_face, tisp_awb_api_set_face, tisp_awb_face_get, tisp_awb_face_set, tisp_awb_face_pos.
 * On the raw blocks awbInfo[ch] points at (word 0 the AWB section, word 1 the run state, word 3 the mode block).
 */
#ifndef TX_ISP_T33_R13G_H
#define TX_ISP_T33_R13G_H

#include "tx_isp_t33_awbL.h"

#define T33_AWBM_FLAGS	0x10000U	/* run state offset of the mode / ct / freeze bytes */

struct t33_awbm_ops {
	void (*bad_mode)(void);						/* isp_printf (error) */
	void (*set_gain)(u32 ch);					/* tisp_awb_set_gain */
	void (*reg_write)(u32 ch, u32 mod, u32 slot, u32 val);		/* tisp_reg_write_t33 */
	void (*awb_trig)(u32 a, u32 b);					/* system_reg_set_awb_trig */
};

static inline u8 *t33_awbm_blk(const u8 *info, u32 word)
{
	return (u8 *)(unsigned long)t33_aL_r32(info, 4 * word);
}

/* tisp_awb_set_mode: a = { mode, manual ct word, manual word } */
static inline void t33_awbm_set_mode(const struct t33_awbm_ops *o, const u8 *info, const u32 *a)
{
	/* modes 2..8: words 1 and 2 of the mode block */
	static const u16 preset[9][2] = {
		{ 0, 0 }, { 0, 0 },
		{ 384, 384 },
		{ 438, 303 },
		{ 219, 690 },
		{ 240, 564 },
		{ 315, 459 },
		{ 468, 279 },
		{ 240, 376 },
	};
	u8 *m = t33_awbm_blk(info, 3), *st = t33_awbm_blk(info, 1) + T33_AWBM_FLAGS;
	u32 mode = a[0];

	if (mode >= 10) {
		o->bad_mode();
	} else if (mode == 0) {
		t33_aL_w32(m, 0, 0);
	} else {
		t33_aL_w32(m, 0, mode);
		if (mode == 1 || mode == 9) {
			t33_aL_w32(m, 4, a[1]);
			t33_aL_w32(m, 8, a[2]);
		} else {
			t33_aL_w32(m, 4, preset[mode][0]);
			t33_aL_w32(m, 8, preset[mode][1]);
		}
	}
	st[1082] = (u8)t33_aL_r32(m, 0);
	t33_aL_w16(st, 1088, t33_aL_r32(m, 4));
	t33_aL_w16(st, 1090, t33_aL_r32(m, 8));
	st[1080] = 1;
}

/* tisp_awb_set_ct */
static inline void t33_awbm_set_ct(const u8 *info, const u32 *a)
{
	t33_aL_w32(t33_awbm_blk(info, 1), T33_AWBM_FLAGS + 1084, a[0]);
}

/* tisp_awb_set_frz */
static inline void t33_awbm_set_frz(const u8 *info, u32 v)
{
	t33_awbm_blk(info, 1)[T33_AWBM_FLAGS + 1081] = (u8)v;
}

/* tisp_awb_set_start: tp = tparamsP[ch] */
static inline void t33_awbm_set_start(const struct t33_awbm_ops *o, u32 ch, const u8 *info, u8 *tp, const u32 *a)
{
	u8 *par = t33_awbm_blk(info, 0);

	t33_aL_w32(par, 16, a[0]);
	t33_aL_w32(par, 20, a[1]);
	t33_aL_w32(par, 24, a[0]);
	t33_aL_w32(par, 28, a[1]);
	t33_aL_w32(tp, 3416, a[0]);
	t33_aL_w32(tp, 3420, a[1]);
	t33_aL_w32(tp, 3424, a[0]);
	t33_aL_w32(tp, 3428, a[1]);
	o->set_gain(ch);
}

/* tisp_awb_set_statis_localtion: a = 12 byte window descriptor */
static inline void t33_awbm_set_window(const u8 *info, const u8 *a)
{
	u8 *par = t33_awbm_blk(info, 0), *st = t33_awbm_blk(info, 1);

	t33_aL_w16(par, 3184, t33_aL_r32(a, 4));
	t33_aL_w16(par, 3186, a[9]);
	t33_aL_w16(par, 3188, t33_aL_r32(a, 0));
	t33_aL_w16(par, 3190, a[8]);
	st[38056] = 0;
}

/* tisp_awb_api_get_face (tisp_awb_face_get): tab = awb_face_info, 20 bytes per channel */
static inline void t33_awbm_get_face(const u8 *tab, u32 ch, u8 *a)
{
	const u8 *f = tab + 20 * ch;

	t33_aL_w32(a, 4, t33_aL_r32(f, 0));
	t33_aL_w32(a, 8, t33_aL_r32(f, 4));
	t33_aL_w32(a, 12, t33_aL_r32(f, 8));
	t33_aL_w32(a, 16, t33_aL_r32(f, 12));
	t33_aL_w32(a, 0, t33_aL_r32(f, 16));
}

/* tisp_awb_face_pos: the face window (x0..x1, y0..y1) shrunk twice at most to 1/4 of the picture / 8 */
static inline void t33_awbm_face_pos(const struct t33_awbm_ops *o, u32 ch, const u8 *info, u8 *main_cnt,
				     u8 *refresh, u32 en, u32 xa, u32 xb, u32 ya, u32 yb)
{
	u8 *st = t33_awbm_blk(info, 1);
	u32 x0 = xa & 0xffff, x1 = xb & 0xffff, y0 = ya & 0xffff, y1 = yb & 0xffff;
	u32 thr = (t33_aL_r16(st, 28690) * t33_aL_r16(st, 28692)) >> 3, k, n;

	en &= 0xff;
	if (x1 < x0)
		x0 = x1;
	if (y1 < y0)
		y0 = y1;
	x0 = x0 < 2 ? 2 : x0;
	x1 = x1 < 2 ? 2 : x1;
	y0 = y0 < 2 ? 2 : y0;
	y1 = y1 < 2 ? 2 : y1;
	for (k = 0; k < 2; k++) {
		x0 = (x0 + (u32)((int)(x1 - x0) / 6)) & 0xffff;
		x1 = (x0 + (u32)((int)((x1 - x0) * 5) / 6)) & 0xffff;
		y0 = (y0 + (u32)((int)(y1 - y0) / 6)) & 0xffff;
		y1 = (y0 + (u32)((int)((y1 - y0) * 5) / 6)) & 0xffff;
		if ((u32)((int)((x1 - x0) * (y1 - y0)) / 4) < thr)
			break;
	}
	if (en == 1) {
		n = (*main_cnt + 1) & 0xff;
		*main_cnt = n == 255 ? 0 : (u8)n;
		o->reg_write(ch, 29, 37, y1 << 16 | y0);
		o->reg_write(ch, 29, 38, x1 << 16 | x0);
		o->awb_trig(1, 0);
		st[28706] = 1;
		st[28707] = 0;
	} else {
		o->reg_write(ch, 29, 37, 0x20002);
		o->reg_write(ch, 29, 38, 0x20002);
		o->awb_trig(1, 0);
		st[28706] = 0;
	}
	st[T33_AWBM_FLAGS + 1080] = 1;
	*refresh = 1;
}

/* tisp_awb_api_set_face (tisp_awb_face_set) */
static inline void t33_awbm_set_face(const struct t33_awbm_ops *o, u32 ch, const u8 *info, u8 *tab, u8 *main_cnt,
				     u8 *refresh, const u8 *a)
{
	u8 *f = tab + 20 * ch;

	t33_awbm_face_pos(o, ch, info, main_cnt, refresh, a[0], t33_aL_r16(a, 8), t33_aL_r16(a, 16),
			  t33_aL_r16(a, 4), t33_aL_r16(a, 12));
	t33_aL_w32(f, 0, t33_aL_r32(a, 4));
	t33_aL_w32(f, 4, t33_aL_r32(a, 8));
	t33_aL_w32(f, 8, t33_aL_r32(a, 12));
	t33_aL_w32(f, 12, t33_aL_r32(a, 16));
	t33_aL_w32(f, 16, t33_aL_r32(a, 0));
}

#endif /* TX_ISP_T33_R13G_H */
