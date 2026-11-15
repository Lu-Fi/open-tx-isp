/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) block init functions: tisp_gamma_init, tisp_lsc_init, tisp_ae_init
 * (the vendor's allocation of the per-channel block states and the calls into the
 * already ported block code).  The callees are reached through an ops table so the
 * binary oracle (tests/t33_binoracle/i9E.c) can record the calls.
 */
#ifndef TX_ISP_T33_I9E_H
#define TX_ISP_T33_I9E_H

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif
#include "tx_isp_t33_blk.h"

struct t33_i9e_ops {
	int (*module_register)(u32 ch, u32 id, const void *reg_init);
	void *(*alloc_v)(size_t n);
	void *(*alloc_k)(size_t n, u32 flags);
	void (*release)(const void *p);
	void *(*copy)(void *d, const void *s, size_t n);
	void *(*fill)(void *d, int c, size_t n);
	void (*spin_init)(void *l);
	void (*warn)(u32 line, int r);
	void (*msg)(u32 level, u32 line);
	int (*irq_set)(u32 ch, u32 n, const void *fn);
	int (*event_cb)(u32 ch, u32 n, const void *fn);
	int (*reg_write)(u32 ch, u32 blk, u32 reg, u32 val);
	int (*gamma_interp)(u32 ch, const void *ap, u32 lo, u32 hi, u32 force);
	int (*gamma_curve)(u32 ch, const void *ap, u32 lo, u32 hi);
	int (*gamma_strength)(u32 ch);
	int (*gamma_lut)(u32 ch);
	int (*lsc_valid)(u32 a, u32 b, u32 c, u32 d, u8 *p8, u8 *p9, u8 *p10, u8 *p11, u8 *p0, u8 *p21,
			 u8 *p4);
	int (*lsc_malloc)(u32 ch);
	int (*lsc_tgain)(u32 ch, u32 gain, u32 force);
	int (*lsc_ct)(u32 ch, u32 ct, u32 force);
	int (*lsc_para)(u32 ch);
	int (*lsc_write)(u32 ch, u32 sel);
	int (*ae_malloc)(u32 ch, void *ctx);
	u32 (*sensor_attr)(u32 ch);
	int (*ae_normal)(u32 ch, void *hw, void *par);
	const void *gamma_reg_init, *gamma_irq, *lsc_reg_init, *lsc_irq, *ae_reg_init, *dg_reg_init,
		   *ae_hist, *ae_static, *ae_calc;
};

static inline void t33_i9e_st32(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
	p[off + 2] = (u8)(v >> 16);
	p[off + 3] = (u8)(v >> 24);
}

static inline u32 t33_i9e_ptr(const void *p)
{
	return (u32)(uintptr_t)p;
}

/* ---- gamma: gamma_para[ch] (24 bytes) ---------------------------------- */
struct t33_gamma_blk {
	u8 *sec;
	u8 *cur;
	u32 *ev;
	u8 *str;
	u8 *cur2;
	u8 *api;
};

/* tisp_gamma_init */
static inline int t33_gamma_module_init(const struct t33_i9e_ops *o, u32 ch, const u8 *info,
					struct t33_gamma_blk **gp, u8 **tp, const u8 *cp, const u8 *ip,
					const u8 *sp, const u8 *ap, u8 *refresh)
{
	u32 c = ch & 0xff;
	struct t33_gamma_blk *g;
	int r;

	r = o->module_register(c, 11, o->gamma_reg_init);
	*refresh = 0;
	if (r < 0)
		o->warn(562, r);
	g = (struct t33_gamma_blk *)o->alloc_v(24);
	gp[ch] = g;
	g->sec = tp[ch] + 23424;
	g->cur = (u8 *)o->alloc_v(260);
	g->ev = (u32 *)o->alloc_v(8);
	g->str = (u8 *)o->alloc_v(260);
	g->api = (u8 *)o->alloc_v(264);
	g->cur2 = (u8 *)o->alloc_v(258);
	o->spin_init((u8 *)g + 24);
	o->copy(g->cur, cp, 260);
	o->copy(g->ev, ip, 8);
	o->copy(g->str, sp, 260);
	o->copy(g->api, ap, 264);
	g->ev[0] = t33_le32(info + 924) << 22 | t33_le32(info + 920) >> 10;
	g->ev[1] = t33_le32(info + 924) >> 10;
	g->sec[66] = 0;
	g->sec[67] = 0;
	o->gamma_interp(ch, ap, g->ev[0], g->ev[1], 1);
	o->gamma_curve(c, ap, g->ev[0], g->ev[1]);
	o->gamma_strength(ch);
	o->gamma_lut(ch);
	o->irq_set(c, 46, o->gamma_irq);
	o->msg(0, 385);
	return 0;
}

/* ---- lsc: lsc_para[ch] (196 bytes) ------------------------------------- */
struct t33_lsc_r {
	u8 b[24];
	u8 *buf[7];
};

struct t33_lsc_blk {
	u8 *sec;
	u32 *q;
	struct t33_lsc_r *r;
	u8 *s;
	u32 pad[2];
	u8 flag;
};

