/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) MSCA OSD and mask outputs: readable C of the vendor module
 * tisp_msca_t33_api_{set,get}_osd_out_cfg (0xe4ecc / 0xe4e40),
 * _{set,get}_osd_out_attribute_cfg (0xe512c / 0xe51a4), _set_osd_reg (0xe48e4),
 * _{set,get}_mask_out_cfg (0xe553c / 0xe5498) and _set_mask_reg (0xe521c)
 * (list mode: every register is a word of MSCA list 23, tisp_reg_write_t33(ch,
 * 0x17, slot, v) = list word slot + 1; the direct-register branch of the vendor
 * code is dead on the T33).  A write sets tispMscaRefresh: list 23 runs at the
 * next MSCA frame end.
 *
 * Differences to the vendor code (all out-of-range inputs, which the vendor
 * indexes past its buffers): an OSD window / mask number > 3 or a mask
 * channel > 2 is refused without a store when disabling; GET of an OSD window
 * returns the stored u32 (word +12) and not the address of the vendor's
 * attribute word, which would leak a kernel pointer.
 */
#ifndef TX_ISP_T33_OSD_H
#define TX_ISP_T33_OSD_H

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#endif

#define T33_OSD_WIN		4
#define T33_MASK_CH		3
#define T33_MASK_WIN		4

struct t33_osd_win {
	u8 en;
	u16 x, y, w, h;
	u32 word;		/* +12 of the block: bitmap / colour word */
	u16 half;		/* +16 */
};

struct t33_mask_win {
	u8 en;
	u16 a, b, c, d;		/* block +6, +4, +8, +10 */
	u32 color;		/* r << 16 | g << 8 | b */
};

/* the OSD / mask part of the vendor's stMscaAttr[ch] */
struct t33_osd {
	u16 in_w, in_h;		/* attr +0xe / +0x10: the image the OSD may cover */
	struct t33_osd_win win[T33_OSD_WIN];
	u8 a368, a369, a36a, a36c, a36e;
	u8 grp[4][4];		/* attr +0x370 + 4 g + k, [g][k] */
	struct t33_mask_win mask[T33_MASK_CH][T33_MASK_WIN];
	u8 info;		/* MscaOsdInfo: alignment fixes */
};

struct t33_osd_ops {
	void (*slot)(u32 ch, u32 slot, u32 v);	/* tisp_reg_write_t33(ch, 0x17, slot, v) */
	void (*refresh)(u32 ch);		/* tispMscaRefresh = 1 */
	void (*msg)(u32 level, const char *fmt, ...);
};

static inline u16 t33_osd_u16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static inline u32 t33_osd_u32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static inline void t33_osd_p16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static inline void t33_osd_p32(u8 *p, u32 v) { t33_osd_p16(p, (u16)v); t33_osd_p16(p + 2, (u16)(v >> 16)); }

/* tisp_msca_t33_api_set_osd_reg */
static inline void t33_osd_set_reg(struct t33_osd *o, const struct t33_osd_ops *op, u32 ch)
{
	u32 i, en = 0, b;

	for (i = 0; i < T33_OSD_WIN; i++) {
		struct t33_osd_win *w = &o->win[i];

		if (!(w->en & 1))
			continue;
		if ((w->x & 1) || (w->y & 1)) {
			w->x &= 0xfffe;
			w->y &= 0xfffe;
			o->info++;
		}
		if (o->a36c == 2) {
			if ((w->w & 3) || (w->h & 1)) {
				w->w &= 0xfffc;
				w->h &= 0xfffe;
				o->info++;
			}
		} else if ((w->w & 1) || (w->h & 1)) {
			w->w &= 0xfffe;
			w->h &= 0xfffe;
			o->info++;
		}
		b = 0xb4 + 4 * i;
		op->slot(ch, b - 3, (u32)w->x << 16 | w->y);
		op->slot(ch, b - 2, (u32)w->w << 16 | w->h);
		op->slot(ch, b - 1, w->word);
		op->slot(ch, b, w->half);
		en |= (u32)w->en << i;
	}
	op->slot(ch, 5, en);
	op->slot(ch, 0xaa, (u32)o->a369 << 4);
	op->slot(ch, 0xa6, 0x381da506);
	op->slot(ch, 0xa7, 0xd13b5e02);
	op->slot(ch, 0xa8, 0xf6e70064);
	op->slot(ch, 0xa9, 0x8010);
	op->slot(ch, 0xab, (u32)o->a36e << 4 | o->a36c | 0x10000 | (u32)o->a36a << 8);
	for (i = 0; i < 4; i++)
		op->slot(ch, 0xad + i, (u32)o->grp[0][i] | (u32)o->grp[1][i] << 8 |
			 (u32)o->grp[2][i] << 16 | (u32)o->grp[3][i] << 24);
	op->refresh(ch);
}

