/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 defog (block 0xa000 registers 0xa004..0xa230, LUT window 0x320a0/0x320a4).
 *
 * Ported vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc):
 *   tisp_defog_max_filter, _img_filter9, _img_filter25, _interp_trans_k,
 *   _update_trans_k, _write_reg_para1, _write_reg_para2,
 *   _addr_write_reg_para3, _write_reg_para3, _update_reg_para2, _par,
 *   _get_data, _interrupt_static, _init, _deinit, _ev_update, _wdr_en,
 *   _dn_params_refresh, _param_array_get, _param_array_set,
 *   _api_set_strength, _api_get_strength, _strength_itp, _wake_up.
 *
 * IQ data: a 656 byte parameter section at tuning base + 0x8204 (ctx->sec).
 * Per channel the vendor keeps a 56 byte context (t32_data_e2130[ch]), a
 * 3948 byte parameter/LUT block (+16), a 220 byte register block (+20), an
 * 8 KiB DMA buffer (+4) and a vmalloc'ed grid (t32_data_e20e0[ch]).
 * Section fields used: +4 EV, +8 ascending thresholds (9 u32), +44 descending
 * thresholds, +191 threshold mode, +256 interpolated 5 bytes, +429 gain mode,
 * +430.. weights, +590 hold flag, +592/+594 width/height, +610.. 5 tables of 9.
 *
 * Register writes: d03a0 (shadow) selects between register addresses and the
 * shadow index numbers the vendor passes to tisp_reg_write; the port logs
 * the number the vendor passes (c->shadow = d03a0 == 1).
 *
 * NOT ported: tisp_defog_count_weight35abc, Tiziano_defog_soft, the DMA
 * cache sync, event push / event callback registration, irq registration,
 * the allocators and system_reg_read; they are reached through
 * struct t32_defog_ops.  isp_printf calls are dropped.  The vendor
 * dereferences NULL when an allocation fails; the port returns -1.
 * Fields the vendor leaves uninitialised after kmalloc (ctx ev2, strength)
 * are zeroed.  d03a0 values other than 0/1 are not modelled.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_defog_oracle).
 */
#ifndef TX_ISP_T32_DEFOG_H
#define TX_ISP_T32_DEFOG_H

#include "tx_isp_t32_tp.h"

#define T32_DEFOG_SEC_OFF	0x8204U
#define T32_DEFOG_SEC_BYTES	656U
#define T32_DEFOG_PAR_BYTES	3948U
#define T32_DEFOG_P2_BYTES	220U
#define T32_DEFOG_DMA_BYTES	8192U
#define T32_DEFOG_DMA_CHUNK	2048U
#define T32_DEFOG_GFP		0x2400c0U
#define T32_DEFOG_ID		0x0134da0fU
#define T32_DEFOG_GRID_N	180U

/* vendor register write: address, or the shadow index when d03a0 is set */
#define T32_DEFOG_W(c, w, n, a, i, v) \
	T32_FWR(w, *(n), (c)->shadow ? (u32)(i) : (u32)(a), (v))

/* 3948 byte parameter block; two vendor pointer words become fields */
struct t32_defog_par {
	u8 b[T32_DEFOG_PAR_BYTES];
	const u8 *tbl_a;	/* vendor +2176 -> sec + 251 */
	const u8 *tbl_b;	/* vendor +2180 -> sec + 256 */
};

/* 220 byte register block (used bytes 0..115) */
struct t32_defog_p2 {
	u32 w[17];		/* +0..+67 */
	const u8 *pa;		/* +68 -> sec + 263 */
	const u8 *pb;		/* +72 -> sec + 295 */
	const u8 *pc;		/* +76 -> sec + 328 */
	u8 flag[4];		/* +80..+83 */
	u8 tbl[32];		/* +84..+115 */
};

struct t32_defog_ctx {
	u32 nbuf;		/* +0, 4 */
	u8 *buf;		/* +4 DMA buffer */
	u32 buf_phys;		/* +8 */
	u8 *sec;		/* +12 IQ section */
	struct t32_defog_par *par;	/* +16 */
	struct t32_defog_p2 *p2;	/* +20 */
	u32 ev;			/* +24 */
	u32 ev2;		/* +28 */
	u32 height;		/* +32 */
	u32 width;		/* +36 */
	u32 mode;		/* +40 wdr mode */
	u32 irq_cnt;		/* +44 */
	u8 gain_mode;		/* +48 */
	u32 strength;		/* +52 */
};