/* tisp_lsc_init (the vendor's section copy of the tables goes to a buffer nobody reads back here) */
static inline int t33_lsc_module_init(const struct t33_i9e_ops *o, u32 ch, const u8 *info,
				      struct t33_lsc_blk **lp, u8 **tp, u8 **lut, const u8 *mode,
				      u8 *refresh)
{
	u32 c = ch & 0xff, i;
	struct t33_lsc_blk *p;
	struct t33_lsc_r *rr;
	const u8 *tbl;
	u32 *q;
	u8 *sec;
	u32 v;

	p = (struct t33_lsc_blk *)o->alloc_v(196);
	lp[c] = p;
	o->module_register(c, 6, o->lsc_reg_init);
	o->spin_init((u8 *)p + 24);
	*refresh = 0;
	o->spin_init((u8 *)p + 24);
	sec = tp[c] + 17488;
	p->flag = 0;
	p->sec = sec;
	p->q = (u32 *)o->alloc_v(32);
	p->r = (struct t33_lsc_r *)o->alloc_v(52);
	p->s = (u8 *)o->alloc_v(12);
	q = p->q;
	rr = p->r;
	q[0] = t32_tp_le16(info + 14);
	q[1] = t32_tp_le16(info + 12);
	q[2] = t33_le32(info + 976);
	q[3] = 255;
	q[5] = t33_le32(info + 928);
	q[6] = t33_le32(info + 952);
	q[4] = (t33_le32(info + 4) & 0x1f) >= 4;
	q[7] = info[40];
	rr->b[19] = 0xff;
	o->lsc_valid(q[0], q[1], sec[88], sec[89], rr->b + 8, rr->b + 9, rr->b + 10, rr->b + 11, rr->b,
		     rr->b + 21, rr->b + 4);
	if (!rr->b[21]) {
		o->msg(1, 1175);
		sec[88] = 80;
		sec[89] = 80;
		o->lsc_valid(q[0], q[1], sec[88], sec[89], rr->b + 8, rr->b + 9, rr->b + 10, rr->b + 11,
			     rr->b, rr->b + 21, rr->b + 4);
	}
	rr->buf[0] = (u8 *)o->alloc_v(32);
	for (i = 1; i < 7; i++)
		rr->buf[i] = (u8 *)o->alloc_v(4608);
	tbl = (const u8 *)(uintptr_t)t33_le32(lut[c] + 20);
	for (i = 0; i < sec[148]; i++) {
		v = t32_tp_le16(tbl + i * 13826);
		rr->buf[0][2 * i] = (u8)v;
		rr->buf[0][2 * i + 1] = (u8)(v >> 8);
	}
	o->lsc_malloc(c);
	o->lsc_tgain(c, t33_le32(info + 928), 1);
	o->lsc_ct(c, t32_tp_le16(info + 976), 1);
	o->lsc_para(c);
	o->lsc_write(c, 0);
	o->irq_set(c, 36, o->lsc_irq);
	o->reg_write(c, 6, *mode ? 6 : 20556, 1);
	o->reg_write(c, 6, *mode ? 1 : 20496, 1);
	return 0;
}

/* ---- ae: g_ae_contexts[ch] (22984 bytes) ------------------------------- */
/* tisp_ae_init */
static inline int t33_ae_module_init(const struct t33_i9e_ops *o, u32 ch, const u8 *info, u8 **ctxs,
				     u8 **tp)
{
	u32 c = ch & 0xff, v;
	u8 *x;
	int r;

	if (!ctxs[c]) {
		ctxs[c] = (u8 *)o->alloc_k(22984, 208);
		if (!ctxs[c]) {
			o->msg(2, 229);
			return -12;
		}
	}
	x = ctxs[c];
	o->fill(x, 0, 22984);
	t33_i9e_st32(x, 44, t33_i9e_ptr(info));
	o->module_register(c, 30, o->ae_reg_init);
	r = o->module_register(c, 5, o->dg_reg_init);
	if (r < 0)
		o->warn(241, r);
	if ((r = o->ae_malloc(c, x)) != 0) {
		o->warn(246, r);
		o->release(x);
		ctxs[c] = 0;
		return r;
	}
	o->spin_init(x + 22972);
	t33_i9e_st32(x, 1800, t33_i9e_ptr(tp[c] + 112));
	t33_i9e_st32(x, 20004, t33_le32(x + 44));
	t33_i9e_st32(x, 20008, t33_i9e_ptr(x + 20));
	t33_i9e_st32(x, 20012, t33_i9e_ptr(x + 32));
	t33_i9e_st32(x, 20000, o->sensor_attr(c));
	t33_i9e_st32(x, 20016, t33_le32(info + 904));
	o->ae_normal(c, x + 48, x + 20024);
	t33_i9e_st32(x, 22968, 1);
	o->reg_write(c, 30, 3, (t32_tp_le16(x) & 0xfff) << 16 | (t32_tp_le16(x + 2) & 0xfff));
	o->reg_write(c, 30, 4, (u32)x[7] << 16 | x[12]);
	o->reg_write(c, 30, 6, (u32)x[14] << 8 | x[5] | 0x110000);
	o->reg_write(c, 30, 15, (x[16] & 3) | 0x10010000 | (x[8] & 3) << 4);
	o->reg_write(c, 30, 1, 1);
	v = t33_le32(x + 20060);
	v = (v & 0xffff) | v << 16;
	o->reg_write(c, 5, 3, v);
	o->reg_write(c, 5, 4, v);
	o->reg_write(c, 5, 5, 0);
	o->reg_write(c, 5, 1, 1);
	o->irq_set(c, 5, o->ae_hist);
	o->irq_set(c, 4, o->ae_static);
	o->event_cb(c, 1, o->ae_calc);
	return 0;
}

#endif /* TX_ISP_T33_I9E_H */
