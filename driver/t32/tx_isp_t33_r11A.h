/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AWB: tisp_awb_long_par_update (0x3949c) and tisp_awb_spec_calculate (0x3abb0), on the raw byte
 * blocks awbInfo[ch] points at (word 0 par: the AWB section, word 1 st: the run state, word 4 aux).
 * The module functions they call come in through t33_r11A_ops.
 */
#ifndef TX_ISP_T33_R11A_H
#define TX_ISP_T33_R11A_H

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
#include "tx_isp_t33_blk.h"

struct t33_r11A_ops {
	void (*reg_write)(u32 ch, u32 mod, u32 slot, u32 val);	/* tisp_reg_write_t33 */
	u32 (*reg_read)(u32 reg);				/* system_reg_read */
	u32 (*mult)(u32 bits, u32 a, u32 b);			/* fix_point_mult2_32 */
};

static inline u32 t33_r11A_ptr(const void *p)
{
	return (u32)(unsigned long)p;
}

static inline u32 t33_r11A_r32(const u8 *p, u32 off)
{
	return t33_le32(p + off);
}

static inline u32 t33_r11A_r16(const u8 *p, u32 off)
{
	return p[off] | (u32)p[off + 1] << 8;
}

static inline void t33_r11A_w32(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
	p[off + 2] = (u8)(v >> 16);
	p[off + 3] = (u8)(v >> 24);
}

static inline void t33_r11A_w16(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
}

/* (a * d + (b - a) * x) / d, 32 bit */
static inline u32 t33_r11A_lerp(u32 a, u32 b, u32 x, u32 d)
{
	return (a * d + (b - a) * x) / d;
}

static inline void t33_r11A_copy4(u8 *dst, const u8 *src)
{
	unsigned int k;

	for (k = 0; k < 16; k += 4)
		t33_r11A_w32(dst, k, t33_r11A_r32(src, k));
}

/* four words: lerp between src[k] and src[k + 16] */
static inline void t33_r11A_lerp4(u8 *dst, const u8 *src, u32 x, u32 d)
{
	unsigned int k;

	for (k = 0; k < 16; k += 4)
		t33_r11A_w32(dst, k, t33_r11A_lerp(t33_r11A_r32(src, k), t33_r11A_r32(src, k + 16), x, d));
}

/* the t7 == 0 end of the table choice (vendor 0x39754 .. 0x397b8, 0x39764) */
static inline void t33_r11A_long_tail(u8 *par, u8 *st, u8 *tbl8, u8 *tbl12, u32 cur, u32 t3, u32 d, u32 t1)
{
	u32 hi = t3 + d;

	if (!(cur < hi)) {
		t33_r11A_w32(tbl12, 0, 0);
		t33_r11A_copy4(tbl8, par + 196);
		t33_r11A_w32(tbl12, 4, 0x10000);
	} else if (cur >= t3) {
		t33_r11A_w32(tbl12, 0, t1);
		t33_r11A_copy4(tbl8, par + 196);
		t33_r11A_w32(tbl12, 4, t33_r11A_lerp(t33_r11A_r32(st, 28676), 0x10000, cur - t3, d));
	}
}

