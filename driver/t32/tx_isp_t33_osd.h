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

#endif /* TX_ISP_T33_OSD_H */