struct t32_defog_ops {
	void (*count_weight)(u32 ch, u32 h, u32 w, u32 gain_mode, u8 *p80, u8 *p128,
			     u8 *p430, u8 *p462, u8 *p494, u8 *p526, u8 *p558);
	void (*soft)(struct t32_defog_par *par);
	u32 (*reg_read)(u32 reg);
	void (*dma_sync)(const u8 *buf, u32 len);
	void (*event_push)(u32 ch, const u32 *ev);
	void *(*vmalloc)(u32 size);
	void *(*kmalloc)(u32 size, u32 gfp);
	void (*vfree)(void *p);
	void (*kfree)(void *p);
	void (*irq_set)(u32 ch, u32 id);
	void (*event_set_cb)(u32 ch, u32 ev);
};

/* one channel slot: t32_data_e2130/e20e0/e211c[ch], d03a0, e20f0 */
struct t32_defog_chan {
	struct t32_defog_ctx *ctx;
	u8 *vbuf;
	u32 vsize;
	u8 shadow;		/* d03a0 == 1 */
	u32 irq_total;		/* t32_data_e212c */
	u32 saved[11];		/* t32_data_e20f0 */
	const struct t32_defog_ops *ops;
};

static inline u32 t32_defog_div(u32 n, u32 d)
{
	return d ? n / d : 0xffffffffU;
}

static inline void t32_defog_st32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static inline void t32_defog_cp(u8 *d, const u8 *s, u32 n)
{
	while (n--)
		*d++ = *s++;
}

static inline void t32_defog_set(u8 *d, u8 v, u32 n)
{
	while (n--)
		*d++ = v;
}

/* 3x3 maximum over the 18 x 10 grid, taps outside the grid dropped */
static inline void t32_defog_max_filter(const u8 *in, u8 *out)
{
	u8 tmp[T32_DEFOG_GRID_N];
	int r, c, dr, dc;

	for (r = 0; r < 10; r++)
		for (c = 0; c < 18; c++) {
			u8 m = 0;

			for (dr = (r ? -1 : 0); dr <= (r < 9 ? 1 : 0); dr++)
				for (dc = (c ? -1 : 0); dc <= (c < 17 ? 1 : 0); dc++)
					if (in[(r + dr) * 18 + c + dc] > m)
						m = in[(r + dr) * 18 + c + dc];
			tmp[r * 18 + c] = m;
		}
	t32_defog_cp(out, tmp, T32_DEFOG_GRID_N);
}

/* weighted (2 rad + 1)^2 mean, rounded, taps outside the grid dropped */
static inline void t32_defog_img_filter(const u8 *img, u8 *out, const u8 *k, int rad)
{
	u8 tmp[T32_DEFOG_GRID_N];
	int kw = 2 * rad + 1, ctr = rad * kw + rad;
	int r, c, dr, dc;

	for (r = 0; r < 10; r++)
		for (c = 0; c < 18; c++) {
			u32 sum = 0, wsum = 0;

			for (dr = -rad; dr <= rad; dr++) {
				if (r + dr < 0 || r + dr > 9)
					continue;
				for (dc = -rad; dc <= rad; dc++) {
					u32 wt;

					if (c + dc < 0 || c + dc > 17)
						continue;
					wt = k[ctr + dr * kw + dc];
					wsum += wt;
					sum += (u32)img[(r + dr) * 18 + c + dc] * wt;
				}
			}
			tmp[r * 18 + c] = (u8)t32_defog_div(sum + (wsum >> 1), wsum);
		}
	t32_defog_cp(out, tmp, T32_DEFOG_GRID_N);
}

static inline void t32_defog_img_filter9(const u8 *img, u8 *out, const u8 *k)
{
	t32_defog_img_filter(img, out, k, 1);
}

static inline void t32_defog_img_filter25(const u8 *img, u8 *out, const u8 *k)
{
	t32_defog_img_filter(img, out, k, 2);
}