/* tisp_awb_long_par_update: fills the table tbl of block addresses and the interpolated st blocks */
static inline void t33_awb_long_par_update(u8 *par, u8 *st, u32 aux, u8 *tbl)
{
	u32 cur = t33_r11A_r32(st, 66608);
	u32 t0 = t33_r11A_r32(par, 32), t3 = t33_r11A_r32(par, 36);
	u32 w228 = t33_r11A_r32(par, 228), w236 = t33_r11A_r32(par, 236);
	u32 sh = t33_r11A_r16(par, 3308) & 31;
	u32 t1 = w228 & 0xffff, t7 = w236 & 0xffff, d, hi;
	u32 pa = t33_r11A_ptr(par), sa = t33_r11A_ptr(st);
	u8 *tbl8 = st + 0x9490, *tbl12 = st + 0x94a0;

	t33_r11A_w32(par, 48, t33_r11A_r32(par, 44));
	t33_r11A_w32(tbl, 8, t33_r11A_ptr(tbl8));
	t33_r11A_w32(tbl, 12, t33_r11A_ptr(tbl12));
	t33_r11A_w32(st, 28672, w228);
	t33_r11A_w32(st, 28676, t33_r11A_r32(par, 232) << sh);
	t33_r11A_w32(st, 28680, w236);
	t33_r11A_w32(st, 28684, t33_r11A_r32(par, 240) << sh);
	if (cur < t0 || cur < t3) {
		st[28688] = 0;
		st[28689] = par[5703];
		t33_r11A_w32(tbl, 16, pa + 1264);
		t33_r11A_w32(tbl12, 0, t1);
		t33_r11A_w32(tbl12, 4, t33_r11A_r32(st, 28676));
	} else {
		st[28688] = par[5702];
		st[28689] = par[5703];
		t33_r11A_w32(tbl, 16, pa + 364);
		t33_r11A_w32(tbl12, 0, t7);
		t33_r11A_w32(tbl12, 4, t33_r11A_r32(st, 28684));
	}
	d = (u32)((int)(t3 - t0) / 3);
	hi = t3 + d;
	if (t1 == 1) {
		if (cur < t0) {
			t33_r11A_copy4(tbl8, par + 180);
		} else if (cur < t0 + d) {
			t33_r11A_lerp4(tbl8, par + 180, cur - t0, d);
		} else if (cur < t3) {
			t33_r11A_copy4(tbl8, par + 196);
		} else if (cur >= hi) {
			t33_r11A_copy4(tbl8, par + 212);
		} else {
			t33_r11A_lerp4(tbl8, par + 196, cur - t3, d);
			t33_r11A_w32(tbl12, 4, t33_r11A_lerp(t33_r11A_r32(st, 28676), t33_r11A_r32(st, 28684), cur - t3, d));
		}
		if (!t7)
			t33_r11A_long_tail(par, st, tbl8, tbl12, cur, t3, d, t1);
	} else if (t1 == 0) {
		t33_r11A_copy4(tbl8, par + 212);
		t33_r11A_w32(tbl12, 4, 0x10000);
		if (t7 == 1) {
			if (cur >= hi)
				t33_r11A_w32(tbl12, 4, t33_r11A_r32(st, 28684));
			else if (cur >= t3)
				t33_r11A_w32(tbl12, 4, ((d << 16) - (0x10000 - t33_r11A_r32(st, 28684)) * (cur - t3)) / d);
		}
	}
	/* the table of block addresses (par, st, aux based) */
	t33_r11A_w32(tbl, 0, pa + 3322);
	t33_r11A_w32(tbl, 4, sa + 28688);
	t33_r11A_w32(tbl, 20, pa + 3184);
	t33_r11A_w32(tbl, 24, pa + 60);
	t33_r11A_w32(tbl, 28, pa + 120);
	t33_r11A_w32(tbl, 32, pa + 2164);
	t33_r11A_w32(tbl, 36, pa + 5714);
	t33_r11A_w32(tbl, 40, pa + 244);
	t33_r11A_w32(tbl, 44, pa + 44);
	t33_r11A_w32(tbl, 48, sa + 28764);
	t33_r11A_w32(tbl, 52, pa + 3306);
	t33_r11A_w32(tbl, 56, pa + 3448);
	t33_r11A_w32(tbl, 60, pa + 3438);
	t33_r11A_w32(tbl, 64, pa + 292);
	t33_r11A_w32(tbl, 68, pa + 3362);
	t33_r11A_w32(tbl, 72, sa + 28708);
	t33_r11A_w32(tbl, 76, pa + 324);
	t33_r11A_w32(tbl, 80, sa + 28718);
	t33_r11A_w32(tbl, 84, sa + 28738);
	t33_r11A_w32(tbl, 88, pa + 5126);
	t33_r11A_w32(tbl, 92, pa + 5166);
	t33_r11A_w32(tbl, 96, pa + 52);
	t33_r11A_w32(tbl, 100, sa + 29800);
	t33_r11A_w32(tbl, 104, pa + 5206);
	t33_r11A_w32(tbl, 108, aux + 4246);
	t33_r11A_w32(tbl, 112, pa + 5226);
	t33_r11A_w32(tbl, 116, pa + 5234);
	t33_r11A_w32(tbl, 120, sa + 0x9468);
	t33_r11A_w32(tbl, 124, sa + 0x947c);
	t33_r11A_w32(tbl, 128, pa + 5682);
	t33_r11A_w32(tbl, 132, sa);
	t33_r11A_w32(tbl, 136, sa + 16384);
	t33_r11A_w32(tbl, 140, sa + 24576);
	t33_r11A_w32(tbl, 144, sa + 0x9568);
	t33_r11A_w32(tbl, 148, sa + 0x9878);
	t33_r11A_w32(tbl, 152, sa + 0x9b88);
	t33_r11A_w32(tbl, 156, sa + 0x9d10);
	t33_r11A_w32(tbl, 160, sa + 0x9da0);
	t33_r11A_w32(tbl, 164, sa + 0xa070);
	t33_r11A_w32(tbl, 168, sa + 0xa430);
	t33_r11A_w32(tbl, 172, sa + 0xb430);
	t33_r11A_w32(tbl, 176, sa + 0xc430);
	t33_r11A_w32(tbl, 180, sa + 0xd430);
	t33_r11A_w32(tbl, 184, sa + 0xe430);
	t33_r11A_w32(tbl, 188, sa + 0xf430);
}

