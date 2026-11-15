/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AE region-of-interest and frame-rate entry points: tisp_ae_calc_process,
 * tisp_ae_sepc_area, tisp_ae_roi_point, tisp_set_fps, tisp_ae_api_set_fps.
 * Hardware, printk and the AE calc itself are reached through struct t33_aeq_ops.
 */
#ifndef TX_ISP_T33_AEQ_H
#define TX_ISP_T33_AEQ_H

#include "tx_isp_t33_blk.h"

static inline void t33_aeq_wr32(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
	p[off + 2] = (u8)(v >> 16);
	p[off + 3] = (u8)(v >> 24);
}

struct t33_aeq_ops {
	void (*reg_write)(u32 ch, u32 mod, u32 slot, u32 val);
	void (*err)(u32 level);
	int (*normal_calc)(u32 a0, u32 a1, u32 a2, u32 a3, u32 s0, u32 s1, u32 s2, u32 s3, u32 s4, u32 s5);
	void (*deflicker)(u32 ch);
};

static inline u32 t33_aeq_le16(const u8 *p)
{
	return p[0] | p[1] << 8;
}

/* tisp_ae_calc_process: forwards everything to tisp_normal_ae_calc_process */
static inline int t33_ae_calc_process_fwd(u32 a0, u32 a1, u32 a2, u32 a3, u32 s0, u32 s1, u32 s2, u32 s3,
					  u32 s4, u32 s5, const struct t33_aeq_ops *o)
{
	o->normal_calc(a0, a1, a2, a3, s0, s1, s2, s3, s4, s5);
	return 0;
}

/* tisp_ae_roi_point: ROI corners into the AE list (module 30) */
static inline int t33_ae_roi_point(u32 ch, u32 x0, u32 y0, u32 x1, u32 y1, const u8 *cfg_mode, u8 **cache,
				   const struct t33_aeq_ops *o)
{
	u32 c = ch & 0xff, area;
	u8 *R;

	x0 &= 0xffff;
	y0 &= 0xffff;
	x1 &= 0xffff;
	y1 &= 0xffff;
	if (x1 < x0 || y1 < y0) {
		o->err(1);
		return -1;
	}
	area = (u32)((int)((x1 - x0) * (y1 - y0)) >> 2);
	if (area >= 0x100000) {
		o->err(1);
		o->err(1);
		return -1;
	}
	o->reg_write(c, 30, *cfg_mode ? 17 : 0x17090, x1 << 16 | x0);
	o->reg_write(c, 30, *cfg_mode ? 18 : 0x17094, y1 << 16 | y0);
	o->reg_write(c, 30, *cfg_mode ? 1 : 0x17010, 1);
	R = cache[c];
	R[1297] = (u8)(t33_le32(R + 1520) == 1);
	return 0;
}

/* tisp_ae_sepc_area: p = 6 words (mode, x0, y0, x1, y1, weight) */
static inline void t33_ae_sepc_area(u32 ch, u8 *p, u8 **cache, u8 **info, u8 *hs, u8 *he, u8 *vs, u8 *ve,
				    const u8 *cfg_mode, const struct t33_aeq_ops *o)
{
	u32 x0 = t33_le32(p + 4), y0 = t33_le32(p + 8), x1 = t33_le32(p + 12), y1 = t33_le32(p + 16), v;
	u32 bw = t33_aeq_le16(info[ch] + 2828) / 15, bh = t33_aeq_le16(info[ch] + 2830) / 15;
	u8 *R = cache[ch];

	t33_aeq_wr32(R, 1520, t33_le32(p));
	hs[ch] = (u8)(x0 / bw);
	vs[ch] = (u8)(y0 / bh);
	he[ch] = (u8)(x1 / bw + 1);
	ve[ch] = (u8)(y1 / bh + 1);
	if (hs[ch] >= 15)
		hs[ch] = 14;
	if (vs[ch] >= 15)
		vs[ch] = 14;
	if (he[ch] >= 15)
		he[ch] = 15;
	if (ve[ch] >= 15)
		ve[ch] = 15;
	t33_ae_roi_point(ch & 0xff, x0 & 0xffff, y0 & 0xffff, x1 & 0xffff, y1 & 0xffff, cfg_mode, cache, o);
	v = t33_le32(p + 20);
	if (v > 200)
		v = 200;
	else if (v < 10)
		v = 10;
	t33_aeq_wr32(p, 20, v);
	R = cache[ch];
	R[1296] = (u8)v;
	t33_aeq_wr32(R, 1500, x0);
	t33_aeq_wr32(R, 1504, y0);
	t33_aeq_wr32(R, 1508, x1);
	t33_aeq_wr32(R, 1512, y1);
	t33_aeq_wr32(R, 1516, v);
}

/* tisp_ae_api_set_fps: fps = pointer to the u32 (num << 16 | den) */
static inline int t33_ae_api_set_fps(u32 ch, u8 *fps, u8 **info, const u8 *attr, const struct t33_aeq_ops *o)
{
	const u8 *T = (const u8 *)(unsigned long)t33_le32(info[ch] + 1744);
	const u8 *a = attr + ch * 232;
	u32 f = t33_le32(fps);
	u8 tmp[16] = { 0 };
	int (*cb)(u32, void *);

	if (t33_le32(T + 1740) == 1) {
		o->err(1);
		return -1;
	}
	if (t33_le32(a + 56) != f) {
		cb = (int (*)(u32, void *))(unsigned long)t33_le32(a + 212);
		cb(f, tmp);
	}
	return 0;
}

/* tisp_set_fps */
static inline int t33_set_fps(u32 ch, u32 fps, u8 **info, const u8 *attr, const struct t33_aeq_ops *o)
{
	u8 b[4];
	u8 tmp[16] = { 0 };
	int q, num, den;
	int (*cb)(u32, void *);

	t33_aeq_wr32(b, 0, fps);
	t33_ae_api_set_fps(ch, b, info, attr, o);
	fps = t33_le32(b);
	num = (int)fps >> 16;
	den = (int)(fps & 0xffff);
	/* vendor: plain MIPS div, no zero check; the emulated core takes divisor 1 then */
	q = den ? num / den : num;
	cb = (int (*)(u32, void *))(unsigned long)t33_le32(attr + ch * 232 + 144);
	if (cb((u32)q & 0xff, tmp) < 0) {
		o->err(2);
		return -1;
	}
	o->deflicker(ch);
	return 0;
}

#endif /* TX_ISP_T33_AEQ_H */