/*
 * tisp_defog_interp_trans_k: 5 tables of 9 bytes interpolated at ev over 9
 * thresholds.  Section byte +191 != 0 selects the descending thresholds at
 * +44 and the second value ctx->ev2 instead of (ev, thr).
 */
static inline void t32_defog_interp_trans_k(const struct t32_defog_ctx *x, u32 ev, const u8 *thr,
					    const u8 *ta, const u8 *tb, const u8 *tc,
					    const u8 *td, const u8 *te, u8 *out)
{
	const u8 *tab[5];
	int desc = x->sec[191] != 0;
	int found = 0;
	u32 k = 0, i;

	tab[0] = ta;
	tab[1] = tb;
	tab[2] = tc;
	tab[3] = td;
	tab[4] = te;
	if (desc) {
		thr = x->sec + 44;
		ev = x->ev2;
	}
	for (k = 0; k < 9; k++) {
		u32 th = t32_front_le32(thr + 4 * k);

		if (desc ? th < ev : ev < th) {
			found = 1;
			break;
		}
	}
	if (!found || k == 0) {
		for (i = 0; i < 5; i++)
			out[i] = tab[i][found ? 0 : 8];
		return;
	}
	{
		u32 thp = t32_front_le32(thr + 4 * (k - 1));
		u32 thc = t32_front_le32(thr + 4 * k);
		u32 wp = desc ? ev - thc : thc - ev;
		u32 wc = desc ? thp - ev : ev - thp;
		u32 span = wp + wc;

		for (i = 0; i < 5; i++)
			out[i] = (u8)t32_defog_div((u32)tab[i][k - 1] * wp + (u32)tab[i][k] * wc +
						   (span >> 1), span);
	}
}

/* interpolation with the section's own tables, result at sec + 256 */
static inline void t32_defog_interp_sec(const struct t32_defog_ctx *x, u32 ev)
{
	u8 *s = x->sec;

	t32_defog_interp_trans_k(x, ev, s + 8, s + 610, s + 619, s + 628, s + 637, s + 646, s + 256);
}

/* tisp_defog_update_trans_k */
static inline void t32_defog_update_trans_k(struct t32_defog_chan *c, u32 ch)
{
	struct t32_defog_ctx *x = c->ctx;
	u8 *s = x->sec;
	u8 old[5];
	u32 i;

	(void)ch;
	t32_defog_cp(old, s + 256, 5);
	if (s[590])
		return;
	if (t32_front_le32(s + 4) == x->ev)
		return;
	t32_defog_st32(s + 4, x->ev);
	t32_defog_interp_sec(x, x->ev);
	for (i = 0; i < 5; i++)
		if (old[i] != s[256 + i])
			break;
	if (i == 5)
		return;
	/* vendor loop runs par + 3178 .. par + 3357 (180 bytes) */
	for (i = 0; i < T32_DEFOG_GRID_N; i++)
		x->par->b[3178 + i] &= 0xfU;
}

static inline u32 t32_defog_pack7(const u8 *q)
{
	return (u32)(q[1] & 0x7f) << 8 | (u32)(q[2] & 0x7f) << 16 | (q[0] & 0x7fU) |
	       (u32)(q[3] & 0x7f) << 24;
}

/* tisp_defog_write_reg_para1: 68 registers from the section */
static inline void t32_defog_write_reg_para1(struct t32_defog_chan *c, u32 ch,
					     struct t32_front_write *w, unsigned int *n)
{
	const u8 *s = c->ctx->sec;
	u32 i;

	(void)ch;
	for (i = 0; i < 12; i++)
		T32_DEFOG_W(c, w, n, 0xa050U + 4 * i, 2443U + i,
			    (u32)(t32_tp_le16(s + 82 + 4 * i) & 0x1fff) << 16 |
			    (t32_tp_le16(s + 80 + 4 * i) & 0x1fff));
	for (i = 0; i < 15; i++)
		T32_DEFOG_W(c, w, n, 0xa080U + 4 * i, 2455U + i,
			    (u32)(t32_tp_le16(s + 130 + 4 * i) & 0x3fff) << 16 |
			    (t32_tp_le16(s + 128 + 4 * i) & 0x3fff));
	T32_DEFOG_W(c, w, n, 0xa0bcU, 2470U, t32_tp_le16(s + 188) & 0x3fff);
	for (i = 0; i < 40; i++) {
		const u8 *q = s + 430 + 4 * i;

		T32_DEFOG_W(c, w, n, 0xa0c0U + 4 * i, 2471U + i,
			    (u32)(q[1] & 0x1f) << 8 | (u32)(q[2] & 0x1f) << 16 |
			    (q[0] & 0x1fU) | (u32)(q[3] & 0x1f) << 24);
	}
}

