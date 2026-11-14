/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA OSD / mask configuration in the vendor's stMscaAttr[ch] layout:
 * tisp_msca_t33_api_set_osd_out_cfg, _set_mask_out_cfg, _set_mask_reg and
 * _set_osd_reg.  Every register goes through the vendor's tisp_reg_write_t33
 * (ch, 23, slot or address, value): the list slot when mscaCfgMode is set,
 * else the direct register address.
 *
 * Attr block bytes used: +14/+16 image size, +712 + 20 w OSD window w
 * { en, x @2, y @4, w @6, h @8, word @12, half @16 }, +873.. OSD attributes,
 * +80 + 1984 c + 16 j mask window { en @4, @6, @8, @10, @12, colour @16 }.
 * Differences: an OSD window number > 3 (disable) and a mask channel > 2 are
 * refused without a store (the vendor writes past its block).
 */
#ifndef TX_ISP_T33_MSCAOSD_H
#define TX_ISP_T33_MSCAOSD_H

#include "tx_isp_t33_blk.h"

struct t33_mo_env {
	u8 *attr;			/* stMscaAttr[ch] */
	u8 cfg_mode;			/* mscaCfgMode */
	u8 *refresh;			/* tispMscaRefresh */
	u32 *osd_info;			/* MscaOsdInfo */
	void (*wr)(u32 ch, u32 blk, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	int (*msg)(u32 level, const char *fmt, ...);	/* isp_printf */
};

static inline u32 t33_mo_r16(const u8 *p, u32 o) { return p[o] | p[o + 1] << 8; }
static inline u32 t33_mo_r32(const u8 *p, u32 o)
{
	return p[o] | p[o + 1] << 8 | p[o + 2] << 16 | (u32)p[o + 3] << 24;
}
static inline void t33_mo_w16(u8 *p, u32 o, u32 v) { p[o] = (u8)v; p[o + 1] = (u8)(v >> 8); }
static inline void t33_mo_w32(u8 *p, u32 o, u32 v) { t33_mo_w16(p, o, v); t33_mo_w16(p, o + 2, v >> 16); }

static inline void t33_mo_wr(const struct t33_mo_env *e, u32 ch, u32 slot, u32 addr, u32 val)
{
	e->wr(ch, 23, e->cfg_mode ? slot : addr, val);
}

/* tisp_msca_t33_api_set_osd_reg */
static inline void t33_msca_osd_reg(const struct t33_mo_env *e, u32 ch)
{
	u8 *a = e->attr;
	u32 i, en = 0, x, y, w, h, b, base;
	u8 *win;

	for (i = 0; i < 4; i++) {
		win = a + 712 + 20 * i;
		if (!(win[0] & 1))
			continue;
		x = t33_mo_r16(win, 2);
		y = t33_mo_r16(win, 4);
		if ((x & 1) || (y & 1)) {
			t33_mo_w16(win, 2, x & 0xfffe);
			t33_mo_w16(win, 4, y & 0xfffe);
			(*e->osd_info)++;
		}
		w = t33_mo_r16(win, 6);
		h = t33_mo_r16(win, 8);
		if (a[876] == 2) {
			if ((w & 3) || (h & 1)) {
				t33_mo_w16(win, 6, w & 0xfffc);
				t33_mo_w16(win, 8, h & 0xfffe);
				(*e->osd_info)++;
			}
		} else if ((w & 1) || (h & 1)) {
			t33_mo_w16(win, 6, w & 0xfffe);
			t33_mo_w16(win, 8, h & 0xfffe);
			(*e->osd_info)++;
		}
		b = 180 + 4 * i;
		base = 0xf050c + 16 * i;
		t33_mo_wr(e, ch, b - 3, base - 12, t33_mo_r16(win, 2) << 16 | t33_mo_r16(win, 4));
		t33_mo_wr(e, ch, b - 2, base - 8, t33_mo_r16(win, 6) << 16 | t33_mo_r16(win, 8));
		t33_mo_wr(e, ch, b - 1, base - 4, t33_mo_r32(win, 12));
		t33_mo_wr(e, ch, b, base, t33_mo_r16(win, 16));
		en |= (u32)win[0] << i;
	}
	t33_mo_wr(e, ch, 5, 0xf0048, en);
	t33_mo_wr(e, ch, 170, 0xf04e0, (u32)a[873] << 4);
	t33_mo_wr(e, ch, 166, 0xf04d0, 0x381da506);
	t33_mo_wr(e, ch, 167, 0xf04d4, 0xd13b5e02);
	t33_mo_wr(e, ch, 168, 0xf04d8, 0xf6e70064);
	t33_mo_wr(e, ch, 169, 0xf04dc, 0x8010);
	t33_mo_wr(e, ch, 171, 0xf04e4, (u32)a[878] << 4 | a[876] | 0x10000 | (u32)a[874] << 8);
	for (i = 0; i < 4; i++)
		t33_mo_wr(e, ch, 173 + i, 0xf04f0 + 4 * i, (u32)a[880 + i] | (u32)a[884 + i] << 8 |
			  (u32)a[888 + i] << 16 | (u32)a[892 + i] << 24);
	*e->refresh = 1;
}

/* tisp_msca_t33_api_set_osd_out_cfg: p { u8 pinum, u8 en, u16 left, top, w, h, u32 @12, u16 @16 } */
static inline int t33_msca_osd_out_cfg(const struct t33_mo_env *e, u32 ch, const u8 *p)
{
	u8 *a = e->attr;
	u32 pin = p[0], left, top, ww, hh, in_w, in_h, x, y, w;
	u8 *win;

	if (p[1] == 1) {
		left = t33_mo_r16(p, 2);
		top = t33_mo_r16(p, 4);
		ww = t33_mo_r16(p, 6);
		hh = t33_mo_r16(p, 8);
		if (pin > 3) {
			e->msg(1, "t33 osd: pinum %d out of range\n", pin);
			return 0;
		}
		in_w = t33_mo_r16(a, 14);
		in_h = t33_mo_r16(a, 16);
		if (in_w < left + ww || in_h < top + hh) {
			e->msg(1, "t33 osd: %d %d %d %d %d %d %d\n", pin, left, top, ww, hh, in_w, in_h);
			e->msg(2, "t33 osd: image overflows the boundary");
			return 0;
		}
		win = a + 712 + 20 * pin;
		win[0] = p[1];
		t33_mo_w16(win, 2, left);
		t33_mo_w16(win, 4, top);
		t33_mo_w16(win, 6, ww);
		t33_mo_w16(win, 8, hh);
		t33_mo_w32(win, 12, t33_mo_r32(p, 12));
		t33_mo_w16(win, 16, t33_mo_r16(p, 16));
		/* the vendor's clip to the image (cannot trigger after the check above) */
		x = t33_mo_r16(win, 2);
		w = t33_mo_r16(win, 6);
		if (in_w < x + w)
			t33_mo_w16(win, 6, in_w - x);
		y = t33_mo_r16(win, 4);
		w = t33_mo_r16(win, 8);
		if (in_h < y + w)
			t33_mo_w16(win, 8, in_h - y);
	} else if (pin <= 3) {
		win = a + 712 + 20 * pin;
		win[0] = p[1];
		t33_mo_w16(win, 2, 0);
		t33_mo_w16(win, 4, 0);
		t33_mo_w16(win, 6, 0);
		t33_mo_w16(win, 8, 0);
		t33_mo_w32(win, 12, 0);
		t33_mo_w16(win, 16, 0);
	}
	t33_msca_osd_reg(e, ch);
	return 0;
}

/* tisp_msca_t33_api_set_mask_reg */
static inline void t33_msca_mask_reg(const struct t33_mo_env *e, u32 ch)
{
	u8 *a = e->attr;
	u32 k, j, en = 0, v, s, ad;
	const u8 *m, *ak;

	for (k = 0; k < 3; k++) {
		ak = a + 1984 * k;
		for (j = 0; j < 4; j++) {
			m = ak + 16 * j + 80;
			s = 117 + 16 * k + 4 * j;
			ad = 0xf0400 + 64 * k + 16 * j;
			v = 0;
			if (m[4] & 1) {
				t33_mo_wr(e, ch, s - 1, ad, t33_mo_r16(m, 6) << 16 | t33_mo_r16(m, 8));
				t33_mo_wr(e, ch, s + 1, ad + 8, t33_mo_r32(m, 16));
				v = t33_mo_r16(m, 10) << 16 | t33_mo_r16(m, 12);
			}
			t33_mo_wr(e, ch, s, ad + 4, v);
		}
		v = (ak[84] | ak[100] | ak[116] | ak[132]) & 0xff;
		en |= v << k;
		t33_mo_wr(e, ch, 4, 0xf0044, en);
	}
	*e->refresh = 1;
}

/* tisp_msca_t33_api_set_mask_out_cfg: p { u8 ch, idx, en, -, u16 @4, @6, @8, @10, u8 r @19, g @20, b @21 } */
static inline int t33_msca_mask_out_cfg(const struct t33_mo_env *e, u32 ch, const u8 *p)
{
	u8 *a = e->attr;
	u32 c = p[0], idx = p[1];
	u8 *m;

	if (p[2] == 1) {
		if (idx > 3) {
			e->msg(1, "t33 mask: index %d out of range\n", idx);
			return 0;
		}
		if (c > 2)
			return 0;
		m = a + 1984 * c + 16 * idx + 80;
		m[4] = p[2];
		t33_mo_w16(m, 8, t33_mo_r16(p, 4));
		t33_mo_w16(m, 6, t33_mo_r16(p, 6));
		t33_mo_w16(m, 10, t33_mo_r16(p, 8));
		t33_mo_w16(m, 12, t33_mo_r16(p, 10));
		t33_mo_w32(m, 16, (u32)p[19] << 16 | (u32)p[20] << 8 | p[21]);
	} else if (c <= 2 && idx <= 3) {
		m = a + 1984 * c + 16 * idx + 80;
		m[4] = 0;
	}
	t33_msca_mask_reg(e, ch);
	return 0;
}

#endif /* TX_ISP_T33_MSCAOSD_H */