/* (mult(10, a << 2, b) + 2) >> 2 */
static inline u32 t33_r11A_scale(const struct t33_r11A_ops *o, u32 a, u32 b)
{
	return (o->mult(10, a << 2, b) + 2) >> 2;
}

/* one of the five flag sections of tisp_awb_spec_calculate */
static inline void t33_r11A_sec(const struct t33_r11A_ops *o, u32 ch, u8 *par, u8 *st, u32 flagoff, u32 wa, u32 wb,
				u32 ha, u32 hb, u32 sa, u32 sb, u32 shoff, u32 finoff, u32 slot_a, u32 slot_b,
				u32 slot_f)
{
	u32 flag = par[flagoff];
	u32 fin = t33_r11A_r32(par, finoff);

	if (flag == 1) {
		o->reg_write(ch, 29, slot_a, t33_r11A_r32(par, wa));
		o->reg_write(ch, 29, slot_b, t33_r11A_r32(par, wb));
		fin |= (u32)st[shoff] << 16;
	} else if (flag) {
		t33_r11A_w16(st, sa, t33_r11A_scale(o, t33_r11A_r16(par, ha), t33_r11A_r32(par, 52)));
		t33_r11A_w16(st, sb, t33_r11A_scale(o, t33_r11A_r16(par, hb), t33_r11A_r32(par, 56)));
	}
	o->reg_write(ch, 29, slot_f, fin);
}

/* tisp_awb_spec_calculate: list 29 words (slots 17 .. 36) from the section and the statistics registers */
static inline void t33_awb_spec_calculate(const struct t33_r11A_ops *o, u32 ch, u8 *par, u8 *st)
{
	volatile u32 len = 10;	/* not constant: the call to memcpy stays a call */
	u32 mode = st[28706], p, hi = 0;

	memcpy(st + 28708, par + 5704, len);
	if (mode == 1 && st[28707] < 2)
		st[28707]++;
	p = par[5704];
	if (p) {
		if (mode != 1 || st[28707] < 2) {
			st[28708] = 0;
		} else {
			u32 r1, r2, r3, a1, s4;

			r1 = o->reg_read(0x3fff);
			r2 = o->reg_read(0x3fff);
			r3 = o->reg_read(0x3fff);
			a1 = r2 ? (r1 << 8) / r2 : 0;
			s4 = r2 ? (r3 << 8) / r2 : 0;
			t33_r11A_w16(par, 3460, a1 & 0xffff);
			t33_r11A_w16(par, 3480, s4 & 0xffff);
			if (p == 1) {
				o->reg_write(ch, 29, 17, t33_r11A_r16(par, 3462) << 16 | (a1 & 0xffff));
				o->reg_write(ch, 29, 22, t33_r11A_r32(par, 3480));
				hi = (u32)st[28708] << 16;
			} else {
				t33_r11A_w16(st, 28718, t33_r11A_scale(o, a1, t33_r11A_r32(par, 52)));
				t33_r11A_w16(st, 28738, t33_r11A_scale(o, s4, t33_r11A_r32(par, 56)));
			}
		}
	}
	o->reg_write(ch, 29, 27, t33_r11A_r32(par, 324) | hi);
	t33_r11A_sec(o, ch, par, st, 5709, 3468, 3488, 3470, 3490, 28728, 28748, 28713, 344, 19, 24, 32);
	t33_r11A_sec(o, ch, par, st, 5710, 3472, 3492, 3472, 3492, 28730, 28750, 28714, 348, 20, 25, 33);
	t33_r11A_sec(o, ch, par, st, 5711, 3472, 3492, 3474, 3494, 28732, 28752, 28715, 352, 20, 25, 34);
	t33_r11A_sec(o, ch, par, st, 5712, 3476, 3496, 3476, 3496, 28734, 28754, 28716, 356, 21, 26, 35);
	t33_r11A_sec(o, ch, par, st, 5713, 3476, 3496, 3478, 3498, 28736, 28756, 28717, 360, 21, 26, 36);
}

#endif /* TX_ISP_T33_R11A_H */
