/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 WDR per frame software of the vendor module: Tiziano_wdr_deghost_fpga and
 * Tiziano_wdr_fusion_fpga (Matt Davis' recovered C, PR #15), the two functions
 * tisp_wdr_process calls through struct t32_wdr_ops (.deghost / .fusion) of
 * tx_isp_t32_wdr.h.  Proven bit-exact against the vendor functions by host
 * oracles (tests/t32_oracle):
 *
 *  phase 1 (t32_wdrfpga1_oracle): deghost_fpga;
 *  phase 2 (t32_wdrfpga2_oracle): fusion_fpga, end to end (all its outputs, globals,
 *  kmalloc / kfree and the tisp_tmo_timeratio_curve_adjust hook calls).
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
 *
 * fusion_fpga beyond vendor (#279, user OK 2026-11-11): the vendor allocates the two
 * statistics buffers with 450 bytes (225 entries) but clears the first with 510 bytes
 * (60 bytes past the kmalloc size) and its sort length par + 0x2f4 (low byte, up to 255)
 * is not limited to 225, so the two sums read up to 255 entries (the second buffer's
 * entries past 225 are uninitialised slab memory; tisp_wdr_paixu itself stays below 225).
 * The port allocates and clears both buffers with 510 bytes (255 entries): the first
 * buffer and every output for sort lengths <= 225 are bit-exact with the vendor; for
 * 226..255 the second sum reads zeros where the vendor reads stale heap bytes.
 * Vendor divide by zero not fed by the oracle: ext[0] = 0, a zero block size
 * (t32_data_e3b30), sort length 0, a mean that wraps to 0, a zero scale sc (lim*32 /
 * gain wrapping to a multiple of 65536), par + 8 == par + 4, two tone knots (par + 0x5e0)
 * that differ but round to the same 1/128 step.  The "return 0" fallbacks of the knot
 * searches are the vendor's; they are unreachable (the value lies below the last knot).
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

/* ---- phase 2: Tiziano_wdr_fusion_fpga ---- */

/* globals of the fusion (vendor .bss; WDRMODE t32_data_e3b10 is st->nbuf, wdr_frm_num t32_data_e3a9a st->ev_hold) */
struct t32_wdrf_st {
	u16 diff_fix_y[5];	/* t32_data_e3b14: last step of each fix_y value (0 / 1) */
	u16 fix_y_tmp[5];	/* t32_data_e3b20: tone curve y knots, moved by at most 1 per frame */
	u16 diff_thr_max;	/* t32_data_e3b2a: last step of the maximum value (<= 50) */
	u16 thr_max_tmp;	/* t32_data_e3b2c: maximum value of the last frame */
	u16 ev_last_switch;	/* t32_data_e3b3a: exposure at the last output mode switch */
	u8 frame_sum;		/* t32_data_e3b3c: frames since then */
	/* tisp_tmo_timeratio_curve_adjust (ch, ratio, mode, a, b, c, d, e): the TMO block's hook */
	void (*tmo_timeratio_curve_adjust)(u32 ch, u32 ratio, u32 mode, u32 a, u32 b, u32 c, u32 d, u32 e);
};

#define T32_WDRF_BUF_SZ		510	/* the two statistics buffers: 255 entries (sort length up to 255; vendor kmalloc 450, #279) */
#define T32_WDRF_CURVE_SZ	50	/* 25 u16 fusion curve */

/* t32_data_c2c50: default fusion curve */
static const u16 t32_wdrf_defc[25] = {
	0, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xbff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff,
	0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff,
};

/* signed u16 member */
static inline int t32_wdrf_s16(const u8 *p, u32 off)
{
	return (s16)t32_wdr_r16(p, off);
}

/* knot table with the vendor's inline rounding: y0 + (span / 2 + (y1 - y0)(v - x0)) / span, signed (not truncated) */
static inline u32 t32_wdrf_knots(const u8 *par, u32 xo, u32 yo, u32 n, u32 v)
{
	u32 i;

	if (v < t32_wdr_r16(par, xo))
		return t32_wdr_r16(par, yo);
	if (!(v < t32_wdr_r16(par, xo + 2 * (n - 1))))
		return t32_wdr_r16(par, yo + 2 * (n - 1));
	for (i = 1; i < n; i++)
		if (v < t32_wdr_r16(par, xo + 2 * i)) {
			u32 x0 = t32_wdr_r16(par, xo + 2 * i - 2), y0 = t32_wdr_r16(par, yo + 2 * i - 2);
			int span = (int)(t32_wdr_r16(par, xo + 2 * i) - x0);

			return y0 + (u32)t32_wdr_sdiv((int)((u32)(span / 2) + (t32_wdr_r16(par, yo + 2 * i) - y0) * (v - x0)),
						    span);
		}
	return 0;
}

/* knot table through tisp_wdr_interplate, 16 bit result */
static inline u32 t32_wdrf_iknots(const u8 *par, u32 xo, u32 yo, u32 n, u32 v)
{
	u32 i;

	if (v < t32_wdr_r16(par, xo))
		return t32_wdr_r16(par, yo);
	if (!(v < t32_wdr_r16(par, xo + 2 * (n - 1))))
		return t32_wdr_r16(par, yo + 2 * (n - 1));
	for (i = 1; i < n; i++)
		if (v < t32_wdr_r16(par, xo + 2 * i))
			return (u32)t32_wdr_interplate((int)t32_wdr_r16(par, xo + 2 * i - 2), (int)t32_wdr_r16(par, xo + 2 * i),
						       (int)t32_wdr_r16(par, yo + 2 * i - 2), (int)t32_wdr_r16(par, yo + 2 * i),
						       (int)v) & 0xffff;
	return 0;
}

/* 5 values at v: lo set below x0, hi set from x1 on, tisp_wdr_interplate between */
static inline void t32_wdrf_set5(const u8 *par, u32 xo, u32 lo, u16 *out, u32 v)
{
	u32 x0 = t32_wdr_r16(par, xo), x1 = t32_wdr_r16(par, xo + 2), i;

	for (i = 0; i < 5; i++) {
		u32 a = t32_wdr_r16(par, lo + 2 * i), b = t32_wdr_r16(par, lo + 10 + 2 * i);

		out[i] = (u16)(!(x0 < v) ? a : !(x1 < v) ? (u32)t32_wdr_interplate((int)x0, (int)x1, (int)a, (int)b, (int)v) : b);
	}
}

/* fusion curve knot segments: c[i] = a + ((b - a) d + 2^(sh-1)) >> sh, a, b signed */
static inline void t32_wdrf_seg(u16 *c, const u8 *a, const u8 *b, u32 d, u32 sh)
{
	u32 i;

	for (i = 0; i < 25; i++) {
		int y0 = t32_wdrf_s16(a, 2 * i), y1 = t32_wdrf_s16(b, 2 * i);

		c[i] = (u16)(((int)((u32)(y1 - y0) * d + (1U << (sh - 1))) >> sh) + y0);
	}
}

/* first segment from an unsigned base: c[i] = base + ((b - base) d + 16) >> 5 */
static inline void t32_wdrf_seg0(u16 *c, const u16 *base, const u8 *b, u32 d)
{
	u32 i;

	for (i = 0; i < 25; i++) {
		u32 y0 = base[i];

		c[i] = (u16)(((int)(((u32)t32_wdrf_s16(b, 2 * i) - y0) * d + 16) >> 5) + y0);
	}
}

/* last segment: c[i] = a + ((b - a) d + span / 2) / span, signed */
static inline void t32_wdrf_segn(u16 *c, const u8 *a, const u8 *b, u32 d, int span)
{
	u32 i;

	for (i = 0; i < 25; i++) {
		int y0 = t32_wdrf_s16(a, 2 * i), y1 = t32_wdrf_s16(b, 2 * i);

		c[i] = (u16)(t32_wdr_sdiv((int)((u32)(y1 - y0) * d + (u32)(span >> 1)), span) + y0);
	}
}

/*
 * The fusion curve at u (6 knots par + 0x3d0): from base (defc or the curve itself) to set[0],
 * then set[k - 1] to set[k] per knot interval, set[4] above the last knot.  Returns 0 when u
 * is at or below the first knot (no curve: the caller decides).
 */
static inline int t32_wdrf_curve(const u8 *par, u16 *c, const u16 *base, u32 set, u32 u)
{
	u32 k[6], i;

	for (i = 0; i < 6; i++)
		k[i] = t32_wdr_r16(par, 0x3d0 + 2 * i);
	if (!(k[0] < u))
		return 0;
	if (u <= k[1])
		t32_wdrf_seg0(c, base, par + set, (u - k[0]) & 0xffff);
	else if (u <= k[2])
		t32_wdrf_seg(c, par + set, par + set + 0x32, (u - k[1]) & 0xffff, 6);
	else if (u <= k[3])
		t32_wdrf_seg(c, par + set + 0x32, par + set + 0x64, (u - k[2]) & 0xffff, 7);
	else if (u <= k[4])
		t32_wdrf_seg(c, par + set + 0x64, par + set + 0x96, (u - k[3]) & 0xffff, 7);
	else if (u <= k[5])
		t32_wdrf_segn(c, par + set + 0x96, par + set + 0xc8, (u - k[4]) & 0xffff, (int)(k[5] - k[4]));
	else
		t32_wdr_cpy((u8 *)c, par + set + 0xc8, T32_WDRF_CURVE_SZ);
	return 1;
}

/* luminance levels of tisp_wdr_get_lum_s over the 160 bin histogram (work + 0x1f58) */
static inline void t32_wdrf_lum(const u8 *par, const u8 *work, u16 *lum, const u16 *lut)
{
	t32_wdr_get_lum_s((const u32 *)(work + 0x1f58), (const u16 *)(par + 0x322), lum, par + 0x72b, lut);
}

/*
 * Tiziano_wdr_fusion_fpga (ch): per frame fusion parameters.  In vendor order:
 *  - optional exposure dependent 3 threshold pairs (par + 0x6f6..) -> par + 0x100 / 0x102;
 *  - exposure ratio long / short (8.8), clamped, TMO hook, two knot offsets -> ext[4], ext[5];
 *  - the 15 x 15 block statistics (work + 0x1850 / 0x1bd4) normalised by the block sizes,
 *    sorted (tisp_wdr_paixu), mean of the brightest n and of the top 10 dark values;
 *  - five weighted terms -> the maximum value (moved by at most 50 per frame), its limit;
 *  - tone curve y knots fix_y (par + 0x5ec, moved by at most 1 per frame), output mode switch;
 *  - fusion curve hw + 0x370 by the scaled limit between the exposure knots, its luminance
 *    blend, the 33 entry tone curve hw + 0x3a2 and the slopes ext[6], ext[7], 4 log2 widths.
 */
static inline void t32_wdr_fusion_fpga(struct t32_wdr_st *st, struct t32_wdrf_st *fs, u32 ch)
{
	const struct t32_wdr_ops *o = st->ops;
	struct t32_wdr_rec *r = st->rec[ch];
	u8 *par = r->par, *hw = r->hw, *work = r->work, *ext8 = r->ext;
	u32 *ext = (u32 *)r->ext;
	u16 *cur = (u16 *)(hw + 0x370), *tone = (u16 *)(hw + 0x3a2);
	u32 exp = ext[1], lexp = ext[0], g4 = st->grid[4], maxv = t32_wdr_r32(work, 0x184c);
	u32 nsort = t32_wdr_r16(par, 0x2f4) & 0xff, ratio, t, top, avg, t2, t2c, maxc, thr, lim, gain, sc, u, i;
	int off_a = 0, off_b, ws[5];
	u16 lum[4] = { 0, 0, 0, 0 }, lut[160], defc[25], dc[5], e8[5], f4[5], q[5] = { 0, 0, 0, 0, 0 };
	s16 sl[4];
	u16 *a8, *a4, *s, *s00, *tb, *t90;

	a8 = (u16 *)o->kmalloc(T32_WDRF_BUF_SZ, T32_WDR_GFP_ATOMIC);
	a4 = (u16 *)o->kmalloc(T32_WDRF_BUF_SZ, T32_WDR_GFP_ATOMIC);
	for (i = 0; i < 160; i++)	/* t32_data_c2b10 */
		lut[i] = (u16)(i < 32 ? 64 * i + 63 : 16 * i + 1551);
	t32_wdr_cpy((u8 *)defc, (const u8 *)t32_wdrf_defc, T32_WDRF_CURVE_SZ);
	s = (u16 *)o->kmalloc(T32_WDRF_CURVE_SZ, T32_WDR_GFP_ATOMIC);
	s00 = (u16 *)o->kmalloc(T32_WDRF_CURVE_SZ, T32_WDR_GFP_ATOMIC);
	tb = (u16 *)o->kmalloc(T32_WDRF_CURVE_SZ, T32_WDR_GFP_ATOMIC);
	t90 = (u16 *)o->kmalloc(T32_WDRF_CURVE_SZ, T32_WDR_GFP_ATOMIC);
	t32_wdr_zero((u8 *)tb, T32_WDRF_CURVE_SZ);
	t32_wdr_cpy((u8 *)t90, (const u8 *)defc, T32_WDRF_CURVE_SZ);
	t32_wdr_zero((u8 *)a8, T32_WDRF_BUF_SZ);
	t32_wdr_zero((u8 *)a4, T32_WDRF_BUF_SZ);
	t32_wdr_zero((u8 *)s, T32_WDRF_CURVE_SZ);
	t32_wdr_zero((u8 *)s00, T32_WDRF_CURVE_SZ);

	/* exposure dependent threshold pair */
	if (par[0x729] == 1) {
		u32 e0 = t32_wdr_r32(par, 0x90), e1 = t32_wdr_r32(par, 0x94), te[3];
		s16 b0[3], b8[3], c0[3];
		int v;

		for (i = 0; i < 3; i++) {
			u32 k;

			for (k = 0; k < 3; k++) {
				u32 lo = 0x6f6 + 6 * k + 2 * i;
				s16 y = !(exp < e0) && exp < e1 ?
					(s16)t32_wdr_interplate((int)e0, (int)e1, t32_wdrf_s16(par, lo), t32_wdrf_s16(par, lo + 18), (int)exp) :
					(s16)t32_wdr_r16(par, exp < e0 ? lo : lo + 18);

				(k == 0 ? b0 : k == 1 ? b8 : c0)[i] = y;
			}
			te[i] = (u32)b0[i] * g4;
		}
		if (te[0] < maxv) {
			t32_wdr_w16(par, 0x100, (u16)b8[0]);
			v = c0[0];
		} else if (te[2] < maxv) {
			u32 a = !(maxv > te[1]), b = a + 1, span = te[a] - te[b], n = te[a] - maxv;

			t32_wdr_w16(par, 0x100, ((u32)(b8[b] - b8[a]) * n + (span >> 1)) / span + (u32)b8[a]);
			v = (int)(((u32)(c0[b] - c0[a]) * n + (span >> 1)) / span + (u32)c0[a]);
		} else {
			t32_wdr_w16(par, 0x100, (u16)b8[2]);
			v = c0[2];
		}
		t32_wdr_w16(par, 0x102, (u32)v);
	}

	/* exposure ratio (8.8) and the two offsets */
	ratio = (exp * 256 + (lexp >> 1)) / lexp;
	if (fs->tmo_timeratio_curve_adjust)
		fs->tmo_timeratio_curve_adjust(ch, ratio & 0xffff, par[0x116], par[0x620], par[0x622], par[0x624], par[0x632],
					       t32_wdr_r16(par, 0x68e));
	t = t32_wdr_r16(par, 0x2ee) << 8;
	t = (t < (ratio & 0xffff) ? t : ratio) & 0xffff;
	if (t32_wdr_r16(par, 0x300) == 1) {
		off_a = (s16)t32_wdrf_knots(par, 0x2f6, 0x302, 5, t);
		if (t32_wdr_r16(par, 0x684) == 1) {
			u32 l;

			t32_wdrf_lum(par, work, lum, lut);
			if (off_a < (int)t32_wdr_r16(par, 0x694))
				t32_wdr_w16(par, 0x694, (u32)off_a);
			l = lum[3];
			if (l < t32_wdr_r16(par, 0x686))
				off_a = -t32_wdrf_s16(par, 0x690);
			else if (l < t32_wdr_r16(par, 0x688))
				off_a = (s16)-t32_wdr_interplate((int)t32_wdr_r16(par, 0x686), (int)t32_wdr_r16(par, 0x688),
								 (int)t32_wdr_r16(par, 0x690), (int)t32_wdr_r16(par, 0x692), (int)l);
			else if (l < t32_wdr_r16(par, 0x68a))
				off_a = (s16)t32_wdr_interplate((int)t32_wdr_r16(par, 0x688), (int)t32_wdr_r16(par, 0x68a),
								-t32_wdrf_s16(par, 0x692), (int)t32_wdr_r16(par, 0x694), (int)l);
			else if (l < t32_wdr_r16(par, 0x68c))
				off_a = (s16)t32_wdr_interplate((int)t32_wdr_r16(par, 0x68a), (int)t32_wdr_r16(par, 0x68c),
								(int)t32_wdr_r16(par, 0x694), (int)t32_wdr_r16(par, 0x696), (int)l);
			else
				off_a = t32_wdrf_s16(par, 0x696);
		}
	}
	off_b = t32_wdr_r16(par, 0x316) == 1 ? (s16)t32_wdrf_knots(par, 0x30c, 0x318, 5, t) : off_a;
	ext[5] = t - (u32)off_b;
	ext[4] = t - (u32)off_a;
	gain = 0xfff;
	if (t32_wdr_r16(par, 0x2f2) == 1)
		gain = ((t32_wdr_r32(work, 0x1838) + t32_wdr_r32(work, 0x183c) + t32_wdr_r32(work, 0x1840) +
			 t32_wdr_r32(work, 0x1844)) & 0x3ffff) >> 2;

	/* 15 x 15 block statistics: inner blocks / last row / last column / corner by their sizes */
	for (i = 0; i < 225; i++) {
		u32 row = i / 15, col = i % 15,
		    g = row == 14 ? (col == 14 ? st->grid[2] : st->grid[1]) : col == 14 ? st->grid[0] : st->grid[3];

		a8[i] = (u16)t32_wdr_sdiv((int)(t32_wdr_r32(work, 0x1850 + 4 * i) * 16 + (g >> 1)), (int)g);
		a4[i] = (u16)t32_wdr_sdiv((int)(t32_wdr_r32(work, 0x1bd4 + 4 * i) * 16 + (g >> 1)), (int)g);
	}
	t32_wdr_paixu(a8, nsort, 1);
	t32_wdr_paixu(a4, 10, 0);
	for (i = 0, avg = 0, top = 0; i < nsort; i++) {
		avg += a8[i];
		top += a4[i];
	}
	avg = (((nsort >> 1) + avg) / nsort + 1) & 0xffff;
	top = ((top + 5) / 10) & 0xffff;
	t2 = (u32)(t32_wdr_sdiv((int)(t * top), (int)avg) >> 8);
	if (t32_wdr_r16(par, 0x698) == 1)
		t32_wdr_w16(par, 0x6a2, t32_wdrf_iknots(par, 0x69a, 0x6a4, 4, t2));
	maxc = maxv > 0xffff ? 0xffff : maxv;
	t2c = t2 < 0x10000 ? t2 : 0xffff;
	t32_wdr_w16(ext8, 0x66, top);
	t32_wdr_w16(ext8, 0x68, avg);
	t32_wdr_w16(ext8, 0x6c, t2c);
	t32_wdr_w16(ext8, 0x6a, maxc / g4);

	/* five weighted terms of the maximum value */
	{
		u32 w1, w2, w3, w4, w5;

		w1 = t32_wdr_r16(par, 0x6ac) == 1 ? t32_wdrf_iknots(par, 0x6ae, 0x6c2, 9, t2c) : t32_wdr_r16(par, 0x6c0);
		if (t32_wdr_r16(par, 0x328) == 1) {
			if (t32_wdr_r16(par, 0x684) == 0)
				t32_wdrf_lum(par, work, lum, lut);
			w2 = t32_wdrf_knots(par, 0x32a, 0x342, 11, lum[3]) & 0xffff;
		} else {
			w2 = t32_wdr_r16(par, 0x340);
		}
		w3 = t32_wdr_r16(par, 0x358) == 1 ? t32_wdrf_knots(par, 0x35a, 0x372, 11, top) & 0xffff : t32_wdr_r16(par, 0x370);
		w4 = t32_wdr_r16(par, 0x388) == 1 ? t32_wdrf_knots(par, 0x38a, 0x3a2, 11, avg) & 0xffff : t32_wdr_r16(par, 0x3a0);
		if (t32_wdr_r16(par, 0x3b8) == 1) {
			u32 kn[5];

			for (i = 0; i < 5; i++)
				kn[i] = t32_wdr_r16(par, 0x3ba + 2 * i) * g4;
			if (kn[0] < maxc) {
				w5 = t32_wdr_r16(par, 0x3c6);
			} else if (kn[4] < maxc) {
				for (i = 1, w5 = 0; i < 5; i++)
					if (kn[i] < maxc) {
						u32 y = t32_wdr_r16(par, 0x3c6 + 2 * i);

						w5 = (y + ((t32_wdr_r16(par, 0x3c4 + 2 * i) - y) * (maxc - kn[i])) / (kn[i - 1] - kn[i])) & 0xffff;
						break;
					}
			} else {
				w5 = t32_wdr_r16(par, 0x3ce);
			}
		} else {
			w5 = t32_wdr_r16(par, 0x3c4);
		}
		{
			u32 e0 = t32_wdr_r32(par, 0x98), e1 = t32_wdr_r32(par, 0x9c);

			for (i = 0; i < 5; i++)
				ws[i] = exp < e0 ? t32_wdrf_s16(par, 0x6de + 2 * i) : exp < e1 ?
					t32_wdr_interplate((int)e0, (int)e1, t32_wdrf_s16(par, 0x6de + 2 * i), t32_wdrf_s16(par, 0x6e8 + 2 * i), (int)exp) :
					t32_wdrf_s16(par, 0x6e8 + 2 * i);
		}
		thr = (w1 * (u32)ws[4] + 8 + w2 * (u32)ws[0] + w3 * (u32)ws[1] + w4 * (u32)ws[2] + w5 * (u32)ws[3]) >> 4;
	}
	if (st->ev_hold) {
		u32 prev = fs->thr_max_tmp;

		if (prev < thr) {
			fs->diff_thr_max = ((thr - prev) & 0xffff) < 51 ? (u16)(thr - prev) : 50;
			thr = fs->diff_thr_max + prev;
		} else {
			fs->diff_thr_max = ((prev - thr) & 0xffff) < 51 ? (u16)(prev - thr) : 50;
			thr = prev - fs->diff_thr_max;
		}
	}
	fs->thr_max_tmp = (u16)thr;
	if (t32_wdr_r16(par, 0x100) < 0x1000) {
		lim = (gain * ext[4] + 0x80) >> 8;
		if (thr <= lim)
			lim = thr;
	} else {
		lim = gain = 0xfff;
	}

	/* tone curve y knots */
	if (!st->ev_hold)
		for (i = 0; i < 5; i++)
			fs->fix_y_tmp[i] = (u16)t32_wdr_r16(par, 0x5ec + 2 * i);
	if (t32_wdr_r16(par, 0x5ea) == 1) {
		t32_wdrf_set5(par, 0x600, 0x60c, dc, top);
		t32_wdrf_set5(par, 0x604, 0x634, e8, t2c);
		t32_wdrf_set5(par, 0x608, 0x65c, f4, t32_wdr_r16(ext8, 0x6a));
		for (i = 0; i < 5; i++)
			t32_wdr_w16(par, 0x5ec + 2 * i, (u32)t32_wdr_sdiv((int)(e8[i] * t32_wdr_r16(par, 0x628) +
						dc[i] * t32_wdr_r16(par, 0x626) + f4[i] * t32_wdr_r16(par, 0x62a)), 16));
		for (i = 0; i < 5; i++) {
			u16 f = fs->fix_y_tmp[i], y = (u16)t32_wdr_r16(par, 0x5ec + 2 * i), step;

			if (f < y) {
				fs->diff_fix_y[i] = 1;
				step = 1;
			} else {
				fs->diff_fix_y[i] = (u16)(f - y) < 2 ? (u16)(f - y) : 1;
				step = (u16)-fs->diff_fix_y[i];
			}
			fs->fix_y_tmp[i] = (u16)(f + step);
		}
		for (i = 0; i < 5; i++)
			t32_wdr_w16(par, 0x5ec + 2 * i, fs->fix_y_tmp[i]);
	}

	/* output mode switch on exposure change */
	if (par[0x68] == 1) {
		u32 e = ext[1], d;

		if (!st->ev_hold)
			fs->ev_last_switch = (u16)e;
		d = fs->ev_last_switch <= e ? e - fs->ev_last_switch : fs->ev_last_switch - e;
		if (t32_wdr_r32(par, 0x70) < (d & 0xffff) || fs->frame_sum == 0) {
			t32_wdr_w16(par, 0x116, exp < t32_wdr_r32(par, 0x74) ? st->nbuf : 1);
			fs->ev_last_switch = (u16)ext[1];
		}
		fs->frame_sum++;
		if (t32_wdr_r32(par, 0x6c) < fs->frame_sum)
			fs->frame_sum = 0;
	}

	/* limit outputs, scale */
	sc = lim > 0xfff ? ((lim * 32 + (gain >> 1)) / gain) & 0xffff : 0x20;
	t32_wdr_w16(ext8, 0x5a, lim);
	t32_wdr_w16(ext8, 0x5c, gain);
	t32_wdr_w16(ext8, 0x60, lum[0]);
	t32_wdr_w16(ext8, 0x5e, thr);
	t32_wdr_w16(ext8, 0x64, lum[3]);
	{
		u32 x0 = t32_wdr_r16(par, 0x5d0), x3 = t32_wdr_r16(par, 0x5d6), v;

		if (lim < x0) {
			v = t32_wdr_r16(par, 0x5d8);
		} else if (lim < x3) {
			u32 k = lim < t32_wdr_r16(par, 0x5d2) ? 0 : lim < t32_wdr_r16(par, 0x5d4) ? 1 : 2;
			u32 xa = t32_wdr_r16(par, 0x5d0 + 2 * k), ya = t32_wdr_r16(par, 0x5d8 + 2 * k),
			    span = t32_wdr_r16(par, 0x5d2 + 2 * k) - xa;

			v = (ya + ((u32)((int)span / 2) + (t32_wdr_r16(par, 0x5da + 2 * k) - ya) * (lim - xa)) / span) & 0xffff;
		} else {
			v = t32_wdr_r16(par, 0x5de);
		}
		u = (sc - v) & 0xffff;
		t32_wdr_w16(ext8, 0x62, sc - v);
		ext[8] = ((u * 256 + (sc >> 1)) / sc) & 0xffff;
	}

	/* fusion curve by u between the exposure knots: low set par + 0x3dc, high set par + 0x4d6 */
	{
		u32 e0 = t32_wdr_r32(par, 0xa0), e1 = t32_wdr_r32(par, 0xa4);

		if (e0 < exp && exp <= e1) {
			if (!t32_wdrf_curve(par, s, cur, 0x3dc, u)) {
				t32_wdr_cpy((u8 *)s, (const u8 *)defc, T32_WDRF_CURVE_SZ);
				t32_wdr_cpy((u8 *)s00, (const u8 *)defc, T32_WDRF_CURVE_SZ);
			} else {
				int span = (int)(e1 - e0);

				t32_wdrf_curve(par, s00, cur, 0x4d6, u);
				for (i = 0; i < 25; i++)
					cur[i] = (u16)(t32_wdr_sdiv((int)((u32)((s16)s00[i] - (s16)s[i]) * ((exp - e0) & 0xffff) +
									(u32)(span >> 1)), span) + (s16)s[i]);
			}
		} else if (!t32_wdrf_curve(par, cur, e0 < exp ? cur : defc, e0 < exp ? 0x4d6 : 0x3dc, u)) {
			t32_wdr_cpy((u8 *)cur, (const u8 *)defc, T32_WDRF_CURVE_SZ);
		}
	}

	/* luminance blend of the fusion curve towards the default curve */
	if (t32_wdr_r16(par, 0x6d4) == 1) {
		u32 l2;

		if (t32_wdr_r16(par, 0x684) == 0 && t32_wdr_r16(par, 0x328) == 0)
			t32_wdrf_lum(par, work, lum, lut);
		l2 = lum[2];
		if (lum[0] > 0xf3c) {
			l2 = (u32)((int)(l2 + lum[1]) >> 1);
			lum[2] = (u16)l2;
		}
		if (l2 <= t32_wdr_r16(par, 0x6d8)) {
			for (i = 0; i < 25; i++)
				tb[i] = t90[i] > 0xfff ? 0xfff : t90[i];
			if (t32_wdr_r16(par, 0x6d6) < lum[2]) {
				u32 k = (u32)t32_wdr_interplate((int)t32_wdr_r16(par, 0x6d6), (int)t32_wdr_r16(par, 0x6d8),
								(int)t32_wdr_r16(par, 0x6da), (int)t32_wdr_r16(par, 0x6dc), lum[2]) & 0xffff;

				for (i = 0; i < 25; i++) {
					u16 v = (u16)((int)(tb[i] * k + (0x80 - k) * cur[i]) >> 7);

					tb[i] = v > 0xfff ? 0xfff : v;
				}
			}
			t32_wdr_cpy((u8 *)cur, (const u8 *)tb, T32_WDRF_CURVE_SZ);
		}
	}

	/* 33 entry tone curve from the 5 fix_y knots (x par + 0x5e0.. in 1/128) */
	for (i = 0; i < 4; i++) {
		u32 a = t32_wdr_r16(par, 0x5e2 + 2 * i);

		q[i + 1] = (u16)((int)(a + 0x40) >> 7);
		if (a == t32_wdr_r16(par, 0x5e0 + 2 * i)) {
			sl[i] = 0;
		} else {
			int dq = (int)q[i + 1] - (int)q[i];

			sl[i] = (s16)t32_wdr_sdiv((int)((t32_wdr_r16(par, 0x5ee + 2 * i) - t32_wdr_r16(par, 0x5ec + 2 * i)) * 16 +
							(u32)(dq / 2)), dq);
		}
	}
	tone[0] = (u16)(t32_wdr_r16(par, 0x5ec) << 4);
	for (i = 1; i < 33; i++)
		tone[i] = (u16)(tone[i - 1] + (!(q[0] < i) ? 0 : !(q[1] < i) ? sl[0] : !(q[2] < i) ? sl[1] :
					       !(q[3] < i) ? sl[2] : sl[3]));
	for (i = 0; i < 33; i++)
		tone[i] >>= 4;

	/* slopes and widths */
	{
		u32 d = t32_wdr_r32(par, 8) - t32_wdr_r32(par, 4);
		int e;

		if ((int)d < 0)
			d = 1;
		ext[6] = ((ext[5] - ext[4]) * 1024 + (u32)((int)d >> 1)) / d;
		e = (int)t32_wdr_r16(par, 0x102) - (int)t32_wdr_r16(par, 0x100);
		if (e < 1)
			e = 1;
		ext[7] = (u32)t32_wdr_sdiv((int)(t32_wdr_r16(par, 0x104) * 4096 + (u32)(e >> 1)), e);
		t32_wdr_w16(par, 0x126, t32_wdr_log2((int)(t32_wdr_r16(par, 0x124) - t32_wdr_r16(par, 0x122))));
		t32_wdr_w16(par, 0x130, t32_wdr_log2((int)(t32_wdr_r16(par, 0x12e) - t32_wdr_r16(par, 0x12c))));
		t32_wdr_w16(par, 0x114, t32_wdr_log2((int)(t32_wdr_r16(par, 0x10e) - t32_wdr_r16(par, 0x10c))));
		t32_wdr_w16(par, 0x10a, t32_wdr_log2((int)(t32_wdr_r16(par, 0x108) - t32_wdr_r16(par, 0x106))));
	}
	o->kfree(a8);
	o->kfree(a4);
	o->kfree(s);
	o->kfree(s00);
	o->kfree(tb);
	o->kfree(t90);
}

#endif
