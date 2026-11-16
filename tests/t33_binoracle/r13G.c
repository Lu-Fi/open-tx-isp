/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r13G": AWB mode / ct / freeze / start / window / face set-ups with the calling
 * convention of the vendor functions (spec: r13G.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r13G.h"

extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 slot, u32 val);
extern void system_reg_set_awb_trig(u32 a, u32 b);
extern void tisp_awb_set_gain(u32 ch);
extern int isp_printf(int lvl, const char *fmt, ...);

u32 our_awbInfo[4];
u32 our_tparamsP[4];
u8 our_main_face_en_cnt;
u8 our_tispAwbRefresh;
u8 our_awb_face_info[80];

static void r13G_bad_mode(void)
{
	isp_printf(2, "invalid awb mode %s\n", "tisp_awb_set_mode", 3675);
}

static void r13G_ops(struct t33_awbm_ops *o)
{
	o->bad_mode = r13G_bad_mode;
	o->set_gain = tisp_awb_set_gain;
	o->reg_write = tisp_reg_write_t33;
	o->awb_trig = system_reg_set_awb_trig;
}

static u8 *r13G_info(u32 ch)
{
	return (u8 *)(unsigned long)our_awbInfo[ch];
}

int bo_tisp_awb_set_mode(u32 ch, const u32 *a)
{
	struct t33_awbm_ops o;

	r13G_ops(&o);
	t33_awbm_set_mode(&o, r13G_info(ch), a);
	return 0;
}

void bo_tisp_awb_set_ct(u32 ch, const u32 *a) { t33_awbm_set_ct(r13G_info(ch), a); }

void bo_tisp_awb_set_frz(u32 ch, u32 v) { t33_awbm_set_frz(r13G_info(ch), v); }

void bo_tisp_awb_set_start(u32 ch, const u32 *a)
{
	struct t33_awbm_ops o;

	r13G_ops(&o);
	t33_awbm_set_start(&o, ch, r13G_info(ch), (u8 *)(unsigned long)our_tparamsP[ch], a);
}

void bo_tisp_awb_set_statis_localtion(u32 ch, const u8 *a) { t33_awbm_set_window(r13G_info(ch), a); }

void bo_tisp_awb_api_get_face(u32 ch, u8 *a) { t33_awbm_get_face(our_awb_face_info, ch, a); }

void bo_tisp_awb_api_set_face(u32 ch, const u8 *a)
{
	struct t33_awbm_ops o;

	r13G_ops(&o);
	t33_awbm_set_face(&o, ch, r13G_info(ch), our_awb_face_info, &our_main_face_en_cnt, &our_tispAwbRefresh, a);
}

int bo_tisp_awb_face_get(u32 ch, u8 *a)
{
	t33_awbm_get_face(our_awb_face_info, ch, a);
	return 0;
}

int bo_tisp_awb_face_set(u32 ch, const u8 *a)
{
	struct t33_awbm_ops o;

	r13G_ops(&o);
	t33_awbm_set_face(&o, ch, r13G_info(ch), our_awb_face_info, &our_main_face_en_cnt, &our_tispAwbRefresh, a);
	return 0;
}

int bo_tisp_awb_face_pos(u32 ch, u32 en, u32 xa, u32 xb, u32 ya, u32 yb)
{
	struct t33_awbm_ops o;

	r13G_ops(&o);
	t33_awbm_face_pos(&o, ch, r13G_info(ch), &our_main_face_en_cnt, &our_tispAwbRefresh, en, xa, xb, ya, yb);
	return 0;
}