/* tisp_msca_t33_api_set_osd_out_cfg: block { u8 pinum, u8 en, u16 left, u16 top, u16 w, u16 h, u32 @12, u16 @16 } */
static inline void t33_osd_set_out(struct t33_osd *o, const struct t33_osd_ops *op, u32 ch, const u8 *p)
{
	u32 n = p[0];
	struct t33_osd_win *w;

	if (p[1] == 1) {
		u32 left = t33_osd_u16(p + 2), top = t33_osd_u16(p + 4), ww = t33_osd_u16(p + 6), hh = t33_osd_u16(p + 8);

		if (n > 3) {
			op->msg(1, "[ISP_OSD] The maximum number of OSD stacks supported is only 4, and pinum is %d !\n", n);
			return;
		}
		if (o->in_w < left + ww || o->in_h < top + hh) {
			op->msg(1, "----->> pinum:%d, left:%d, top:%d, width:%d, height:%d, crop_w:%d. crop_h:%d <<-----\n",
				n, left, top, ww, hh, o->in_w, o->in_h);
			op->msg(2, "The image overflows the OSD boundary !!!");
			return;
		}
		w = &o->win[n];
		w->en = 1;
		w->x = (u16)left;
		w->y = (u16)top;
		w->w = (u16)ww;
		w->h = (u16)hh;
		w->word = t33_osd_u32(p + 12);
		w->half = t33_osd_u16(p + 16);
		if (o->in_w < (u32)w->w + w->x)
			w->w = (u16)(o->in_w - w->x);
		if (o->in_h < (u32)w->h + w->y)
			w->h = (u16)(o->in_h - w->y);
	} else {
		if (n > 3)
			return;
		w = &o->win[n];
		memset(w, 0, sizeof(*w));
		w->en = p[1];
	}
	t33_osd_set_reg(o, op, ch);
}

/* tisp_msca_t33_api_get_osd_out_cfg: the block's other bytes stay as the caller had them */
static inline void t33_osd_get_out(const struct t33_osd *o, u8 *p)
{
	const struct t33_osd_win *w;

	if (p[0] > 3)
		return;
	w = &o->win[p[0]];
	if (w->en == 1) {
		p[1] = 1;
		t33_osd_p16(p + 2, w->x);
		t33_osd_p16(p + 4, w->y);
		t33_osd_p16(p + 6, w->w);
		t33_osd_p16(p + 8, w->h);
		t33_osd_p32(p + 12, w->word);
		t33_osd_p16(p + 16, w->half);
		return;
	}
	memset(p + 1, 0, 9);
	memset(p + 12, 0, 6);
}

/*
 * tisp_msca_t33_api_{set,get}_osd_out_attribute_cfg: block { u32 a36c, u32 a36a, u32 a36e,
 * u8 g[4][4] at 12 (byte k of group g at 12 + 4 g + k), u8 a368 at 28 }.  Set stores only,
 * the registers follow with the next window write.
 */
static inline void t33_osd_set_attr(struct t33_osd *o, const u8 *p)
{
	u32 g, k;

	o->a36c = p[0];
	o->a368 = p[28];
	o->a36a = p[4];
	o->a36e = p[8];
	for (g = 0; g < 4; g++)
		for (k = 0; k < 4; k++)
			o->grp[g][k] = p[12 + 4 * g + k];
}

static inline void t33_osd_get_attr(const struct t33_osd *o, u8 *p)
{
	u32 g, k;

	t33_osd_p32(p, o->a36c);
	p[28] = o->a368;
	t33_osd_p32(p + 4, o->a36a);
	t33_osd_p32(p + 8, o->a36e);
	for (g = 0; g < 4; g++)
		for (k = 0; k < 4; k++)
			p[12 + 4 * g + k] = o->grp[g][k];
}

