/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA API (batch 5): autozoom, mirror/flip, mask output config.
 * Module state is passed in by the caller: stMscaAttr (per channel parameter block pointers),
 * fcrop_attr, mscaCfgMode and tispMscaRefresh.
 */
#ifndef TX_ISP_T33_MSCA_API5_H
#define TX_ISP_T33_MSCA_API5_H

#include "tx_isp_t33_blk.h"

extern void tisp_msca_t33_scaling_algorithm(u32 ch);
extern void tisp_msca_t33_curve_calc(u32 ch, u32 i);
extern void tisp_msca_t33_ch_curve_write(u32 ch, u32 i);
extern void tisp_msca_t33_ctrl_reg_write(u32 ch, u32 i);
extern u32 tisp_reg_read_t33(u32 a, u32 b, u32 c);
extern int tisp_reg_write_t33(u32 a, u32 b, u32 c, u32 d);

#define T33_MSCA_ZOOM_ENT	22	/* bytes per zoom entry in a channel block */
#define T33_MSCA_MF_STRIDE	1984	/* bytes per mirror/flip entry */

static inline u32 t33_msca_le16(const u8 *p)
{
	return p[0] | p[1] << 8;
}

static inline void t33_msca_st16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/* tisp_msca_t33_api_set_autozoom */
static inline void t33_msca_set_autozoom(u32 ch, const u32 *zoom, u8 *const *attr, const u32 *fcrop,
					 const u8 *cfg_mode, u8 *refresh)
{
	u8 *base = attr[ch & 0xff];
	const u32 *fc = fcrop + (ch & 0xff) * 15;
	u32 i, v;
	u8 *e;

	for (i = 0; i < 3; i++) {
		e = base + T33_MSCA_ZOOM_ENT * i;
		if (zoom[i] == 1) {
			t33_msca_st16(e, 1);
			t33_msca_st16(e + 6, zoom[i + 3]);
			t33_msca_st16(e + 8, zoom[i + 6]);
			t33_msca_st16(e + 2, zoom[i + 9]);
			v = zoom[i + 12];
		} else {
			t33_msca_st16(e, 0);
			if (fc[i]) {
				t33_msca_st16(e + 6, fc[i + 3]);
				t33_msca_st16(e + 8, fc[i + 6]);
				t33_msca_st16(e + 2, fc[i + 9]);
				v = fc[i + 12];
			} else {
				t33_msca_st16(e + 6, 0);
				t33_msca_st16(e + 8, 0);
				t33_msca_st16(e + 2, t33_msca_le16(base + 70));
				v = t33_msca_le16(base + 72);
			}
		}
		t33_msca_st16(e + 4, v);
		tisp_msca_t33_scaling_algorithm(ch & 0xff);
		tisp_msca_t33_curve_calc(ch & 0xff, i);
		tisp_msca_t33_ch_curve_write(ch & 0xff, i);
		tisp_msca_t33_ctrl_reg_write(ch & 0xff, i);
		tisp_reg_write_t33(ch & 0xff, 23, *cfg_mode ? 1 : 0xf0010, 1);
	}
	*refresh = 1;
}

static inline u32 t33_msca_bit(u32 w, u32 n)
{
	return (w >> n) & 1;
}

/* tisp_msca_t33_api_set_mirr_flip */
static inline void t33_msca_set_mirr_flip(u32 ch, const u8 *flip, u8 *const *attr, const u8 *cfg_mode,
					  u8 *refresh)
{
	u8 *base = attr[ch & 0xff];
	u32 reg = *cfg_mode ? 7 : 0xf0050;
	u32 f = flip[0], k, t4, t5, t6, s, v;
	u8 *e = base + (f & 3) * T33_MSCA_MF_STRIDE;

	for (k = 0; k < 6; k++)
		e[80] = (u8)((e[80] & ~(1U << k)) | t33_msca_bit(f, 2 + k) << k);
	t6 = t33_le32(base + 4048);
	t5 = t33_le32(base + 2064);
	t4 = t33_le32(base + 80);
	s = t33_msca_bit(t6, 3) << 29 | t33_msca_bit(t5, 3) << 28 | t33_msca_bit(t4, 3) << 27 |
	    t33_msca_bit(t6, 1) << 26 | t33_msca_bit(t5, 1) << 25 | t33_msca_bit(t4, 1) << 24 |
	    t33_msca_bit(t6, 2) << 21 | t33_msca_bit(t5, 2) << 20 | t33_msca_bit(t4, 2) << 19 |
	    t33_msca_bit(t6, 0) << 18 | t33_msca_bit(t5, 0) << 17 | t33_msca_bit(t4, 0) << 16 |
	    t33_msca_bit(t6, 5) << 15 | t33_msca_bit(t6, 4) << 14 | t33_msca_bit(t5, 5) << 13 |
	    t33_msca_bit(t5, 4) << 12 | t33_msca_bit(t4, 5) << 11 | t33_msca_bit(t4, 4) << 10;
	v = tisp_reg_read_t33(ch & 0xff, 23, reg);
	tisp_reg_write_t33(ch & 0xff, 23, reg, (v & 0xc00001ffU) | s);
	tisp_reg_write_t33(ch & 0xff, 23, *cfg_mode ? 1 : 0xf0010, 1);
	*refresh = 1;
}

/* tisp_msca_t33_api_get_mask_out_cfg: o[0], o[1] select the entry, the rest is output */
static inline void t33_msca_get_mask_out_cfg(u32 ch, u8 *o, u8 *const *attr)
{
	const u8 *e = attr[ch & 0xff] + ((u32)o[0] * 124 + o[1]) * 16;

	if (e[84] == 1) {
		o[2] = 1;
		t33_msca_st16(o + 4, t33_msca_le16(e + 88));
		t33_msca_st16(o + 6, t33_msca_le16(e + 86));
		t33_msca_st16(o + 8, t33_msca_le16(e + 90));
		t33_msca_st16(o + 10, t33_msca_le16(e + 92));
		o[19] = e[98];
		o[20] = e[97];
		o[21] = e[96];
	} else {
		o[2] = 0;
		t33_msca_st16(o + 4, 0);
		t33_msca_st16(o + 6, 0);
		t33_msca_st16(o + 8, 0);
		t33_msca_st16(o + 10, 0);
		o[19] = 0;
		o[20] = 0;
		o[21] = 0;
	}
}

#endif /* TX_ISP_T33_MSCA_API5_H */