/* tisp_defog_write_reg_para2: 44 registers from the 220 byte block */
static inline void t32_defog_write_reg_para2(struct t32_defog_chan *c, u32 ch,
					     struct t32_front_write *w, unsigned int *n)
{
	const struct t32_defog_p2 *p = c->ctx->p2;
	u32 m;

	(void)ch;
	T32_DEFOG_W(c, w, n, 0xa048U, 2441U,
		    (u32)(p->flag[0] & 1) << 24 | (u32)(p->flag[2] & 0x3f) << 8 |
		    (u32)(p->flag[1] & 0x3f) << 16 | (p->flag[3] & 0x3fU));
	for (m = 0; m < 41; m++) {
		u32 v;

		if (m < 8)
			v = t32_defog_pack7(p->pa + 4 * m);
		else if (m < 16)
			v = t32_defog_pack7(p->pb + 4 * (m - 8));
		else if (m < 24)
			v = (p->w[2 * (m - 16) + 1] & 0x3fffU) << 16 | (p->w[2 * (m - 16)] & 0x3fffU);
		else if (m == 24)
			v = p->w[16] & 0x3fffU;
		else if (m < 33)
			v = t32_defog_pack7(p->pc + 4 * (m - 25));
		else
			v = t32_defog_pack7(p->tbl + 4 * (m - 33));
		T32_DEFOG_W(c, w, n, 0xa160U + 4 * m, 2511U + m, v);
	}
	T32_DEFOG_W(c, w, n, 0xa220U, 2559U, c->ctx->sec[591] & 0x1fU);
	T32_DEFOG_W(c, w, n, 0xa010U, 2437U, 15);
}

/* LUT word j of the 4 x 180 byte tables in the parameter block */
static inline u32 t32_defog_lut_word(const u8 *par, u32 j)
{
	return (u32)par[2458 + j] | (u32)par[2638 + j] << 8 | (u32)par[2818 + j] << 16 |
	       (u32)par[2998 + j] << 24;
}

/* tisp_defog_addr_write_reg_para3 (the d03a0 == 1 variant) */
static inline void t32_defog_addr_write_reg_para3(struct t32_defog_chan *c, u32 ch,
						  struct t32_front_write *w, unsigned int *n)
{
	const u8 *par = c->ctx->par->b;
	u32 s3 = c->shadow ? 2564U : 0x320a0U;
	u32 s4 = c->shadow ? 2562U : 0xa228U;
	u32 j;

	(void)ch;
	T32_FWR(w, *n, s4, 0);
	T32_FWR(w, *n, s3, 257);
	for (j = 0; j < T32_DEFOG_GRID_N; j++)
		T32_FWR(w, *n, c->shadow ? 2566U + j : 0x320a4U, t32_defog_lut_word(par, j));
	T32_FWR(w, *n, s3 + 183, 0xb30102U);
	T32_FWR(w, *n, s4 + 187, 1);
}

/* tisp_defog_write_reg_para3 (direct system register writes) */
static inline void t32_defog_write_reg_para3(struct t32_defog_chan *c, u32 ch,
					     struct t32_front_write *w, unsigned int *n)
{
	const u8 *par = c->ctx->par->b;
	u32 j;

	T32_FWR(w, *n, c->shadow ? 2562U : 0xa228U, 0);
	T32_FWR(w, *n, c->shadow ? 2564U : 0x320a0U, 257);
	for (j = 0; j < T32_DEFOG_GRID_N; j++)
		T32_FWR(w, *n, c->shadow ? 2566U : 0x320a4U, t32_defog_lut_word(par, j));
	T32_FWR(w, *n, c->shadow ? 2564U : 0x320a0U, 0xb30102U);
	T32_FWR(w, *n, ch == 0 ? 0xa228U : 2562U, 1);
}