/* tisp_msca_t33_api_set_mask_reg */
static inline void t33_mask_set_reg(struct t33_osd *o, const struct t33_osd_ops *op, u32 ch)
{
	u32 k, j, slot, en = 0;

	for (k = 0; k < T33_MASK_CH; k++) {
		u32 any = 0;

		for (j = 0; j < T33_MASK_WIN; j++) {
			const struct t33_mask_win *m = &o->mask[k][j];
			u32 v = 0;

			slot = k * 0x10 + 0x75 + 4 * j;
			if (m->en & 1) {
				op->slot(ch, slot - 1, (u32)m->a << 16 | m->b);
				op->slot(ch, slot + 1, m->color);
				v = (u32)m->c << 16 | m->d;
			}
			op->slot(ch, slot, v);
			any |= m->en;
		}
		en |= (any & 0xff) << k;
		op->slot(ch, 4, en);
	}
	op->refresh(ch);
}

/* tisp_msca_t33_api_set_mask_out_cfg: block { u8 ch, u8 idx, u8 en, -, u16 @4, @6, @8, @10, u8 r @19, g @20, b @21 } */
static inline void t33_mask_set_out(struct t33_osd *o, const struct t33_osd_ops *op, u32 ch, const u8 *p)
{
	struct t33_mask_win *m;

	if (p[0] >= T33_MASK_CH)
		return;
	if (p[2] == 1) {
		if (p[1] > 3) {
			op->msg(1, "[ISP_MASK] The maximum number of MASK stacks supported is only 4, and pinum is %d !\n", p[1]);
			return;
		}
		m = &o->mask[p[0]][p[1]];
		m->en = 1;
		m->b = t33_osd_u16(p + 4);
		m->a = t33_osd_u16(p + 6);
		m->c = t33_osd_u16(p + 8);
		m->d = t33_osd_u16(p + 10);
		m->color = (u32)p[19] << 16 | (u32)p[20] << 8 | p[21];
	} else {
		if (p[1] > 3)
			return;
		o->mask[p[0]][p[1]].en = 0;
	}
	t33_mask_set_reg(o, op, ch);
}

static inline void t33_mask_get_out(const struct t33_osd *o, u8 *p)
{
	const struct t33_mask_win *m;

	if (p[0] >= T33_MASK_CH || p[1] > 3)
		return;
	m = &o->mask[p[0]][p[1]];
	if (m->en == 1) {
		p[2] = 1;
		t33_osd_p16(p + 4, m->b);
		t33_osd_p16(p + 6, m->a);
		t33_osd_p16(p + 8, m->c);
		t33_osd_p16(p + 10, m->d);
		p[19] = (u8)(m->color >> 16);
		p[20] = (u8)(m->color >> 8);
		p[21] = (u8)m->color;
		return;
	}
	p[2] = 0;
	memset(p + 4, 0, 8);
	memset(p + 19, 0, 3);
}

/* ------------------------------------------------------------------ */
/* draw block (line / rectangle / mask lines), tuning id 0xa0         */
/* ------------------------------------------------------------------ */
/*
 * tisp_msca_t33_api_{set,get}_line_out_cfg (0xe3b30 / 0xe453c), tisp_msca_t33_set_line_reg
 * (0xe0d7c) and find_k_n (table lookup_table, 63 x { slope * 1e6, k, n }).  List mode
 * slots of MSCA list 23: 0xa4 = 1, 0xc7 .. 0xca = the colour list, 9 = msk size, 0x12f = 0x101,
 * 3 words per window from 0x131, 0x192 = 0x10102.  Vendor behaviours kept: the unused-window fill
 * starts at the number of ENABLED windows (not at the first free window number) and writes
 * slots n, n + 1, n + 1 (n + 2 is never cleared); the colour list is global (4 entries, shared
 * by all channels and windows).  Window numbers >= 32 are refused (the vendor indexes past the
 * attribute block).
 */
#define T33_DRAW_WIN	32

