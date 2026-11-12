/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE sensor setters: readable C of the vendor's tisp_set_ae_long_ag /
 * tisp_set_ae_short_ag (-> tisp_set_sensor_analog_gain / _short_) and
 * tisp_set_ae_sensor_fps.
 *
 * Vendor layout (channel c = ch & 0xff):
 *   T = e15a8[c] -> { tune, buf }, R = e1588[c] (AE state), S = dd2d4 + c * 232
 *   (the sensor block: word +56 the current fps word, function entries at
 *   +124/+184 (long gain), +128/+188 (short gain), +212 (fps)).
 *   tune: +1664 fixed fps, +1736 u16 shift, +1740 fixed-fps flag
 *   buf:  +7620/+7636 long/short gain the AE wants, +11772 fps mode,
 *         +11776 fps numerator, +11780 fps denominator
 *   R:    +580/+596 long/short sensor gain, +588/+604 long/short digital
 *         gain, +636/+668 their limits, +1264/+1268 fps min/max,
 *         +1272 sensor fps, +1279/+1283 and +1284 freeze flags
 * The sensor block is handed to the vendor's gain function by value; its
 * function entries take (value, info) where info is a stack block (32 B
 * for the gain, 128 B for fps) of which the vendor sets only the channel
 * (+0 for the gain, +8 for fps; the rest is uninitialised stack in the
 * vendor and 0 here).  The sensor's gain
 * comes back as a u16 at info +4 (long) / +14 (short).
 *
 * Vendor UB kept: tisp_set_ae_sensor_fps divides by buf +11780 without a zero
 * test (mode 1); ours takes the quotient as 0xffffffff there (what the reference C gives).  The functions return nothing (the vendor's v0 is a leftover).
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_apiag_oracle).
 */
#ifndef TX_ISP_T32_API_AG_H
#define TX_ISP_T32_API_AG_H

#include "tx_isp_t32_tp.h"

#define T32_SNS_FPS_WORD	56
#define T32_SNS_ALLOC_AGAIN	124
#define T32_SNS_ALLOC_AGAIN_S	128
#define T32_SNS_SET_AGAIN	184
#define T32_SNS_SET_AGAIN_S	188
#define T32_SNS_SET_FPS		212

struct t32_ae_ag_ops {
	u32 (*log2_fixed_to_fixed)(u32 v, u32 in_q, u32 out_q);
	u32 (*math_exp2)(u32 v, u32 in_q, u32 out_q);
	u32 (*mult2_32)(u32 q, u32 a, u32 b);
	u32 (*div_32)(u32 q, u32 a, u32 b);
	/* call the sensor block's function entry at off with (arg, info) */
	u32 (*sensor_call)(const u8 *sblk, u32 off, u32 arg, u8 *info);
};

struct t32_ae_ag_ctx {
	u8 *tune;	/* e15a8[c][0] */
	u8 *buf;	/* e15a8[c][1] */
	u8 *st;		/* e1588[c] */
	const u8 *sns;	/* dd2d4 + c * 232 */
};

static inline u32 t32_ag_rd32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline void t32_ag_wr32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/*
 * tisp_set_sensor_analog_gain (short = 0) / tisp_set_sensor_short_analog_gain
 * (short = 1): the sensor rounds log2(gain) to its own step, the vendor sets
 * the step the sensor reports and returns the gain of the rounded value.
 */
static inline u32 t32_set_sensor_analog_gain(const struct t32_ae_ag_ops *op, u32 ch, u32 again,
					     const u8 *sns, int is_short)
{
	u8 info[32] = { 0 };
	u32 l, g;

	t32_ag_wr32(info, ch & 0xff);
	l = op->log2_fixed_to_fixed(again << 6, 16, 16);
	l = op->sensor_call(sns, is_short ? T32_SNS_ALLOC_AGAIN_S : T32_SNS_ALLOC_AGAIN, l, info);
	g = op->math_exp2(l, 16, 16);
	op->sensor_call(sns, is_short ? T32_SNS_SET_AGAIN_S : T32_SNS_SET_AGAIN,
			info[is_short ? 14 : 4] | (u32)info[is_short ? 15 : 5] << 8, info);
	return g >> 6;
}

/* tisp_set_ae_long_ag (short = 0) / tisp_set_ae_short_ag (short = 1) */
static inline void t32_set_ae_ag(const struct t32_ae_ag_ops *op, const struct t32_ae_ag_ctx *c, u32 ch,
				 int is_short)
{
	u8 *st = c->st;
	u32 ag = t32_ag_rd32(st + (is_short ? 596 : 580));
	u32 dg, sh, g, d, lim;

	if (t32_ag_rd32(c->buf + (is_short ? 7636 : 7620)) == ag)
		return;
	dg = t32_ag_rd32(st + (is_short ? 604 : 588));
	sh = t32_ag_rd32(c->tune + 1736) & 0xffff;
	g = t32_set_sensor_analog_gain(op, ch, ag, c->sns, is_short);
	t32_ag_wr32(st + (is_short ? 596 : 580), g);
	if (st[is_short ? 1283 : 1279] || st[1284])
		return;
	d = op->div_32(sh, op->mult2_32(sh, ag, dg), g);
	lim = t32_ag_rd32(st + (is_short ? 668 : 636));
	t32_ag_wr32(st + (is_short ? 604 : 588), lim < d ? lim : d);
}

/* tisp_set_ae_sensor_fps: clamp the AE's fps to [min, max] and hand it to the sensor */
static inline void t32_set_ae_sensor_fps(const struct t32_ae_ag_ops *op, const struct t32_ae_ag_ctx *c, u32 ch)
{
	u8 info[128] = { 0 };
	u8 *tune = c->tune, *buf = c->buf, *st = c->st;
	u32 den = t32_ag_rd32(buf + 11780), mn = t32_ag_rd32(st + 1264), mx = t32_ag_rd32(st + 1268);
	u32 sh = (tune[1736] | (u32)tune[1737] << 8) & 31, w;

	t32_ag_wr32(info + 8, ch & 0xff);
	if (t32_ag_rd32(buf + 11772) == 1) {
		u32 v = (den ? t32_ag_rd32(buf + 11776) / den : 0xffffffff) << sh;

		if (mx < v)
			t32_ag_wr32(buf + 11776, mx >> sh);
		else if (v < mn)
			t32_ag_wr32(buf + 11776, mn >> sh);
		w = (t32_ag_rd32(buf + 11776) << 16) + den;
		if (w != t32_ag_rd32(c->sns + T32_SNS_FPS_WORD))
			op->sensor_call(c->sns, T32_SNS_SET_FPS, w, info);
		return;
	}
	if (t32_ag_rd32(tune + 1740) == 1) {
		u32 x = t32_ag_rd32(tune + 1664);

		if (mx < x)
			t32_ag_wr32(tune + 1664, mx);
		else if (x < mn)
			t32_ag_wr32(tune + 1664, mn);
		w = (((t32_ag_rd32(tune + 1664) << 1) >> sh) << 16) + 2;
		if (w != t32_ag_rd32(c->sns + T32_SNS_FPS_WORD))
			op->sensor_call(c->sns, T32_SNS_SET_FPS, w, info);
	} else {
		w = (((t32_ag_rd32(st + 1272) << 1) >> sh) << 16) + 2;
	}
	t32_ag_wr32(buf + 11776, (w - 2) >> 16);
	t32_ag_wr32(buf + 11780, 2);
}

#endif