static inline void t32_defog_write_lut(struct t32_defog_chan *c, u32 ch,
				       struct t32_front_write *w, unsigned int *n)
{
	if (c->shadow)
		t32_defog_addr_write_reg_para3(c, ch, w, n);
	else
		t32_defog_write_reg_para3(c, ch, w, n);
}

/* tisp_defog_update_reg_para2: section -> 220 byte block and parameter block */
static inline void t32_defog_update_reg_para2(struct t32_defog_chan *c, u32 ch)
{
	struct t32_defog_ctx *x = c->ctx;
	const u8 *s = x->sec;
	struct t32_defog_par *par = x->par;
	struct t32_defog_p2 *p = x->p2;
	u32 t3 = s[393], t4 = s[411], t5 = s[360];
	u32 i;

	(void)ch;
	for (i = 0; i < 17; i++) {
		u32 a = s[394 + i], b = s[412 + i];

		if ((int)t3 < 65)
			a = (u32)((int)(a * t3) >> 6);
		else
			a = 127U - (u32)((int)((127U - a) * (128U - t3)) >> 6);
		if ((int)t4 < 65)
			b = (u32)((int)(b * t4) >> 6);
		else
			b = 127U - (u32)((int)((127U - b) * (128U - t4)) >> 6);
		p->w[i] = (b & 0x7f) << 7 | (a & 0x7f);
	}
	for (i = 0; i < 32; i++)
		p->tbl[i] = (u8)((((u32)s[361 + i] * t5) >> 7) & 0x7f);
	p->flag[3] = s[327];
	p->flag[2] = s[262];
	p->flag[1] = s[261];
	p->flag[0] = s[190];
	p->pa = s + 263;
	p->pb = s + 295;
	p->pc = s + 328;
	par->b[2415] = s[190];
	par->b[2416] = s[192];
	par->b[2417] = s[193];
	par->b[2418] = s[194];
	par->b[2420] = s[195];
	par->b[2421] = s[196];
	par->b[2422] = s[197];
	par->b[2423] = s[198];
	par->b[2424] = s[199];
	t32_defog_cp(par->b + 2381, s + 217, 9);
	t32_defog_cp(par->b + 2390, s + 226, 25);
	t32_defog_cp(par->b + 2364, s + 200, 17);
	par->tbl_a = s + 251;
	par->tbl_b = s + 256;
}

/* tisp_defog_par: per frame work */
static inline int t32_defog_par(struct t32_defog_chan *c, u32 ch,
				struct t32_front_write *w, unsigned int *n)
{
	struct t32_defog_ctx *x = c->ctx;

	t32_defog_update_trans_k(c, ch);
	if (x->irq_cnt < 2U)
		x->par->b[2420] = 0;
	else
		x->par->b[2420] = x->sec[195];
	c->ops->soft(x->par);
	t32_defog_write_lut(c, ch, w, n);
	return 0;
}

/* tisp_defog_get_data: three 21 bit values per 64 bit word of the DMA block */
static inline void t32_defog_get_data(struct t32_defog_par *par, const u8 *src)
{
	u32 i;

	src += 8;
	for (i = 0; i < T32_DEFOG_GRID_N; i++, src += 8) {
		u32 w0 = t32_front_le32(src), w1 = t32_front_le32(src + 4);

		t32_defog_st32(par->b + 1440 + 4 * i, w0 & 0x1fffffU);
		t32_defog_st32(par->b + 720 + 4 * i, (w1 << 11 | w0 >> 21) & 0x1fffffU);
		t32_defog_st32(par->b + 4 * i, (w1 >> 10) & 0x1fffffU);
	}
}

/* tisp_defog_interrupt_static */
static inline int t32_defog_interrupt_static(struct t32_defog_chan *c, u32 ch)
{
	struct t32_defog_ctx *x = c->ctx;
	const struct t32_defog_ops *o = c->ops;
	u32 st = o->reg_read(0xa230U);
	u32 ev[3] = { 0, 0, 3 };
	u32 k;

	c->irq_total++;
	for (k = 0; k < 4; k++) {
		u32 off = T32_DEFOG_DMA_CHUNK * k;

		if (st == x->buf_phys + off) {
			o->dma_sync(x->buf + off, T32_DEFOG_DMA_CHUNK);
			t32_defog_get_data(x->par, x->buf + off);
		}
	}
	x->irq_cnt = (x->irq_cnt >> 30) == 1 ? 255U : x->irq_cnt + 1;
	o->event_push(ch, ev);
	return 1;
}