struct t33_line_win {
	u32 type;
	u8 col[3];
	u16 raw[4];		/* type 2: the caller's end points */
	u16 x, y, w, h;
	u8 en, cidx, b3aa, width, k, b3ad, b3ae, b3af;
};

struct t33_draw {
	struct t33_line_win win[T33_DRAW_WIN];
	u8 msk;
};

struct t33_colors {
	u32 c[4];
	u32 n;
};

static const u32 t33_kn_tab[63][3] = {
	{ 24938, 0xcc, 0xf3 }, { 49908, 0xcc, 0xf4 }, { 74940, 0x99, 0xf5 },
	{ 100065, 0xcd, 0xf5 }, { 125316, 0x80, 0xf6 }, { 150726, 0x9a, 0xf6 },
	{ 176327, 0xb5, 0xf6 }, { 202154, 0xcf, 0xf6 }, { 228243, 0xea, 0xf6 },
	{ 254631, 0x82, 0xf7 }, { 281356, 0x90, 0xf7 }, { 308459, 0x9e, 0xf7 },
	{ 335982, 0xac, 0xf7 }, { 363970, 0xba, 0xf7 }, { 392471, 0xc9, 0xf7 },
	{ 421535, 0xd8, 0xf7 }, { 451217, 0xe7, 0xf7 }, { 481575, 0xf7, 0xf7 },
	{ 512670, 0x83, 0xf8 }, { 544571, 0x8b, 0xf8 }, { 577350, 0x94, 0xf8 },
	{ 611087, 0x9c, 0xf8 }, { 645868, 0xa5, 0xf8 }, { 681788, 0xaf, 0xf8 },
	{ 718951, 0xb8, 0xf8 }, { 757471, 0xc2, 0xf8 }, { 797473, 0xcc, 0xf8 },
	{ 839100, 0xd7, 0xf8 }, { 882505, 0xe2, 0xf8 }, { 927864, 0xee, 0xf8 },
	{ 975372, 0xfa, 0xf8 }, { 1000000, 0x01, 0x00 }, { 1025249, 0x83, 0xf9 },
	{ 1077744, 0x8a, 0xf9 }, { 1133138, 0x91, 0xf9 }, { 1191754, 0x99, 0xf9 },
	{ 1253960, 0xa1, 0xf9 }, { 1320183, 0xa9, 0xf9 }, { 1390915, 0xb2, 0xf9 },
	{ 1466731, 0xbc, 0xf9 }, { 1548303, 0xc6, 0xf9 }, { 1636427, 0xd1, 0xf9 },
	{ 1732051, 0xde, 0xf9 }, { 1836308, 0xeb, 0xf9 }, { 1950572, 0xfa, 0xf9 },
	{ 2076521, 0x85, 0xfa }, { 2216228, 0x8e, 0xfa }, { 2372280, 0x98, 0xfa },
	{ 2547958, 0xa3, 0xfa }, { 2747477, 0xb0, 0xfa }, { 2976349, 0xbe, 0xfa },
	{ 3241920, 0xcf, 0xfa }, { 3554211, 0xe3, 0xfa }, { 3927247, 0xfb, 0xfa },
	{ 4381286, 0x8c, 0xfb }, { 4946715, 0x9e, 0xfb }, { 5671282, 0xb5, 0xfb },
	{ 6634566, 0xd4, 0xfb }, { 7979811, 0xff, 0xfb }, { 9993495, 0xa0, 0xfc },
	{ 13344073, 0xd6, 0xfc }, { 20036898, 0xa0, 0xfd }, { 40098734, 0xa0, 0xfe },
};

/* find_k_n: binary search of slope * 1e6; *k, *n = the nearest entry (or the limits) */
static inline void t33_find_k_n(u32 v, u8 *k, u8 *n)
{
	int lo = 0, hi = 0x3e, mid = 0x3e;
	u32 d1, d2;

	for (;;) {
		mid = mid / 2 + lo;
		if (t33_kn_tab[mid][0] < v) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
			if (t33_kn_tab[mid][0] <= v) {
				*k = (u8)t33_kn_tab[mid][1];
				*n = (u8)t33_kn_tab[mid][2];
				return;
			}
		}
		if (hi < lo)
			break;
		mid = hi - lo;
	}
	if (hi < 0) {
		*k = 0xcc;
		*n = 0xf3;
	} else if (lo < 0x3f) {
		d1 = t33_kn_tab[lo][0] - v;
		d2 = t33_kn_tab[hi][0] - v;
		d1 = (int)d1 < 0 ? -d1 : d1;
		d2 = (int)d2 < 0 ? -d2 : d2;
		if (d2 < d1) {
			*k = (u8)t33_kn_tab[hi][1];
			*n = (u8)t33_kn_tab[hi][2];
		} else {
			*k = (u8)t33_kn_tab[lo][1];
			*n = (u8)t33_kn_tab[lo][2];
		}
	} else {
		*k = 0xa0;
		*n = 0xfe;
	}
}

