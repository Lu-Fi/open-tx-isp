/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 TMO helpers (binary oracle block "r13A"): tisp_tmo_tmo_lumcurve_ev_interp,
 * tisp_tmo_tmo_satcurve_ev_interp, tisp_tmo_ramY_vi0_reg_refresh,
 * tisp_tmo_ramV_vi0_reg_refresh and tisp_tmo_rcfg_reg_num_disV.
 * The block state is the vendor's tmoInfo[ch] byte block.
 */
#ifndef TX_ISP_T33_R13A_H
#define TX_ISP_T33_R13A_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_tmo.h"
#include "tx_isp_t33_tmoP.h"

struct t33_r13a_ops {
	void (*drc_ctrl)(u32 ch);				/* tisp_tmo_drc_ctrl */
	void (*reg_write)(u32 ch, u32 blk, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	void (*sys_write)(u32 reg, u32 val);			/* system_reg_write */
	void (*vi23_ctrl)(u32 ch);				/* tisp_tmo_ramV_vi23_reg_ctrl */
};

/* byte offset in the section of luminance curve set k (0..10) */
static inline u32 t33_r13a_lum_off(u32 k)
{
	return k == 0 ? 4682 : k == 1 ? 5084 : 1064 + 402 * (k - 2);
}

/* byte offset in the section of saturation curve set k */
static inline u32 t33_r13a_sat_off(u32 k)
{
	return 420 + 50 * k;
}

/* one blended point: half word table h and word table w */
static inline void t33_r13a_put(u8 *h, u8 *w, u32 i, int t)
{
	t33_tmop_st16(h + 2 * i, (u32)t);
	*(u32 *)(w + 4 * i) = (u32)t & 0xffff;
}

/* tisp_tmo_tmo_lumcurve_ev_interp: curve_x = the 402 bytes the vendor copies from .rodata */
static inline void t33_tmo_lumcurve_interp(u8 *info, const u8 *curve_x)
{
	const u8 *sec = (const u8 *)(unsigned long)t33_le32(info + 2732);
	u32 sel = t32_tp_le16(sec + 5684), f = 0, k = 0, i, a, b, w;
	int t;

	if (sel == 0) {
		t33_tmo_curve_pick(sec, t33_le32(info + 10808), t33_le32(info + 10812), t33_le32(info + 10816),
				   &f, &k);
		for (i = 0; i < 201; i++) {
			a = t32_tp_le16(sec + t33_r13a_lum_off(k) + 2 * i);
			b = t32_tp_le16(sec + t33_r13a_lum_off(k + 1) + 2 * i);
			t = (int)(a * f + (0x100 - f) * b) / 0x100;
			t33_r13a_put(info + 2736, info + 448, i, t);
		}
	}
	if (sel == 1) {
		w = t33_le32(info + 1364);
		for (i = 0; i < 201; i++) {
			a = t32_tp_le16(sec + 4682 + 2 * i);
			b = t32_tp_le16(curve_x + 2 * i);
			t = (int)(a * w + (1023 - w) * b) / 1023;
			t33_r13a_put(info + 2736, info + 448, i, t);
		}
	}
}

/* tisp_tmo_tmo_satcurve_ev_interp */
static inline void t33_tmo_satcurve_interp(u8 *info)
{
	const u8 *sec = (const u8 *)(unsigned long)t33_le32(info + 2732);
	u32 f = 0, k = 0, i, a, b;
	int t;

	t33_tmo_curve_pick(sec, t33_le32(info + 10808), t33_le32(info + 10812), t33_le32(info + 10816), &f, &k);
	for (i = 0; i < 25; i++) {
		a = t32_tp_le16(sec + t33_r13a_sat_off(k) + 2 * i);
		b = t32_tp_le16(sec + t33_r13a_sat_off(k + 1) + 2 * i);
		t = (int)(a * f + (0x100 - f) * b) / 0x100;
		t33_r13a_put(info + 3138, info + 140, i, t);
	}
}

/* tisp_tmo_ramY_vi0_reg_refresh: the Y curve (100 pairs + 1) into list slots / the 0x32274 port */
static inline void t33_tmo_ramy_refresh(const struct t33_r13a_ops *o, u8 *const *info, u8 cfg, u32 ch)
{
	const u8 *v = info[ch];
	u32 base = cfg ? 93 : 0x32274, i, x;

	o->drc_ctrl(ch & 0xff);
	o->reg_write(ch, 8, cfg ? 91 : 0x32270, 0x101);
	for (i = 0; i < 100; i++) {
		x = (u32)(t32_tp_le16(v + 2736 + 4 * i + 2) & 0x1fff) << 16;
		x += t32_tp_le16(v + 2736 + 4 * i) & 0x1fff;
		o->reg_write(ch, 8, base + i, x);
	}
	x = t32_tp_le16(v + 3136) & 0x1fff;
	o->reg_write(ch, 8, base + 100, (x << 16) + x);
	o->reg_write(ch, 8, cfg ? 195 : 0x32270, 0x10102);
}

/* tisp_tmo_ramV_vi0_reg_refresh: mode = stIspInitPar word at byte 904 */
static inline void t33_tmo_ramv_refresh(const struct t33_r13a_ops *o, u8 *const *info, u32 mode, u8 cfg, u32 ch)
{
	const u8 *v = info[ch];
	const u8 *vt = (const u8 *)(unsigned long)t33_le32(v + 10832);
	u32 k;

	if (mode == 1) {
		o->reg_write(ch, 8, cfg ? 34 : 28920, 0);
		o->reg_write(ch, 8, cfg ? 31 : 28908, 0);
		o->reg_write(ch, 8, cfg ? 89 : 28896, t33_le32(v + 10836));
		if (!v[10845])
			o->reg_write(ch, 8, cfg ? 32 : 28912, 1);
	} else {
		o->vi23_ctrl(ch);
	}
	o->sys_write(0x32260, 0x101);
	for (k = 0; k < 1500; k++)
		o->sys_write(0x32264, t33_le32(vt + 4 * k));
	o->sys_write(0x32260, 0x10102);
}

/* tisp_tmo_rcfg_reg_num_disV: attr = stModuleAttr (0x700 bytes per channel, list page pointer at +0xe8) */
static inline int t33_tmo_rcfg_reg_num_disV(const u8 *attr, u16 *reg_num, u32 ch)
{
	u32 *list = (u32 *)(unsigned long)t33_le32(attr + (ch & 0xff) * 1792 + 232);

	*reg_num = 196;
	list[0] = 196;
	return 0;
}

#endif /* TX_ISP_T33_R13A_H */