/* weights / grid statistics from the section (vendor helper behind ops) */
static inline void t32_defog_count_weight(struct t32_defog_chan *c, u32 ch)
{
	struct t32_defog_ctx *x = c->ctx;
	u8 *s = x->sec;

	c->ops->count_weight(ch & 0xffU, x->height, x->width, s[429], s + 80, s + 128, s + 430,
			     s + 462, s + 494, s + 526, s + 558);
}

/* hold flag clear: the section EV follows ctx->ev; then interpolate */
static inline void t32_defog_trans_refresh(struct t32_defog_ctx *x)
{
	if (!x->sec[590])
		t32_defog_st32(x->sec + 4, x->ev);
	t32_defog_interp_sec(x, t32_front_le32(x->sec + 4));
}

/* common end of init / wdr_en / dn_params_refresh */
static inline void t32_defog_refresh_tail(struct t32_defog_chan *c, u32 ch,
					  struct t32_front_write *w, unsigned int *n)
{
	u8 *b = c->ctx->par->b;

	t32_defog_update_reg_para2(c, ch);
	t32_defog_write_reg_para2(c, ch, w, n);
	t32_defog_set(b + 2458, 128, 180);
	t32_defog_set(b + 2638, 128, 180);
	t32_defog_set(b + 2818, 128, 180);
	t32_defog_set(b + 2998, 0, 180);
	t32_defog_write_lut(c, ch, w, n);
}

/* tisp_defog_init; size[0] = width, size[1] = height */
static inline int t32_defog_init(struct t32_defog_chan *c, u32 ch, const u32 *size, u8 *tuning_base,
				 struct t32_front_write *w, unsigned int *n)
{
	const struct t32_defog_ops *o = c->ops;
	int wd = (int)size[0], h = (int)size[1];
	int h2 = h >> 1, w2 = wd >> 1;
	u32 hh = (u32)(h2 / 10) << 1, ww = (u32)(w2 / 18) << 1;
	u32 ra = (u32)(h2 % 10), rb = (u32)(w2 % 18);
	u32 ga = hh + 2, gb = ww + 2, area = ga * gb, cls, phys;
	struct t32_defog_ctx *x;
	u8 *s, *b;

	c->vsize = (u32)((h + 5) / 10) * (u32)((wd + 9) / 18) * 13U;
	c->vbuf = o->vmalloc(c->vsize);
	if (c->vbuf)
		t32_defog_set(c->vbuf, 0, c->vsize);
	x = o->kmalloc(sizeof(*x), T32_DEFOG_GFP);
	c->ctx = x;
	if (!x)
		return -1;
	t32_defog_set((u8 *)x, 0, sizeof(*x));
	x->buf = o->kmalloc(T32_DEFOG_DMA_BYTES, T32_DEFOG_GFP);
	if (!x->buf)
		return -1;
	phys = (u32)(unsigned long)x->buf + 0x80000000U;
	T32_DEFOG_W(c, w, n, 0xa210U, 2555U, phys);
	T32_DEFOG_W(c, w, n, 0xa214U, 2556U, phys + 2048);
	T32_DEFOG_W(c, w, n, 0xa218U, 2557U, phys + 4096);
	T32_DEFOG_W(c, w, n, 0xa21cU, 2558U, phys + 6144);
	T32_DEFOG_W(c, w, n, 0xa20cU, 2554U, 3);
	x->nbuf = 4;
	x->buf_phys = phys;
	x->sec = tuning_base + T32_DEFOG_SEC_OFF;
	x->ev = 99;
	x->par = o->kmalloc(sizeof(*x->par), T32_DEFOG_GFP);
	if (!x->par)
		return -1;
	t32_defog_set(x->par->b, 0, T32_DEFOG_PAR_BYTES);
	x->par->tbl_a = NULL;
	x->par->tbl_b = NULL;
	x->p2 = o->kmalloc(sizeof(*x->p2), T32_DEFOG_GFP);
	if (!x->p2)
		return -1;
	t32_defog_set((u8 *)x->p2, 0, sizeof(*x->p2));
	x->irq_cnt = 0;
	x->height = (u32)h;
	x->width = (u32)wd;
	x->mode = 0;
	b = x->par->b;

