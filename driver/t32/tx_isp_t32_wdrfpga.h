/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 WDR per frame software of the vendor module: Tiziano_wdr_deghost_fpga and
 * Tiziano_wdr_fusion_fpga (Matt Davis' recovered C, PR #15), the two functions
 * tisp_wdr_process calls through struct t32_wdr_ops (.deghost / .fusion) of
 * tx_isp_t32_wdr.h.  Proven bit-exact against the vendor functions by host
 * oracles (tests/t32_oracle):
 *
 *  phase 1 (t32_wdrfpga1_oracle): deghost_fpga.
 *
 * The blocks are addressed like tx_isp_t32_wdr.h does: par = record + 12
 * (parameter block), ext = record + 16 (112 byte extension), hw = record + 20
 * (hardware parameter image), work = record + 24 (work area), members by the
 * vendor offsets named below.
 *
 * Vendor quirks kept: deghost mode 1 blends the main curve with its own copy
 * taken just before (k c + (16 - k) c = 16 c modulo 2^32, so the blend is the
 * identity and only the floor at the knot axis acts); the 81 knot x axis (t32_data_c2c84) has 1633 where the
 * 64 step would give 1663; deghost reads the u16 just past the 10200 byte work
 * area (tisp_wdr_getVar reads idx[256] of the last index table at work + 9688);
 * temporary buffers from kmalloc are not checked.  Vendor UB not fed by the
 * oracle: deghost mode 0 divides by ext[0] (the long exposure) unchecked.
 */
#ifndef TX_ISP_T32_WDRFPGA_H
#define TX_ISP_T32_WDRFPGA_H

#include "tx_isp_t32_wdr.h"

/* t32_data_c2c84: x positions of the 81 knot WDR curves (sic: 1633) */
static const int t32_wdr_knot_x[81] = {
	0, 15, 31, 47, 63, 79, 95, 111, 127, 143, 159, 175, 191, 207, 223, 239, 255,
	287, 319, 351, 383, 415, 447, 479, 511,
	575, 639, 703, 767, 831, 895, 959, 1023, 1087, 1151, 1215, 1279, 1343, 1407, 1471, 1535,
	1599, 1633, 1727, 1791, 1855, 1919, 1983, 2047, 2111, 2175, 2239, 2303, 2367, 2431, 2495,
	2559, 2623, 2687, 2751, 2815, 2879, 2943, 3007, 3071, 3135, 3199, 3263, 3327, 3391, 3455,
	3519, 3583, 3647, 3711, 3775, 3839, 3903, 3967, 4031, 4095,
};

/* ---- phase 1: Tiziano_wdr_deghost_fpga ---- */

/* parameter block (par) members of the deghost */
#define T32_WDRD_P_EXPK		0x80	/* u32[4]: exposure knots of the 4 weight curves */
#define T32_WDRD_P_MODE		0x1e4	/* u16: 1 = histogram matching, 0 = exposure ratio, other = keep */
#define T32_WDRD_P_VAR_LO	0x1e6	/* u16: slope variance below which the matched curve is kept */
#define T32_WDRD_P_VAR_HI	0x1e8	/* u16: variance from which the blend weight is W_HI */
#define T32_WDRD_P_W_LO		0x1ea	/* u16: blend weight at VAR_LO */
#define T32_WDRD_P_W_HI		0x1ec	/* u16: blend weight from VAR_HI on */
#define T32_WDRD_P_STR_ON	0x1ee	/* u16: 1 = strength follows the variance */
#define T32_WDRD_P_STR_LO	0x1f0	/* u16: variance knots of the strength */
#define T32_WDRD_P_STR_HI	0x1f2
#define T32_WDRD_P_STR_Y0	0x1f4	/* u16: strength at STR_LO */
#define T32_WDRD_P_STR_Y1	0x1f6	/* u16: strength from STR_HI on */
#define T32_WDRD_P_STR	0x1f8	/* u16: resulting strength */
#define T32_WDRD_P_DW_CFG	0x1fa	/* u16[5]: tisp_wdr_degweight_ev configuration */
#define T32_WDRD_P_DW_XS	0x30	/* u32[8]: its exposure knots */
#define T32_WDRD_P_DW_YS	0x206	/* u16[8]: its weights */
#define T32_WDRD_P_DW_OUT	0x204	/* u16: the degweight result */
#define T32_WDRD_P_WCURVE	0x216	/* u16[4][27]: weight curves at the 4 exposure knots */
#define T32_WDRD_P_DIVSH	0x71f	/* u8: numerator scale of the reciprocal table (<< 14) */
#define T32_WDRD_NW		27
/* hardware image (hw) members */
#define T32_WDRD_H_RECIP	0x1c	/* u32[27]: (scale << 14) / weight, rounded */
#define T32_WDRD_H_CURVE_A	0xa0	/* u16[81] */
#define T32_WDRD_H_CURVE_B	0x142	/* u16[81] */
#define T32_WDRD_H_CURVE	0x1e4	/* u16[81]: [0] strength 0..15, [1..80] the curve */
/* work area: 6 histograms of 256 u32 from +0x38, the 3 match index tables */
#define T32_WDRD_W_HIST		0x38
#define T32_WDRD_W_IDX_A	0x23d8
#define T32_WDRD_W_IDX_B	0x25d8
#define T32_WDRD_W_IDX_C	0x21d8

/* rounded line between two variance knots: y0 + ((x1 - x0) / 2 + (y1 - y0) (v - x0)) / (x1 - x0) */
static inline u32 t32_wdrd_ramp(u32 x0, u32 x1, u32 y0, u32 y1, u32 v)
{
	u32 span = x1 - x0;

	return ((u32)((int)span / 2) + (y1 - y0) * (v - x0)) / span + y0;
}

/*
 * Tiziano_wdr_deghost_fpga (ch): per frame deghost curves.  Mode 1: the vendor
 * matching of tisp_wdr_get_map over the 6 work histograms into the hw curves A,
 * the main curve and B; the main curve then blends towards itself by the slope
 * variance of index table B, is floored at the knot x axis, gets its strength
 * from its first two knots and is copied over A and B.  Mode 0: the curve is the
 * knot axis scaled by short / long exposure (clamped to 4095), copied over A and B.
 * Then (any mode) the optional variance strength, the 27 weights at the current
 * exposure (interpolated between the 4 curves) and their reciprocals.
 */
static inline void t32_wdr_deghost_fpga(struct t32_wdr_st *st, u32 ch)
{
	const struct t32_wdr_ops *o = st->ops;
	struct t32_wdr_rec *r = st->rec[ch];
	u8 *par = r->par, *hw = r->hw, *work = r->work;
	u32 *ext = (u32 *)r->ext;
	u16 *cur = (u16 *)(hw + T32_WDRD_H_CURVE);
	u16 *w = (u16 *)(ext + 9);
	u32 exp = ext[1], var = 0, i, mode;
	u16 *tmp;

	tmp = (u16 *)o->kmalloc(162, T32_WDR_GFP_ATOMIC);
	t32_wdr_zero((u8 *)tmp, 162);
	i = t32_wdr_exptime_update(ext);
	t32_wdr_w16(par, T32_WDRD_P_DW_OUT,
		    t32_wdr_degweight_ev((const u16 *)(par + T32_WDRD_P_DW_CFG), (const u32 *)(par + T32_WDRD_P_DW_XS),
					 (const u16 *)(par + T32_WDRD_P_DW_YS), i, ext));
	mode = t32_wdr_r16(par, T32_WDRD_P_MODE);
	if (mode == 1) {
		const u32 *h[6];
		u32 lo = t32_wdr_r16(par, T32_WDRD_P_VAR_LO), c1, s;

		for (i = 0; i < 6; i++)
			h[i] = (const u32 *)(work + T32_WDRD_W_HIST + 1024 * i);
		t32_wdr_get_map(o, h, (u16 *)(hw + T32_WDRD_H_CURVE_A), cur, (u16 *)(hw + T32_WDRD_H_CURVE_B),
				(u16 *)(work + T32_WDRD_W_IDX_A), (u16 *)(work + T32_WDRD_W_IDX_B),
				(u16 *)(work + T32_WDRD_W_IDX_C));
		var = t32_wdr_getvar((const u16 *)(work + T32_WDRD_W_IDX_B));
		if (var >= lo) {
			u32 hi = t32_wdr_r16(par, T32_WDRD_P_VAR_HI), k = t32_wdr_r16(par, T32_WDRD_P_W_HI);

			if (var < hi)
				k = t32_wdrd_ramp(lo, hi, t32_wdr_r16(par, T32_WDRD_P_W_LO), k, var);
			for (i = 0; i < 81; i++)
				tmp[i] = cur[i];
			for (i = 1; i < 81; i++) {
				u32 b = (u32)((int)(tmp[i] * k + (16 - k) * cur[i] + 8) / 16) & 0xffff;

				if ((int)b < t32_wdr_knot_x[i])
					b = (u32)t32_wdr_knot_x[i];
				cur[i] = (u16)b;
			}
		}
		c1 = cur[1];
		s = ((c1 * 31 + 8 - cur[2] * 15) & 0xfffff) >> 4;
		cur[0] = (u16)(s > 14 ? 15 : s);
		t32_wdr_cpy(hw + T32_WDRD_H_CURVE_A, (const u8 *)cur, 162);
		t32_wdr_cpy(hw + T32_WDRD_H_CURVE_B, (const u8 *)cur, 162);
	} else if (mode == 0) {
		u32 g = (ext[1] * 128 + (ext[0] >> 1)) / ext[0];

		for (i = 0; i < 81; i++) {
			u32 v = g * (u32)t32_wdr_knot_x[i] >> 7;

			cur[i] = (u16)(v > 0xfff ? 0xfff : v);
		}
		t32_wdr_cpy(hw + T32_WDRD_H_CURVE_A, (const u8 *)cur, 162);
		t32_wdr_cpy(hw + T32_WDRD_H_CURVE_B, (const u8 *)cur, 162);
	}
	if (t32_wdr_r16(par, T32_WDRD_P_STR_ON) == 1 && st->ev_hold == 1) {
		u32 lo = t32_wdr_r16(par, T32_WDRD_P_STR_LO), hi = t32_wdr_r16(par, T32_WDRD_P_STR_HI);

		if (var >= lo)
			t32_wdr_w16(par, T32_WDRD_P_STR, var < hi ?
				    t32_wdrd_ramp(lo, hi, t32_wdr_r16(par, T32_WDRD_P_STR_Y0),
						  t32_wdr_r16(par, T32_WDRD_P_STR_Y1), var) :
				    t32_wdr_r16(par, T32_WDRD_P_STR_Y1));
	}
	{
		const u8 *wc = par + T32_WDRD_P_WCURVE;
		u32 k;

		for (k = 0; k < 4; k++)
			if (exp < t32_wdr_r32(par, T32_WDRD_P_EXPK + 4 * k))
				break;
		for (i = 0; i < T32_WDRD_NW; i++) {
			if (k == 0 || k == 4)
				w[i] = (u16)t32_wdr_r16(wc, 54 * (k ? 3 : 0) + 2 * i);
			else
				w[i] = (u16)t32_wdr_interplate((int)t32_wdr_r32(par, T32_WDRD_P_EXPK + 4 * (k - 1)),
							       (int)t32_wdr_r32(par, T32_WDRD_P_EXPK + 4 * k),
							       (int)t32_wdr_r16(wc, 54 * (k - 1) + 2 * i),
							       (int)t32_wdr_r16(wc, 54 * k + 2 * i), (int)exp);
		}
	}
	for (i = 0; i < T32_WDRD_NW; i++)
		t32_wdr_w32(hw, T32_WDRD_H_RECIP + 4 * i, w[i] ? ((u32)(w[i] >> 1) + (u32)par[T32_WDRD_P_DIVSH] * 0x4000) / w[i]
								: 0x10000);
	o->kfree(tmp);
}

#endif