/* the colour search of every line type: index of the colour, appended while the list has room */
static inline void t33_draw_color(struct t33_line_win *w, struct t33_colors *cl, const struct t33_osd_ops *op, u32 c)
{
	u32 i;

	for (i = 0; i < 4; i++) {
		if (cl->c[i] == c) {
			w->cidx = (u8)i;
			return;
		}
	}
	if (cl->n < 4) {
		cl->c[cl->n] = c;
		w->cidx = (u8)cl->n++;
	} else {
		op->msg(1, "Color buff is full !!! \n");
	}
}

/* tisp_msca_t33_set_line_reg */
static inline void t33_draw_set_reg(const struct t33_draw *d, const struct t33_colors *cl, const struct t33_osd_ops *op, u32 ch)
{
	u32 i, cnt = 0, s;

	op->slot(ch, 0xa4, 1);
	for (i = 0; i < 4; i++)
		op->slot(ch, 0xc7 + i, cl->c[i]);
	op->slot(ch, 9, d->msk);
	op->slot(ch, 0x12f, 0x101);
	for (i = 0; i < T33_DRAW_WIN; i++) {
		const struct t33_line_win *w = &d->win[i];

		s = 0x131 + 3 * i;
		if (w->en != 1)
			continue;
		op->slot(ch, s, (u32)w->y << 12 | w->x | (w->type + 1) * 0x1000000 | 0x4000000U);
		op->slot(ch, s + 1, (u32)w->h << 12 | (u32)w->cidx << 24 | w->w | (u32)w->b3aa << 26);
		op->slot(ch, s + 2, (u32)w->k << 8 | (u32)w->b3ad << 16 | w->width | (u32)w->b3ae << 18 | (u32)w->b3af << 23);
		cnt++;
	}
	for (; cnt < T33_DRAW_WIN; cnt++) {
		s = cnt * 3 + 0x131;
		op->slot(ch, s, 0);
		op->slot(ch, s + 1, 0);
		op->slot(ch, s + 1, 0);
	}
	op->slot(ch, 0x192, 0x10102);
	op->refresh(ch);
}

/*
 * tisp_msca_t33_api_set_line_out_cfg: block (36 B) { u8 win, u8 msk, .., u32 type @4, u8 en @12,
 * u16 x1 @14, y1 @16, x2 @18, y2 @20, u8 r @25, g @26, b @27, u8 width @28, u8 @29, u16 / u8 @30, u8 @32 }
 */