	/* grid reciprocals (Q31) and remainders */
	t32_defog_st32(b + 2160, t32_defog_div(0x80000000U + (area >> 1), area));
	t32_defog_st32(b + 2164, t32_defog_div(0x80000000U + ((ww * ga) >> 1), ww * ga));
	t32_defog_st32(b + 2168, t32_defog_div(0x80000000U + ((hh * gb) >> 1), hh * gb));
	t32_defog_st32(b + 2172, t32_defog_div(0x80000000U + ((hh * ww) >> 1), hh * ww));
	if (area < 8193U)
		cls = 0;
	else if (area < 16385U)
		cls = 1;
	else
		cls = area < 0x8001U ? 2 : 3;
	b[2455] = (u8)ra;
	b[2456] = (u8)rb;
	b[2457] = (u8)cls;
	T32_DEFOG_W(c, w, n, 0xa040U, 2439U, (hh & 0xff) << 16 | (ww & 0xff));
	T32_DEFOG_W(c, w, n, 0xa044U, 2440U, (ra & 0x1f) << 16 | (rb & 0x1f));
	T32_DEFOG_W(c, w, n, 0xa04cU, 2442U, 0);
	T32_DEFOG_W(c, w, n, 0xa204U, 2552U, 0);
	T32_DEFOG_W(c, w, n, 0xa208U, 2553U, cls);
	T32_DEFOG_W(c, w, n, 0xa20cU, 2554U, 2);
	T32_DEFOG_W(c, w, n, 0xa010U, 2437U, 3);

	s = x->sec;
	x->gain_mode = s[429];
	if (s[429] != 128 || (u32)wd != t32_tp_le16(s + 592) || (u32)h != t32_tp_le16(s + 594)) {
		t32_defog_count_weight(c, ch);
		s[592] = (u8)wd;
		s[593] = (u8)(wd >> 8);
		s[594] = (u8)h;
		s[595] = (u8)(h >> 8);
	}
	t32_defog_write_reg_para1(c, ch, w, n);
	t32_defog_trans_refresh(x);
	t32_defog_refresh_tail(c, ch, w, n);
	o->irq_set(ch & 0xffU, 10);
	o->event_set_cb(ch, 3);
	T32_FWR(w, *n, 0xa004U, T32_DEFOG_ID);
	return 0;
}

/* tisp_defog_deinit */
static inline int t32_defog_deinit(struct t32_defog_chan *c)
{
	struct t32_defog_ctx *x = c->ctx;
	const struct t32_defog_ops *o = c->ops;

	if (x) {
		if (x->buf) {
			o->kfree(x->buf);
			x->buf = NULL;
			x->buf_phys = 0;
		}
		if (x->par) {
			o->kfree(x->par);
			x->par = NULL;
		}
		if (x->p2) {
			o->kfree(x->p2);
			x->p2 = NULL;
		}
		o->kfree(c->ctx);
		c->ctx = NULL;
	}
	if (c->vbuf) {
		o->vfree(c->vbuf);
		c->vbuf = NULL;
	}
	return 0;
}

/* tisp_defog_ev_update; (a4, a5) are the vendor stack arguments */
static inline int t32_defog_ev_update(struct t32_defog_chan *c, u32 ch, u32 lo, u32 hi,
				      u32 a4, u32 a5)
{
	struct t32_defog_ctx *x = c->ctx;
	u32 ev = hi << 22 | lo >> 10;

	if (ev != x->ev && x->sec[191] == 0)
		t32_defog_update_trans_k(c, ch);
	x->ev = ev;
	if ((x->ev2 != a4 || a5 != 0) && x->sec[191] == 1) {
		x->ev2 = a4;
		t32_defog_update_trans_k(c, ch);
	}
	return 0;
}

/* tisp_defog_wdr_en */
static inline int t32_defog_wdr_en(struct t32_defog_chan *c, u32 ch, u32 mode,
				   struct t32_front_write *w, unsigned int *n)
{
	struct t32_defog_ctx *x = c->ctx;
	u8 *s = x->sec;

	x->mode = mode;
	t32_defog_set(x->par->b + 3178, 0, 256);
	if (x->height != t32_tp_le16(s + 594) || x->width != t32_tp_le16(s + 592))
		t32_defog_count_weight(c, ch);
	x->gain_mode = s[429];
	t32_defog_write_reg_para1(c, ch, w, n);
	t32_defog_trans_refresh(x);
	t32_defog_refresh_tail(c, ch, w, n);
	return 0;
}

/* tisp_defog_dn_params_refresh */
static inline int t32_defog_dn_params_refresh(struct t32_defog_chan *c, u32 ch, u32 value,
					      struct t32_front_write *w, unsigned int *n)
{
	struct t32_defog_ctx *x = c->ctx;
	u8 *s = x->sec;

	if (x->height != t32_tp_le16(s + 594) || x->width != t32_tp_le16(s + 592))
		t32_defog_count_weight(c, ch);
	x->gain_mode = s[429];
	t32_defog_write_reg_para1(c, ch, w, n);
	t32_defog_trans_refresh(x);
	t32_defog_refresh_tail(c, ch, w, n);
	x->strength = value;
	return 0;
}

/* tisp_defog_param_array_get: the section is copied out whole */
static inline int t32_defog_param_array_get(const struct t32_defog_chan *c, u8 *buf, u32 *len)
{
	t32_defog_cp(buf, c->ctx->sec, T32_DEFOG_SEC_BYTES);
	*len = T32_DEFOG_SEC_BYTES;
	return 0;
}

/* tisp_defog_param_array_set */
static inline int t32_defog_param_array_set(struct t32_defog_chan *c, u32 ch, const u8 *buf,
					    struct t32_front_write *w, unsigned int *n)
{
	struct t32_defog_ctx *x = c->ctx;
	u8 *s = x->sec;

	t32_defog_cp(s, buf, T32_DEFOG_SEC_BYTES);
	if (x->gain_mode != s[429]) {
		x->gain_mode = s[429];
		s[592] = (u8)x->width;
		s[593] = (u8)(x->width >> 8);
		s[594] = (u8)x->height;
		s[595] = (u8)(x->height >> 8);
		t32_defog_count_weight(c, ch);
		t32_defog_write_reg_para1(c, ch, w, n);
	}
	t32_defog_trans_refresh(x);
	t32_defog_set(x->par->b + 3178, 0, 256);
	t32_defog_update_reg_para2(c, ch);
	t32_defog_write_reg_para2(c, ch, w, n);
	return 0;
}

/* the vendor API stubs do nothing */
static inline int t32_defog_api_set_strength(void)
{
	return 0;
}

static inline int t32_defog_api_get_strength(void)
{
	return 0;
}

/* tisp_defog_strength_itp: blend 9 table bytes towards 0 / 255 */
static inline void t32_defog_strength_itp(u32 strength, const u8 *in, u8 *out)
{
	u32 a = strength & 0xff, i;

	if (a < 128) {
		for (i = 0; i < 9; i++)
			out[i] = (u8)((int)(in[i] * a) >> 7);
	} else if (a == 128) {
		t32_defog_cp(out, in, 9);
	} else {
		u32 b = ~a & 0xff;

		for (i = 0; i < 9; i++)
			out[i] = (u8)~(u32)((int)(((~(u32)in[i]) & 0xff) * b) >> 7);
	}
}

/* tisp_defog_wake_up: rewrite the block from the saved register values */
static inline void t32_defog_wake_up(struct t32_defog_chan *c, u32 ch,
				     struct t32_front_write *w, unsigned int *n)
{
	static const u16 addr[11] = { 0xa210, 0xa214, 0xa218, 0xa21c, 0xa20c, 0xa040,
				      0xa044, 0xa04c, 0xa204, 0xa208, 0xa20c };
	static const u16 idx[11] = { 2555, 2556, 2557, 2558, 2554, 2439,
				     2440, 2442, 2552, 2553, 2554 };
	u32 i;

	t32_defog_write_reg_para1(c, ch, w, n);
	t32_defog_write_reg_para2(c, ch, w, n);
	t32_defog_write_reg_para3(c, ch, w, n);
	for (i = 0; i < 11; i++)
		T32_DEFOG_W(c, w, n, addr[i], idx[i], c->saved[i]);
}

#endif /* TX_ISP_T32_DEFOG_H */