static inline void t33_draw_set_out(struct t33_draw *d, struct t33_colors *cl, const struct t33_osd_ops *op, u32 ch, u8 *p)
{
	struct t33_line_win *w;
	u32 n = p[0], type = t33_osd_u32(p + 4), x1, y1, x2, y2, dx, dy;
	u8 kn;

	if (n >= T33_DRAW_WIN)
		return;
	w = &d->win[n];
	w->type = type;
	w->col[0] = p[25];
	w->col[1] = p[26];
	w->col[2] = p[27];
	x1 = t33_osd_u16(p + 14);
	y1 = t33_osd_u16(p + 16);
	x2 = t33_osd_u16(p + 18);
	y2 = t33_osd_u16(p + 20);
	switch (type) {
	case 0:
	case 1:
	case 3:
		t33_draw_color(w, cl, op, (u32)p[25] << 16 | (u32)p[26] << 8 | p[27]);
		if (type == 3) {
			if ((u8)(p[1] - 8) < 0x39) {
				d->msk = p[1];
			} else {
				op->msg(1, "[ISP][%s, %d]: msk size error !!! default is 8 \n", "tisp_msca_t33_api_set_line_out_cfg", 0x80e);
				p[1] = 8;
				d->msk = 8;
			}
		}
		w->x = (u16)x1;
		w->y = (u16)y1;
		w->w = (u16)x2;
		w->h = (u16)y2;
		w->en = p[12];
		w->width = p[28];
		w->k = type == 1 ? (u8)t33_osd_u16(p + 30) : 5;
		w->b3ad = 0;
		w->b3ae = 1;
		w->b3af = type == 3 ? 0 : p[29];
		w->b3aa = type == 1 ? p[32] : p[30];
		break;
	case 2:
		t33_draw_color(w, cl, op, (u32)p[25] << 16 | (u32)p[26] << 8 | p[27]);
		w->raw[0] = (u16)x1;
		w->raw[1] = (u16)y1;
		w->raw[2] = (u16)x2;
		w->raw[3] = (u16)y2;
		w->en = p[12];
		w->b3aa = 0;
		w->width = p[28];
		w->k = 5;
		w->b3ad = 0;
		w->b3ae = 1;
		w->b3af = p[29];
		if (p[12] == 1 && x1 == x2 && y1 == y2) {
			op->msg(1, "[ISP][%s, %d]: The starting and ending coordinates of a line cannot be equal !!! \n",
				"tisp_msca_t33_api_set_line_out_cfg", 0x8b7);
			return;
		}
		if (x1 < x2) {
			w->x = (u16)x1;
			dx = x2 - x1;
		} else {
			w->x = (u16)x2;
			dx = x1 - x2;
		}
		if (y1 < y2) {
			w->y = (u16)y1;
			dy = y2 - y1;
		} else {
			w->y = (u16)y2;
			dy = y1 - y2;
		}
		if (x1 == x2) {
			w->k = 0;
			dx = w->width;
			w->b3ad = 0;
			w->b3ae = 0;
		} else if (y1 == y2) {
			w->k = 0xff;
			dy = w->width;
			w->b3ad = 2;
			w->b3ae = 0x1f;
		} else {
			if ((x2 < x1 && y2 < y1) || (x1 < x2 && y1 < y2))
				w->b3ad = dx / dy ? 2 : 0;
			else
				w->b3ad = dx / dy ? 3 : 1;
			t33_find_k_n(dx * 1000000 / dy, &w->k, &kn);
			w->b3ae = (signed char)kn < 0 ? (u8)-(signed char)kn : (u8)(kn | 0x10);
		}
		w->w = (u16)dx;
		w->h = (u16)dy;
		break;
	default:
		op->msg(0, "[ISP][%s, %d]: line type error !!! \n", "tisp_msca_t33_api_set_line_out_cfg", 0x909);
		break;
	}
	t33_draw_set_reg(d, cl, op, ch);
}

/* tisp_msca_t33_api_get_line_out_cfg: the fields of the stored type, the rest stays as the caller had it */
static inline void t33_draw_get_out(const struct t33_draw *d, u8 *p)
{
	const struct t33_line_win *w;

	if (p[0] >= T33_DRAW_WIN)
		return;
	w = &d->win[p[0]];
	t33_osd_p32(p + 4, w->type);
	p[25] = w->col[0];
	p[26] = w->col[1];
	p[27] = w->col[2];
	if (w->type > 3)
		return;
	p[12] = w->en;
	if (w->type == 2) {
		t33_osd_p16(p + 14, w->raw[0]);
		t33_osd_p16(p + 16, w->raw[1]);
		t33_osd_p16(p + 18, w->raw[2]);
		t33_osd_p16(p + 20, w->raw[3]);
	} else {
		t33_osd_p16(p + 14, w->x);
		t33_osd_p16(p + 16, w->y);
		t33_osd_p16(p + 18, w->w);
		t33_osd_p16(p + 20, w->h);
	}
	p[28] = w->width;
	p[29] = w->b3af;
	if (w->type == 1) {
		p[32] = w->b3aa;
		t33_osd_p16(p + 30, w->k);
	} else if (w->type != 2) {
		p[30] = w->b3aa;
	}
}

#endif /* TX_ISP_T33_OSD_H */
