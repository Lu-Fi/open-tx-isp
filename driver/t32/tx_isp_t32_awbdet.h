/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AWB colour temperature detection, the body of the vendor tisp_awb_ct_detect_simd_3
 * (0x209b8, 4824 lines of MXU2 in the recovered C), ported phase by phase.  Each phase is
 * checked against Matt Davis' recovered C (PR #15) by tests/t32_oracle (t32_awbd_oracle),
 * which stops the vendor function at the phase boundary and compares the state there.
 *
 * Vendor context: tisp_awb_long_par_update builds a table of 47 pointers (a0 of the vendor
 * function).  All of them are field addresses inside par / state, except slot 16 (parameter
 * table) and slots 8 / 12 (vec / pair); ours addresses the fields by name, so the table
 * itself is not needed.  Slot -> field: 0 par+3322, 4 st.sel_a, 8 st.vec, 12 st.pair,
 * 16 par+tbl, 20 par+3184 (zones_h at +2, zones_w at +6), 24 ct_axis_r, 28 ct_axis_b,
 * 32 ct_grid, 36 zone_flag, 40 par+244, 44 ct, 48 gw_tab, 52 fp_shift (out_shift at +2),
 * 56 gw_en, 60 sat_mode, 64 par+292, 68 par+3362, 72 st.spec_flags, 76 spec_ctl,
 * 80 spec_r0, 84 spec_b0, 88 ct_lim, 92 par+5166, 96 cal_r, 100 st+29800, 104 par+5206,
 * 108 par+4246-based, 112 par+5226, 116 par+5234, 120 lim_a, 124 lim_b, 128 par+5682,
 * 132 st (the statistics zstat), 136 zw_raw, 140 st+24576, 144 st+79228, 148 st+80012,
 * 152 st+80... (see tisp_awb_long_par_update 0x3d158), 172 gw_map, 184 zw_norm.
 *
 * Phase 0: working set set-up, gray world fallback, normalisation of the zone values, clamp
 * of the statistics to the colour temperature grid (t32_awb_ct_detect_prep).
 * Phase 1: the zone loop up to L_21800 - cell of the ct grid per zone, per cell sums, the
 * colour temperature of the zone and its weight (t32_awb_ct_detect_zones).
 * Phase 2: the reference points - zones near one of the sel_a points (par+3322) raise their
 * gw_map entry to the class of the distance, near one of the sel_b points (par+3362) lower
 * it to 256 minus the class (t32_awb_ct_detect_refs), up to L_21a78 where the MXU2 part starts.
 * Phase 3 (L_21a78 .. L_21b04): with sat_mode 1 or 2 the gw_map entries are multiplied by zt2 and
 * divided by its maximum (t32_awb_ct_detect_norm).
 * Phase 4 (L_21b04 .. L_21f18, cand_mode 1 only): the 12 fullest cells of the ct grid, five
 * (r, b) points for each, duplicates removed (t32_awb_ct_detect_cand).  The other branch
 * (cand_mode != 1) skips phases 4 to 9.
 * Phase 5 (L_21f18 .. L_22184): every candidate point moves to the mean of the zone statistics
 * around it until it stops moving (t32_awb_ct_detect_refine).
 * Phase 6 (L_22184 .. L_22468): candidate points within a radius of the best one are merged
 * into a weighted mean, slot by slot (t32_awb_ct_detect_cluster).
 * Phase 7 (L_22468 .. L_22688): count and weigh the zones around each merged point
 * (t32_awb_ct_detect_zonesum).
 * Phase 8 (L_22688 .. L_227ac): mean zone weights, counts scaled by them (t32_awb_ct_detect_weigh).
 * Phase 9 (L_227ac .. L_21d24): each zone's weight scaled by its strongest merged point
 * (t32_awb_ct_detect_nearest).
 * Phase 10 (L_22924 .. L_22af8; also the whole cand_mode != 1 branch, which skips phases 4..9):
 * the spec samples lower the weights near them (t32_awb_ct_detect_spec).
 * Phase 11 (L_22af8 .. L_22e34): the weighted sums over the zones (t32_awb_ct_detect_sums).
 * Phase 12 (L_22e34 .. L_23398): mean r, b, and with par+244 == 1 the zones far from it weighted
 * down and the mean taken again (t32_awb_ct_detect_mean).
 * Phase 13 (L_23398 .. L_236d8): with par+316 == 1 the zones are sorted into the ct limit rectangles
 * (t32_awb_ct_detect_limits); otherwise, or when ended, Ct_Cal and the output (t32_awb_ct_detect_finish).
 * Phase 14 (L_236d8 .. L_23a7c): mean of each rectangle, near-mean zone counts, rectangles sorted by
 * falling count (t32_awb_ct_detect_cells).  Phase 15 (L_23a7c .. L_23c10): two percentages of the
 * over-limit zones mapped through two ramps (t32_awb_ct_detect_ramps).
 * Phase 16 (L_23c10 .. L_23f70): how many of the fullest rectangles are taken (par+5694 mode), and the
 * first lim_a / lim_b entries extended or shifted accordingly (t32_awb_ct_detect_pick).
 * Phase 17 (L_23f70 .. L_24140): the two limit tables blended by the ramp results, the par+5684 flag, sums over
 * the taken rectangles, their original indices sorted (t32_awb_ct_detect_blend).
 */
#ifndef TX_ISP_T32_AWBDET_H
#define TX_ISP_T32_AWBDET_H

#include "tx_isp_t32_awbct.h"

/* Working set of one detection.  The vendor keeps the arrays as .bss scratch
 * (t32_data_dc02c, dc004, dbfdc, dbfb4, dbf8c, dbf64, dbf3c) cleared on every call. */
struct t32_awb_ctd {
	u32 acc[7][10];		/* ten accumulators each (filled by later phases) */
	u8 idx[10];		/* 0 .. 9 (rodata bf150) */
	u16 lim_a[10];		/* t32_data_dbf28: the first three entries of st.lim_a, the rest is left alone (.bss of the vendor) */
	u16 lim_b[10];		/* t32_data_dbf14: the same for st.lim_b */
	u16 tab_b[4];		/* the next four entries of lim_a / lim_b */
	u16 tab_c[4];
	u32 nlim;		/* number of leading non-zero ct_lim entries, 0 when all ten are */
	u32 unit;		/* 1000000 << out_shift */
	u32 half;		/* 1 << (out_shift - 1) */
	u32 w;			/* t32_data_dbf10: weight of the last zone, 1.0 at 16 bits when none */
	u16 zwt[1024];		/* t32_data_db710: weight of each zone >> 6 */
	u32 prod[32];		/* t32_data_db690: products of one row (phase 3) */
	u32 tx[60], ty[60], tn[60], tidx[60];	/* t32_data_db5a0 / db4b0 / db3c0 / db2d0: the non-empty candidate points (phase 6) */
	u32 m;			/* number of valid entries of those four */
	u32 zx[32], zy[32], zw[32];	/* t32_data_db250 / db1d0 / db150: the zones of a row with a gw_map weight (phase 7) */
	u32 m2;			/* number of valid entries of those three */
	u32 cn0[60];		/* t32_data_db060: cl_n before the weighting (phase 8) */
	u32 maxn;		/* largest cl_n after it, local s8+64 (0 when none) */
	u32 tw[4], tb[4], tr[4];	/* t32_data_db000 / db010 / db020: lane sums of W, B * W, R * W (phase 11) */
	u32 sw, sb, sr;		/* their totals, locals s8+68 / s8+72 / s8+76 */
	u32 mr, mb;		/* mean r, b (s8+116 / s8+120) */
	u32 var, vr, vb;	/* mean squared distance (s8+248) and the weighted r, b sums of the second pass (s8+144 / s8+156) */
	u32 cm[5][10];		/* t32_data_dafd0 / dafa8 / daf80 / daf58 / daf30: mean of acc[1], acc[2], acc[4], acc[5], acc[6] per rectangle (phase 14) */
	u8 ord[10];		/* t32_data_daf24: original index of each (sorted) rectangle */
	u32 tot;		/* sum of the counts, 16 bit */
	u32 ct_a, ct_b;		/* phase 15: ramps of the over-limit percentages (registers a2 / a1 at L_23c10) */
	u32 nb_hi, nr_hi, nrb;	/* phase 13 counters, locals s8+112 / s8+124 / s8+108 */
	u32 ran;		/* 0: no variance pass, 1: first pass only (var 0), 2: both */
	u32 s0;			/* phase 16: number of rectangles taken (register s0) */
	u32 omax, omin;		/* phase 16: largest / smallest original index among them (registers v1 / a0; 0 / 12 when not computed) */
	u16 bl[10];		/* t32_data_daf10: lim_a / lim_b blended by ct_a, then by ct_b (phase 17) */
	u32 sums[5];		/* phase 17: over the s0 rectangles the sums of acc[3], acc[0], cm[2], cm[3], cm[4] (registers v1, a3, t0, t1, t2) */
};

/*
 * Gray world fallback of the vendor (L_22ddc / L_22de4): both validity flags 1, ct 5000,
 * both gains 1.0 and the gray world detection fills in the result.
 */
static inline void t32_awb_ct_detect_fallback(struct t32_awb *a, u32 out[2], u32 valid[2])
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;

	valid[0] = valid[1] = 1;
	par->ct = 5000;
	out[0] = out[1] = 1u << (par->fp_shift & 31);
	t32_awb_grayworld(par, st, st->zstat[0][0], &valid[0], out);
}

/*
 * Phase 0 up to the zone loop.  Returns 0 when the detection ended in the gray world
 * fallback (gw_en[3] == 1, gw_en[1] == 2, or all zone values zero), 1 to continue, -1 for
 * a grid of more than 32 rows or columns (not compared with the vendor).
 */
static inline int t32_awb_ct_detect_prep(struct t32_awb *a, struct t32_awb_ctd *d, u32 out[2], u32 valid[2])
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 fp = par->fp_shift, os = par->out_shift;
	u32 rows = par->zones_h, cols = par->zones_w;
	u32 row, col, i, zmax;

	if (rows > 32 || cols > 32)
		return -1;		/* tables have 32 x 32 zones; the vendor would run past them */

	for (i = 0; i < 70; i++)
		d->acc[i / 10][i % 10] = 0;
	for (i = 0; i < 10; i++)
		d->idx[i] = (u8)i;
	/* the vendor first copies the constants {10,20,40,80} / {0,0,102,1024} to tab_b / tab_c;
	 * they are overwritten completely below */
	for (i = 0; i < 3; i++) {
		d->lim_a[i] = st->lim_a[i];
		d->lim_b[i] = st->lim_b[i];
	}
	for (i = 0; i < 4; i++) {
		d->tab_b[i] = st->lim_a[3 + i];
		d->tab_c[i] = st->lim_b[3 + i];
	}
	d->nlim = 0;
	for (i = 0; i < 10; i++) {
		if (!par->ct_lim[2 * i]) {
			d->nlim = i & 0xff;
			break;
		}
	}
	d->unit = 1000000u << (os & 31);
	d->half = 1u << ((os - 1) & 31);

	if (par->gw_en[3] == 1) {
		t32_awb_ct_detect_fallback(a, out, valid);
		return 0;
	}

	/* largest raw value of the zone grid, then each value scaled to fp bits by it */
	zmax = st->zw_raw[0];
	for (row = 0; row < rows; row++)
		for (col = 0; col < cols; col++)
			if (zmax < st->zw_raw[row * 32 + col])
				zmax = st->zw_raw[row * 32 + col];
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			if (par->gw_en[1] == 2 || !zmax) {
				st->zw_norm[row * 32 + col] = 0;
				t32_awb_ct_detect_fallback(a, out, valid);
				return 0;
			}
			st->zw_norm[row * 32 + col] = t32_awb_udiv(st->zw_raw[row * 32 + col] << (fp & 31), zmax);
		}
	}

	/* the mode 0 red and blue statistics limited to the ends of the colour temperature axes */
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			u32 lo_r = par->ct_axis_r[0] << (os & 31), hi_r = par->ct_axis_r[14] << (os & 31);
			u32 lo_b = par->ct_axis_b[0] << (os & 31), hi_b = par->ct_axis_b[14] << (os & 31);
			u32 *r = &st->zstat[0][0][row * 32 + col], *b = &st->zstat[0][1][row * 32 + col];

			if (hi_r < *r)
				*r = hi_r;
			else if (*r < lo_r)
				*r = lo_r;
			if (hi_b < *b)
				*b = hi_b;
			else if (*b < lo_b)
				*b = lo_b;
		}
	}
	return 1;
}

/* Cell of an axis for a value: 0 outside [axis[0], axis[14]] (scaled), 15 at the end, else 1 .. 14 */
static inline u32 t32_awb_ct_cell(const u32 axis[15], u32 v, u32 s)
{
	if (v < axis[0] << (s & 31) || axis[14] << (s & 31) < v)
		return 0;
	if (v == axis[14] << (s & 31))
		return 15;
	return t32_awb_axis_index((const u8 *)axis, 0, v, s);
}

/* grid value at (row, col) of a 15 x 15 table of u32 */
#define T32_AWB_G(tab, row, col) ((tab)[(row) * 15 + (col)])

/*
 * Interpolated value of a ct grid at (r, b), the vendor's four cases by where the cell is:
 * (ir, ib) = bucket of r, b in 1 .. 15; 15 = on the last axis point.  Same scheme as
 * t32_awb_ct_cal, but fixed to the cells (the buckets are given).  tab: 15 x 15 u32.
 */
static inline u32 t32_awb_ct_grid_at(const struct t32_awb_par *par, const u32 *tab, u32 r, u32 b, u32 ir, u32 ib,
				     u32 os)
{
	const u32 *ar = par->ct_axis_r, *ab = par->ct_axis_b;

	if (ir == 15 && ib == 15)
		return tab[224] << (os & 31);
	if (ir == 15)			/* along b at the last r point */
		return t32_awb_interp1(os, b, ab[ib - 1], ab[ib], T32_AWB_G(tab, ib - 1, 14), T32_AWB_G(tab, ib, 14));
	if (ib == 15)			/* along r in the last b row */
		return t32_awb_interp1(os, r, ar[ir - 1], ar[ir], T32_AWB_G(tab, 14, ir - 1), T32_AWB_G(tab, 14, ir));
	{
		u32 lo = t32_awb_interp1(os, r, ar[ir - 1], ar[ir], T32_AWB_G(tab, ib - 1, ir - 1), T32_AWB_G(tab, ib - 1, ir));
		u32 hi = t32_awb_interp1(os, r, ar[ir - 1], ar[ir], T32_AWB_G(tab, ib, ir - 1), T32_AWB_G(tab, ib, ir));

		return t32_awb_interp2(os, b, ab[ib - 1], ab[ib], lo, hi);
	}
}

/*
 * Phase 1: for every zone of the grid (zones_h x zones_w, 32 per row):
 *  - the cell (ir, ib) of the 14 x 14 colour temperature grid that holds its (r, b) statistics
 *    (mode 0, as limited by phase 0) and the per cell sums cell_r / cell_b / cell_n;
 *  - the value of the parameter table (par + 364 or 1264, `tbl`) at (r, b) goes to gw_map;
 *  - when the weight kind pair[0] is 1 the colour temperature of the zone (1e6 / ct grid value
 *    at (r, b)) is weighed against the four thresholds st.vec with base pair[1] (zone_ct_weight)
 *    and the product with the table value replaces the gw_map entry; w / 64 goes to zwt.
 * The vendor prints a diagnostic when the ct grid value is zero (not ported).  When the
 * axes do not rise (the clamp of phase 0 can then leave a statistic outside them) the vendor
 * indexes its cell tables with a negative cell and writes behind the state: -1, nothing is
 * written by this phase.
 */
static inline int t32_awb_ct_detect_zones(struct t32_awb *a, struct t32_awb_ctd *d, const u32 *tbl)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	const u32 *cg = (const u32 *)(par->raw + 2164);
	u32 fp = par->fp_shift, osu = par->out_shift, os = osu & 31;	/* fixdiv / fixmul get the unmasked shifts */
	u32 rows = par->zones_h, cols = par->zones_w;
	u32 row, col;

	if (par->ct_axis_r[14] << os < par->ct_axis_r[0] << os || par->ct_axis_b[14] << os < par->ct_axis_b[0] << os)
		return -1;

	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			u32 z = row * 32 + col, r = st->zstat[0][0][z], b = st->zstat[0][1][z];
			u32 ir = t32_awb_ct_cell(par->ct_axis_r, r, os), ib = t32_awb_ct_cell(par->ct_axis_b, b, os);
			u32 cell = ((ib > 14 ? 14 : ib) - 1) * 14 + ((ir > 14 ? 14 : ir) - 1), v;

			st->cell_n[cell]++;
			st->cell_r[cell] += r;
			st->cell_b[cell] += b;
			d->w = 0x10000;
			v = t32_awb_ct_grid_at(par, tbl, r, b, ir, ib, os);
			if (st->pair[0] == 1) {
				u32 grid = t32_awb_ct_grid_at(par, cg, r, b, ir, ib, os), ct;

				if (ir == 15 && ib == 15) {
					ct = t32_awb_udiv(1000000u, cg[224]);	/* unscaled, unlike the other cells */
				} else {
					u32 q = t32_awb_fixdiv(osu, d->unit, grid);

					ct = (q + d->half) >> os;
				}
				v = t32_awb_zone_ct_weight(ct, st->vec, st->pair[1], fp, v, &d->w);
			}
			st->gw_map[z] = v;
			d->zwt[z] = (u16)(d->w >> 6);
		}
	}
	return 0;
}

/* class of the squared distance d between a zone and a reference point (as in the gray world refine pass) */
static inline u32 t32_awb_dist_class(const struct t32_awb_state *st, u32 d)
{
	if (d >= 818)
		return 0;
	if (d >= 679)
		return 1;
	if (d >= 615)
		return 2;
	if (d >= 572)
		return 3;
	if (d >= 540)
		return 4;
	if (d >= 514)
		return 5;
	return st->gw_tab[d];
}

static inline u32 t32_awb_absdiff(u32 a, u32 b)
{
	return a < b ? b - a : a - b;
}

/*
 * Phase 2.  The reference points are (r, b) pairs of u16 (stride 4 bytes, ten of them at
 * most; par+3322 for the sel_a points, par+3362 for the sel_b points).  Per zone and point
 * d = dr^2 + db^2 of the zone statistics (rounded, in units of the axes) from the point; with
 * c = class(d): sel_a points: gw_map = max(gw_map, c << os); sel_b points:
 * gw_map = min(gw_map, (256 - c) << os).  More than ten points: -1 (the vendor reads on
 * behind the table).
 */
static inline int t32_awb_ct_detect_refs(struct t32_awb *a, const struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	const u16 *ref[2] = { (const u16 *)(par->raw + 3322), (const u16 *)(par->raw + 3362) };
	u32 n[2] = { st->sel_a, st->sel_b };
	u32 os = par->out_shift & 31, rows = par->zones_h, cols = par->zones_w;
	u32 set, i, row, col;

	if (n[0] > 10 || n[1] > 10 || rows > 32 || cols > 32)
		return -1;
	for (set = 0; set < 2; set++) {
		for (i = 0; i < n[set]; i++) {
			for (row = 0; row < rows; row++) {
				for (col = 0; col < cols; col++) {
					u32 z = row * 32 + col;
					u32 dr = t32_awb_absdiff((d->half + st->zstat[0][0][z]) >> os, ref[set][2 * i]);
					u32 db = t32_awb_absdiff((d->half + st->zstat[0][1][z]) >> os, ref[set][2 * i + 1]);
					u32 c = t32_awb_dist_class(st, dr * dr + db * db);

					if (set == 0) {
						if (st->gw_map[z] < c << os)
							st->gw_map[z] = c << os;
					} else {
						if ((256 - c) << os < st->gw_map[z])
							st->gw_map[z] = (256 - c) << os;
					}
				}
			}
		}
	}
	return 0;
}

/* the vendor's vector fixed point product of one lane: (x * y) with s fraction bits */
static inline u32 t32_awb_lane_mul(u32 s, u32 x, u32 y)
{
	u32 m = 0xffffffffu >> ((0u - s) & 31), sh = s & 31;
	u32 xh = x >> sh, yh = y >> sh, xl = x & m, yl = y & m;

	return ((xh * yh) << sh) + xh * yl + xl * yh + ((xl * yl) >> sh);
}

/*
 * Phase 3 (MXU2 in the vendor, four lanes at a time).  Only for sat_mode 1 and 2.  The largest
 * zt2 entry of the grid is found; when it is at least 2 every gw_map entry becomes
 * gw_map * zt2 / max (the product in fixed point, the division fix_point_div_32).  The vendor
 * computes the products of a row in groups of four lanes, the last group starting at
 * cols & ~3: it also stores the lanes behind the row, which for a 32 column grid land in the
 * first eight zwt entries (the tables are adjacent).  Returns -1 for a grid over 32, else 0.
 */
static inline int t32_awb_ct_detect_norm(struct t32_awb *a, struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 fp = par->fp_shift, rows = par->zones_h, cols = par->zones_w;
	u32 zmax = 0, row, col, tail = cols & ~3u;
	u32 *gw = (u32 *)(st->raw + 87108), *t2 = (u32 *)(st->raw + 24576);

	if ((u16)(par->sat_mode - 1) >= 2)
		return 0;
	if (rows > 32 || cols > 32)
		return -1;
	for (row = 0; row < rows; row++)
		for (col = 0; col < cols; col++)
			if (zmax < t2[row * 32 + col])
				zmax = t2[row * 32 + col];
	if (zmax < 2)
		return 0;
	for (row = 0; row < rows; row++) {
		for (col = 0; col < tail + 4; col++) {
			u32 z = row * 32 + col, v = t32_awb_lane_mul(fp, gw[z], t2[z]);

			if (col < 32)
				d->prod[col] = v;
			else {			/* lands in zwt (db710 follows db690) */
				d->zwt[2 * (col - 32)] = (u16)v;
				d->zwt[2 * (col - 32) + 1] = (u16)(v >> 16);
			}
		}
		for (col = 0; col < cols; col++)
			gw[row * 32 + col] = t32_awb_fixdiv(fp, d->prod[col], zmax);
	}
	return 0;
}

/*
 * Phase 4.  cand_mode must be 1 (else 1 is returned: phases 4 to 9 are skipped).
 * Twelve times the fullest cell of the 14 x 14 grid (cell_n, strictly more than the stored
 * count of this slot, which is kept from the previous frame) is taken into top_x / top_y / top_n and
 * its counter cleared.  Each slot gives five points (cell corners (x, y), (x, y+1), centre,
 * (x+1, y), (x+1, y+1)) from the ct axes; a point equal to an earlier one is cleared to
 * (0, 0).  Stale slots with x or y over 13 would index behind the axes: -1.
 */
static inline int t32_awb_ct_detect_cand(struct t32_awb *a)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	const u32 *ar = par->ct_axis_r, *ab = par->ct_axis_b;
	u32 k, x, y, i, j;

	if (par->cand_mode != 1)
		return 1;
	for (k = 0; k < 12; k++)
		if (st->top_x[k] > 13 || st->top_y[k] > 13)
			return -1;
	for (k = 0; k < 12; k++) {
		for (y = 0; y < 14; y++) {
			for (x = 0; x < 14; x++) {
				u32 n = st->cell_n[y * 14 + x];

				if (n && st->top_n[k] < n) {
					st->top_x[k] = x;
					st->top_y[k] = y;
					st->top_n[k] = n;
				}
			}
		}
		st->cell_n[(st->top_y[k] * 14 + st->top_x[k]) & 0xffff] = 0;
	}
	for (k = 0; k < 12; k++) {
		u32 *r = &st->pt_r[5 * k], *b = &st->pt_b[5 * k];

		x = st->top_x[k];
		y = st->top_y[k];
		r[0] = ar[x];			b[0] = ab[y];
		r[1] = ar[x];			b[1] = ab[y + 1];
		r[2] = (ar[x] + 1 + ar[x + 1]) >> 1;	b[2] = (ab[y] + 1 + ab[y + 1]) >> 1;
		r[3] = ar[x + 1];		b[3] = ab[y];
		r[4] = ar[x + 1];		b[4] = ab[y + 1];
	}
	for (i = 0; i < 60; i++) {
		if (!st->pt_r[i] || !st->pt_b[i])
			continue;
		for (j = i + 1; j < 60; j++)
			if (st->pt_r[i] == st->pt_r[j] && st->pt_b[i] == st->pt_b[j])
				st->pt_r[j] = st->pt_b[j] = 0;
	}
	return 0;
}

/*
 * Phase 5 (L_21f18 .. L_22184, MXU2 in the vendor).  Every candidate point (r, b) of the
 * 60 (empty when either is 0) is moved to the mean of the zones whose statistics (rounded,
 * in axis units) lie within cand_r2 of it (squared distance, 32 bit): with n such zones
 * the new point is ((sum + n / 2) / n) >> out_shift per axis.  n == 0 leaves the point.
 * The point has converged when both axes moved by at most cand_tol; otherwise, as long as
 * the pass count (1 for the first) does not exceed cand_iter, it is replaced and the zones
 * are taken again.  pt_n gets n of the last pass.  The vendor sums four lanes at a time
 * with the tail lanes masked, which is every zone of rows x cols.  A cand_iter above
 * 65534 would let the vendor's 16 bit pass count wrap: -1, as for a grid over 32.
 */
static inline int t32_awb_ct_detect_refine(struct t32_awb *a)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 os = par->out_shift & 31, half = 1u << ((par->out_shift - 1) & 31);
	u32 rows = par->zones_h, cols = par->zones_w;
	u32 k, row, col, pass;

	if (rows > 32 || cols > 32 || par->cand_iter > 0xfffe)
		return -1;
	for (k = 0; k < 60; k++) {
		if (!st->pt_r[k] || !st->pt_b[k])
			continue;
		for (pass = 1;; pass++) {
			u32 sr = 0, sb = 0, n = 0, nr, nb;

			for (row = 0; row < rows; row++) {
				for (col = 0; col < cols; col++) {
					u32 z = row * 32 + col, r = st->zstat[0][0][z], b = st->zstat[0][1][z];
					u32 dr = t32_awb_absdiff(st->pt_r[k], (u32)((int)(half + r) >> os));
					u32 db = t32_awb_absdiff(st->pt_b[k], (u32)((int)(half + b) >> os));

					if (db * db + dr * dr <= par->cand_r2) {
						sr += r;
						sb += b;
						n++;
					}
				}
			}
			if (!n)
				break;
			nr = ((sr + (n >> 1)) / n) >> os;
			nb = ((sb + (n >> 1)) / n) >> os;
			st->pt_n[k] = n;
			if (t32_awb_absdiff(st->pt_r[k], nr) <= par->cand_tol && t32_awb_absdiff(st->pt_b[k], nb) <= par->cand_tol)
				break;
			if (par->cand_iter < pass)
				break;
			st->pt_r[k] = nr;
			st->pt_b[k] = nb;
		}
	}
	return 0;
}

/*
 * Phase 6 (L_22184 .. L_22468, MXU2 in the vendor).  Merging the candidate points.  For up to
 * 60 slots k of cl_x / cl_y / cl_n: the candidate with the largest pt_n that exceeds cl_n[k]
 * (cl_n is kept from the previous frame; the first one wins a tie) is taken into the slot; a
 * slot still at 0 ends the pass.  The candidates with a non-zero pt_n are listed (tx, ty, tn,
 * tidx) and those within cl_r2 (squared) of the slot's point are combined: weight sum, and the
 * mean of their r and b weighted by pt_n, rounded, becomes the slot's point and cl_n[k] is
 * cleared; the merged candidates get pt_n 0.  When none is within reach the slot keeps the
 * candidate and its weight.
 */
static inline void t32_awb_ct_detect_cluster(struct t32_awb *a, struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 k, i, j;

	d->m = 0;
	for (k = 0; k < 60; k++) {
		u32 sn = 0, sx = 0, sy = 0;

		for (i = 0; i < 60; i++) {
			if (st->pt_n[i] && st->cl_n[k] < st->pt_n[i]) {
				st->cl_x[k] = st->pt_r[i];
				st->cl_y[k] = st->pt_b[i];
				st->cl_n[k] = st->pt_n[i];
			}
		}
		if (!st->cl_n[k])
			return;
		d->m = 0;
		for (i = 0; i < 60; i++) {
			if (!st->pt_n[i])
				continue;
			d->tx[d->m] = st->pt_r[i];
			d->ty[d->m] = st->pt_b[i];
			d->tn[d->m] = st->pt_n[i];
			d->tidx[d->m] = i;
			d->m++;
		}
		for (j = 0; j < d->m; j++) {
			u32 dx = t32_awb_absdiff(st->cl_x[k], d->tx[j]), dy = t32_awb_absdiff(st->cl_y[k], d->ty[j]);

			if (dy * dy + dx * dx <= par->cl_r2) {
				sn += d->tn[j];
				sx += d->tn[j] * d->tx[j];
				sy += d->tn[j] * d->ty[j];
				st->pt_n[d->tidx[j]] = 0;
			}
		}
		if (!sn)
			continue;
		st->cl_n[k] = 0;
		st->cl_x[k] = (sx + (sn >> 1)) / sn;
		st->cl_y[k] = (sy + (sn >> 1)) / sn;
	}
}

/*
 * Phase 7 (L_22468 .. L_22688, MXU2 in the vendor).  For every merged point (cl_x and cl_y both
 * non-zero): over the rows of the zone grid the zones with a non-zero gw_map weight (zx, zy, zw:
 * the zone statistics and the weight) are taken, and those whose statistics, rounded and in axis
 * units, lie within zr2 (squared) of the point are counted into cl_n (which restarts at 0) and
 * their weights summed into cl_w.  Returns -1 for a grid over 32.
 */
static inline int t32_awb_ct_detect_zonesum(struct t32_awb *a, struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 os = par->out_shift & 31, half = 1u << ((par->out_shift - 1) & 31);
	u32 rows = par->zones_h, cols = par->zones_w;
	u32 k, row, col, j;

	if (rows > 32 || cols > 32)
		return -1;
	d->m2 = 0;
	for (k = 0; k < 60; k++) {
		if (!st->cl_x[k] || !st->cl_y[k])
			continue;
		st->cl_n[k] = 0;
		st->cl_w[k] = 0;
		for (row = 0; row < rows; row++) {
			u32 n = 0, cnt = 0, sw = 0;

			for (col = 0; col < cols; col++) {
				u32 z = row * 32 + col;

				if (!st->gw_map[z])
					continue;
				d->zx[n] = st->zstat[0][0][z];
				d->zy[n] = st->zstat[0][1][z];
				d->zw[n] = st->gw_map[z];
				n++;
			}
			d->m2 = n;
			for (j = 0; j < n; j++) {
				u32 dy = t32_awb_absdiff(st->cl_y[k], (u32)((int)(half + d->zy[j]) >> os));
				u32 dx = t32_awb_absdiff(st->cl_x[k], (u32)((int)(half + d->zx[j]) >> os));

				if (dy * dy + dx * dx <= par->zr2) {
					cnt++;
					sw += d->zw[j];
				}
			}
			st->cl_n[k] += cnt;
			st->cl_w[k] += sw;
		}
	}
	return 0;
}

/*
 * Phase 8 (L_22688 .. L_227ac).  Weights of the merged points.  cl_w becomes the mean zone
 * weight (rounded), cl_n the zone count scaled by cl_w / (largest cl_w) (rounded; as is when that is
 * 0), then blended with the count before the scaling, cl_mix : 256 - cl_mix, >> 8.  The
 * unscaled counts are kept in cn0.  Returns 1 when no point is left (all cl_n 0): the vendor
 * then continues at L_21d28 (phase 10 directly).
 */
static inline int t32_awb_ct_detect_weigh(struct t32_awb *a, struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 mix = par->cl_mix, mx = 0, i;

	for (i = 0; i < 60; i++)
		if (st->cl_n[i])
			st->cl_w[i] = (st->cl_w[i] + (st->cl_n[i] >> 1)) / st->cl_n[i];
	for (i = 0; i < 60; i++)
		if (st->cl_w[i] && mx < st->cl_w[i])
			mx = st->cl_w[i];
	for (i = 0; i < 60; i++) {
		u32 n = st->cl_n[i];

		d->cn0[i] = n;
		if (mx && n)
			st->cl_n[i] = (n * st->cl_w[i] + (mx >> 1)) / mx;
		st->cl_n[i] = (mix * n + (256 - mix) * st->cl_n[i]) >> 8;
	}
	d->maxn = 0;
	for (i = 0; i < 60; i++)
		if (st->cl_n[i] && d->maxn < st->cl_n[i])
			d->maxn = st->cl_n[i];
	return d->maxn == 0;
}

/*
 * Phase 9 (L_227ac .. L_21d24).  Every zone of the grid (rows x cols): among the first 16
 * merged points the one with the largest cl_n (first on a tie) that lies within zr2 (squared) of
 * the zone's statistics (rounded, in axis units, logical shift) decides the weight: gw_map becomes
 * (cl_n * gw_map + maxn / 2) / maxn, and with no point in reach (gw_map + maxn / 2) / maxn.
 * Needs maxn != 0 (phase 8 returned 0).  Returns -1 for a grid over 32.
 */
static inline int t32_awb_ct_detect_nearest(struct t32_awb *a, const struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 os = par->out_shift & 31, rows = par->zones_h, cols = par->zones_w;
	u32 row, col, j;

	if (rows > 32 || cols > 32)
		return -1;
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			u32 z = row * 32 + col, r = (d->half + st->zstat[0][0][z]) >> os, b = (d->half + st->zstat[0][1][z]) >> os;
			u32 best = 0;

			for (j = 0; j < 16; j++) {
				u32 dx = t32_awb_absdiff(st->cl_x[j], r), dy = t32_awb_absdiff(st->cl_y[j], b);

				if (!st->cl_x[j] || !st->cl_y[j] || dy * dy + dx * dx > par->zr2)
					continue;
				if (!best || st->cl_n[best - 1] < st->cl_n[j])
					best = j + 1;
			}
			if (best)
				st->gw_map[z] = (st->cl_n[best - 1] * st->gw_map[z] + (d->maxn >> 1)) / d->maxn;
			else
				st->gw_map[z] = (st->gw_map[z] + (d->maxn >> 1)) / d->maxn;
		}
	}
	return 0;
}

/*
 * Phase 10 (L_22924 .. L_22af8; entered after phase 9, and straight after phase 3 when cand_mode
 * is not 1).  With few = (fewer than two merged points have cl_n >= 5) and for every spec sample
 * i (st.spec_flags[i] == 2; ratios spec_r0[i] / spec_b0[i], control par+324 + 4 * i) each zone's
 * gw_map is lowered to (256 - c) << out_shift: d = squared distance of the zone statistics (rounded,
 * axis units, logical shift) from the sample; c = 0 for d >= 615, 3 / 4 / 5 for d >= 572 / 540 /
 * 514, and below 514 gw_tab[d], or 256 when d < ctl^2, the latter when cand_mode is 1 and few is 0
 * or cand_mode is 0 (gw_tab otherwise).  Returns 1 when all gw_map entries are 0 (the vendor then ends
 * in the gray world fallback), -1 for a grid over 32, else 0.
 */
static inline int t32_awb_ct_detect_spec(struct t32_awb *a, const struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	const u32 *ctl = (const u32 *)(par->raw + 324);
	const u16 *sr = (const u16 *)(st->raw + 28718), *sb = (const u16 *)(st->raw + 28738);
	u32 os = par->out_shift & 31, rows = par->zones_h, cols = par->zones_w;
	u32 big = 0, few, i, row, col, sum = 0;

	if (rows > 32 || cols > 32)
		return -1;
	for (i = 0; i < 60; i++)
		big += st->cl_n[i] >= 5;
	few = big < 2;
	for (i = 0; i < 10; i++) {
		if (st->spec_flags[i] != 2)
			continue;
		for (row = 0; row < rows; row++) {
			for (col = 0; col < cols; col++) {
				u32 z = row * 32 + col;
				u32 dr = t32_awb_absdiff((d->half + st->zstat[0][0][z]) >> os, sr[i]);
				u32 db = t32_awb_absdiff((d->half + st->zstat[0][1][z]) >> os, sb[i]);
				u32 dd = db * db + dr * dr, c;

				if (dd >= 615)
					c = 0;
				else if (dd >= 572)
					c = 3;
				else if (dd >= 540)
					c = 4;
				else if (dd >= 514)
					c = 5;
				else if (par->cand_mode == 1 ? !few && dd < ctl[i] * ctl[i] : par->cand_mode == 0 && dd < ctl[i] * ctl[i])
					c = 256;
				else
					c = st->gw_tab[dd];
				if ((256 - c) << os < st->gw_map[z])
					st->gw_map[z] = (256 - c) << os;
			}
		}
	}
	for (row = 0; row < rows; row++)
		for (col = 0; col < cols; col++)
			sum += st->gw_map[row * 32 + col];
	return sum == 0;
}

/* (1 << n) - 1 as the vendor builds it: -1 >> (-n & 31) (all ones for n = 0) */
static inline u32 t32_awb_lowmask(u32 n)
{
	return 0xffffffffu >> ((-n) & 31);
}

/*
 * Phase 11 (L_22af8 .. L_22e34).  Clears valid[0] and sums the zones of the grid, four columns
 * at a time (the last group of a row runs past the columns when they are no multiple of 4; those
 * lanes are summed too, as in the vendor).  Per zone z: with m = gw_map[z] >> 8 and g = zw_norm[z]
 * >> 8 the product P = m * g >> out_shift, the weight W = (P * flag[z]) >> out_shift, all in
 * 32 bit halves of out_shift bits each (the MXU2 has no 64 bit multiply), and then, as 32 bit
 * halves of fp_shift bits, R * (W << 8) and B * (W << 8) >> fp_shift for the zone statistics R, B.
 * The lane sums of W, R * W and B * W are added up to sw, sr, sb.  Returns 1 when sw is 0 (the
 * vendor then ends in the gray world fallback), -1 for a grid over 32, else 0.
 */
static inline int t32_awb_ct_detect_sums(struct t32_awb *a, struct t32_awb_ctd *d, u32 valid[2])
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 fp = par->fp_shift & 0xffff, os = par->out_shift & 0xffff;
	u32 fm = t32_awb_lowmask(fp), om = t32_awb_lowmask(os);
	u32 rows = par->zones_h, cols = par->zones_w;
	u32 row, col, l;

	if (rows > 32 || cols > 32)
		return -1;
	valid[0] = 0;
	for (l = 0; l < 4; l++)
		d->tw[l] = d->tb[l] = d->tr[l] = 0;
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col += 4) {
			for (l = 0; l < 4; l++) {
				u32 z = row * 32 + col + l;
				u32 m = st->gw_map[z] >> 8, g = st->zw_norm[z] >> 8;
				u32 mo = m >> (os & 31), go = g >> (os & 31), mr = m & om, gr = g & om;
				u32 f = par->zone_flag[z], fs = f << (os & 31), fl = fs & om, f2 = fs >> (os & 31);
				u32 p, ph, pl, w, ws, wh, wl, r, rw, b, bw;

				p = mr * go + mo * gr + ((mo * go) << (os & 31)) + ((mr * gr) >> (os & 31));
				ph = p >> (os & 31);
				pl = p & om;
				w = pl * f2 + ph * fl + ((ph * f2) << (os & 31)) + ((pl * fl) >> (os & 31));
				ws = w << 8;
				wh = ws >> (fp & 31);
				wl = ws & fm;
				r = st->zstat[0][0][z];
				b = st->zstat[0][1][z];
				rw = (r & fm) * wh + (r >> (fp & 31)) * wl + (((r >> (fp & 31)) * wh) << (fp & 31)) + (((r & fm) * wl) >> (fp & 31));
				bw = (b & fm) * wh + (b >> (fp & 31)) * wl + (((b >> (fp & 31)) * wh) << (fp & 31)) + (((b & fm) * wl) >> (fp & 31));
				d->tw[l] += w;
				d->tr[l] += rw;
				d->tb[l] += bw;
			}
		}
	}
	d->sw = d->sb = d->sr = 0;
	for (l = 0; l < 4; l++) {
		d->sr += d->tr[l];
		d->sb += d->tb[l];
		d->sw += d->tw[l];
	}
	return d->sw == 0;
}

/*
 * Phase 12 (L_22e34 .. L_23398).  Mean r, b = sums / sw (fixdiv with out_shift bits).  With
 * var_en == 1: pass one stores per zone D = dr^2 + db^2 (fix_point_mult2, fp_shift bits; dr, db =
 * distance of the zone statistics from the mean) in zdist and, for the zones with non-zero gw_map,
 * zw_norm and zone_flag, adds D * gw_map * zw_norm * (flag << fp) to s1; var = s1 / sw.  Unless var
 * is 0, pass two normalises Dn = D / var and sets zwt3 = 0 for Dn > var_hi << 6, gw_map for
 * Dn < var_lo << 6, else gw_map * (1 - (Dn - (var_lo << 6)) / ((var_hi - var_lo) << 6));
 * w3 = (zwt3 >> 8) * (zw_norm >> 8) * (flag << out_shift) with out_shift bits is summed to s0 and
 * w3 << 8 weighs r and b into vr, vb; when s0 is not 0 the mean is vr / s0, vb / s0.
 * The vendor prints a message for sw == 0 and var_hi == var_lo (then divides by 0); sw is not 0 here.
 * Returns -1 for a grid over 32, else 0.
 */
static inline int t32_awb_ct_detect_mean(struct t32_awb *a, struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 fp = par->fp_shift, os = par->out_shift;
	u32 rows = par->zones_h, cols = par->zones_w;
	u32 row, col, s1 = 0, s0 = 0, hi, lo, span, one;

	if (rows > 32 || cols > 32)
		return -1;
	d->mr = t32_awb_fixdiv(os, d->sr, d->sw);
	d->mb = t32_awb_fixdiv(os, d->sb, d->sw);
	d->ran = 0;
	if (par->var_en != 1)
		return 0;
	d->ran = 1;
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			u32 z = row * 32 + col;
			u32 dr = t32_awb_absdiff(st->zstat[0][0][z], d->mr), db = t32_awb_absdiff(st->zstat[0][1][z], d->mb);
			u32 dd = t32_awb_fixmul(fp, dr, dr) + t32_awb_fixmul(fp, db, db);

			st->zdist[z] = dd;
			if (!st->gw_map[z] || !st->zw_norm[z] || !par->zone_flag[z])
				continue;
			s1 += t32_awb_fixmul(fp, t32_awb_fixmul3(fp, dd, st->gw_map[z], st->zw_norm[z]), (u32)par->zone_flag[z] << (fp & 31));
		}
	}
	d->var = t32_awb_fixdiv(fp, s1, d->sw);
	if (!d->var)
		return 0;
	d->ran = 2;
	hi = par->var_hi << 6;
	lo = par->var_lo << 6;
	span = (par->var_hi - par->var_lo) << 6;
	one = 1u << (fp & 31);
	d->vr = d->vb = 0;
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			u32 z = row * 32 + col;
			u32 dn = t32_awb_fixdiv(fp, st->zdist[z], d->var), wt, w3;

			if (hi < dn)
				wt = 0;
			else if (dn < lo)
				wt = st->gw_map[z];
			else
				wt = t32_awb_fixmul(fp, st->gw_map[z], one - t32_awb_fixdiv(fp, dn - lo, span));
			st->zwt3[z] = wt;
			w3 = t32_awb_fixmul3(os, wt >> 8, st->zw_norm[z] >> 8, (u32)par->zone_flag[z] << (os & 31));
			d->vr += t32_awb_fixmul(fp, st->zstat[0][0][z], w3 << 8);
			d->vb += t32_awb_fixmul(fp, st->zstat[0][1][z], w3 << 8);
			s0 += w3;
		}
	}
	if (s0) {
		d->mr = t32_awb_fixdiv(os, d->vr, s0);
		d->mb = t32_awb_fixdiv(os, d->vb, s0);
	}
	return 0;
}

/*
 * Phase 13 (L_23398 .. L_236d8), only when the low 16 bits of par+316 (lim_en) are 1, else
 * returns 1 (the vendor then goes to Ct_Cal and the output, t32_awb_ct_detect_finish).
 * ct_lim (par+5126, u16 pairs, value and unused half) and ct_lim2 (par+5166) describe, for
 * k < nlim, the rectangle ct_lim[2k] <= r < ct_lim[2k+1], ct_lim2[2k+1] <= b < ct_lim2[2k]
 * (nlim: phase 0; all ten entries non-zero gives 0).  Per zone, with (r, b) = zs2 (st+29800):
 *  - counters over all zones: nb_hi += ct_lim2[3] < b; nr_hi += ct_lim[2 nlim - 4] < r;
 *    nrb += b != 0 when that holds or r != 0;
 *  - a zone outside the hull ct_lim2[0] >= b > ct_lim2[2 nlim - 1], ct_lim[0] < r <= ct_lim[2 nlim - 1]
 *    gets zlim = nlim; inside it the first rectangle that holds (r, b) gives zlim = k and adds to
 *    acc[0][k] the count, acc[1][k] r, acc[2][k] b, acc[4][k] fixmul(10, zw_norm >> 6, flag << 10),
 *    acc[5][k] gw_map >> 6, acc[6][k] zwt; a zone in the hull without a rectangle keeps its zlim.
 * Returns -1 for a grid over 32 (not compared).
 */
static inline int t32_awb_ct_detect_limits(struct t32_awb *a, struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	const u16 *l1 = (const u16 *)(par->raw + 5126), *l2 = (const u16 *)(par->raw + 5166);	/* ct_lim, ct_lim2; indexed below the start for nlim 0 as the vendor does */
	int n = (int)d->nlim;
	u32 rows = par->zones_h, cols = par->zones_w;
	u32 row, col, k;

	if ((par->lim_en & 0xffff) != 1)
		return 1;
	if (rows > 32 || cols > 32)
		return -1;
	d->nb_hi = d->nr_hi = d->nrb = 0;
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			u32 z = row * 32 + col, r = st->zs2[0][z], b = st->zs2[1][z];
			int above = l1[2 * n - 4] < r;

			d->nb_hi += l2[3] < b;
			d->nr_hi += above;
			if (above || r)
				d->nrb += b != 0;
			if (l2[0] < b || !(l2[2 * n - 1] < b) || !(l1[0] < r) || l1[2 * n - 1] < r) {
				st->zlim[z] = n;
				continue;
			}
			for (k = 0; k < (u32)n; k++)
				if (!(r < l1[2 * k]) && r < l1[2 * k + 1] && !(b < l2[2 * k + 1]) && b < l2[2 * k])
					break;
			if (k == (u32)n)
				continue;
			st->zlim[z] = k;
			d->acc[0][k]++;
			d->acc[1][k] += r;
			d->acc[2][k] += b;
			d->acc[4][k] += t32_awb_fixmul(10, st->zw_norm[z] >> 6, (u32)par->zone_flag[z] << 10);
			d->acc[5][k] += st->gw_map[z] >> 6;
			d->acc[6][k] += d->zwt[z];
		}
	}
	return 0;
}

/* L_24508 .. L_24550: the colour temperature of the mean (Ct_Cal) and the output */
static inline void t32_awb_ct_detect_finish(struct t32_awb *a, const struct t32_awb_ctd *d, u32 out[2], u32 valid[2])
{
	t32_awb_ct_cal(a->par, d->mr, d->mb);
	out[0] = d->mr;
	out[1] = d->mb;
	valid[0] = valid[1] = 0;
}

/*
 * Phase 14 (L_236d8 .. L_23a7c).  nlim rectangles (phase 13), their zone counts acc[0][k]:
 *  - hw16[k] = 0 and the sum of the counts; no zone in any rectangle: returns 1 (Ct_Cal and
 *    the output, t32_awb_ct_detect_finish);
 *  - hw16[k] = count (16 bit) and the mean of each rectangle: cm[0] = r sum / count, cm[1] = b
 *    sum / count, cm[2] = acc[4] / count, cm[3] = acc[5] / count, cm[4] = acc[6] / count (all 0
 *    for an empty one);
 *  - acc[3][k] counts the zones whose zlim is k and whose (r, b) lie within a squared distance
 *    below 1000 of the mean of rectangle k (the vendor reads the mean tables of every zlim, a
 *    zone outside the hull has zlim = nlim and sees the unwritten entry nlim: 0 here, as in the
 *    cleared vendor tables);
 *  - the rectangles sorted by falling count (bubble sort, equal counts keep their order) along
 *    with the means, acc[3] (raised to 1 where 0) and ord (original index).
 * hw16 is the u16 array at hw + 4246.  Returns -1 for a grid over 32 or a zlim above 9 (the
 * vendor indexes its ten entry tables with it).
 */
static inline int t32_awb_ct_detect_cells(struct t32_awb *a, struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u16 *hw16 = (u16 *)(a->hw->raw + 4246);
	u32 rows = par->zones_h, cols = par->zones_w;
	u32 n = d->nlim, sum = 0, row, col, i, j, pass;

	if (rows > 32 || cols > 32)
		return -1;
	for (i = 0; i < n; i++) {
		hw16[i] = 0;
		sum += d->acc[0][i];
	}
	if (!sum)
		return 1;
	for (row = 0; row < rows; row++)
		for (col = 0; col < cols; col++)
			if ((st->zlim[row * 32 + col] & 0xff) > 9)
				return -1;
	for (i = 0; i < n; i++) {
		u32 c = d->acc[0][i];

		hw16[i] = (u16)c;
		d->cm[0][i] = c ? d->acc[1][i] / c : 0;
		d->cm[1][i] = c ? d->acc[2][i] / c : 0;
		d->cm[2][i] = c ? d->acc[4][i] / c : 0;
		d->cm[3][i] = c ? d->acc[5][i] / c : 0;
		d->cm[4][i] = c ? d->acc[6][i] / c : 0;
	}
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			u32 z = row * 32 + col, k = st->zlim[z] & 0xff;
			u32 dr = t32_awb_absdiff(d->cm[0][k], st->zs2[0][z]), db = t32_awb_absdiff(d->cm[1][k], st->zs2[1][z]);

			if (db * db + dr * dr < 1000)
				d->acc[3][k]++;
		}
	}
	d->tot = 0;
	for (i = 0; i < n; i++) {
		d->ord[i] = (u8)i;
		if (!d->acc[3][i])
			d->acc[3][i] = 1;
		d->tot = (d->tot + d->acc[0][i]) & 0xffff;
	}
	for (pass = n - 1; pass; pass--) {
		for (j = 0; j < pass; j++) {
			u32 t, c;
			u8 o;

			if (!(d->acc[0][j] < d->acc[0][j + 1]))
				continue;
			t = d->acc[0][j]; d->acc[0][j] = d->acc[0][j + 1]; d->acc[0][j + 1] = t;
			for (c = 0; c < 5; c++) {
				t = d->cm[c][j]; d->cm[c][j] = d->cm[c][j + 1]; d->cm[c][j + 1] = t;
			}
			t = d->acc[3][j]; d->acc[3][j] = d->acc[3][j + 1]; d->acc[3][j + 1] = t;
			o = d->ord[j]; d->ord[j] = d->ord[j + 1]; d->ord[j + 1] = o;
		}
	}
	return 0;
}

/* Ramp of four (x, y) points with falling x: y[0] above x[0], y[3] at or below x[3], else the line
 * through the interval that holds v (signed 32 bit arithmetic as in the vendor; 0 when none does) */
static inline u32 t32_awb_ramp_down(const u16 *x, const u16 *y, int v)
{
	u32 i, res = 0;

	if ((int)x[0] < v)
		return y[0];
	if (!((int)x[3] < v))
		return y[3];
	for (i = 0; i < 3; i++) {
		u32 w, p;

		if (!((int)x[i + 1] < v) || (int)x[i] < v)
			continue;
		w = (u32)x[i] - x[i + 1];
		p = (u32)y[i] * w + ((u32)y[i + 1] - y[i]) * ((u32)x[i] - (u32)v);
		res = (u32)((int)p / (int)w) & 0xffff;
	}
	return res;
}

/* The same for rising x and an unsigned v. */
static inline u32 t32_awb_ramp_up(const u16 *x, const u16 *y, u32 v)
{
	u32 i, res = 0;

	if (v < x[0])
		return y[0];
	if (!(v < x[3]))
		return y[3];
	for (i = 0; i < 3; i++) {
		u32 w, p;

		if (!(v < x[i + 1]) || v < x[i])
			continue;
		w = (u32)x[i + 1] - x[i];
		p = (u32)y[i] * w + ((u32)y[i + 1] - y[i]) * (v - x[i]);
		res = (u32)((int)p / (int)w) & 0xffff;
	}
	return res;
}

/*
 * Phase 15 (L_23a7c .. L_23c10).  Percentages of the zones over the limits, rounded:
 * pb = (nb_hi * 100 + nrb / 2) / nrb as a signed 16 bit value, pr = (nr_hi * 100 + nrb / 2) / nrb
 * as an unsigned 16 bit one (both 0 when nrb is 0).  ct_a = ramp_down(par+5226, par+5234, pb),
 * ct_b = ramp_up(tab_b, tab_c, pr).
 */
static inline void t32_awb_ct_detect_ramps(struct t32_awb *a, struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	int pb = 0;
	u32 pr = 0;

	if (d->nrb) {
		pb = (short)t32_awb_udiv(d->nb_hi * 100 + (d->nrb >> 1), d->nrb);
		pr = t32_awb_udiv(d->nr_hi * 100 + (d->nrb >> 1), d->nrb) & 0xffff;
	}
	d->ct_a = t32_awb_ramp_down((const u16 *)(par->raw + 5226), (const u16 *)(par->raw + 5234), pb);
	d->ct_b = t32_awb_ramp_up(d->tab_b, d->tab_c, pr);
}

/*
 * Phase 16 (L_23c10 .. L_23f70).  s0 = number of the fullest rectangles (phase 14 order) that are
 * taken, by the u16 par+5694 (mode):
 *  - 0: s0 = 3;  1: the smallest s0 >= 2 such that the sum of the first s0 counts (16 bit) exceeds
 *    (tot * par[5692 u16] + 50) / 100 (signed, 16 bit), or nlim when none does;  other: s0 = 3 and
 *    nothing else happens (omax = 0, omin = 12);
 *  - for mode 0 and 1, s0 drops while the count of rectangle s0 - 1 is 0 (phase 14 guarantees
 *    acc[0][0] != 0), omax / omin are the largest / smallest ord[0 .. s0 - 1] (smallest start 12);
 *  - with s0 < 3 (mode 0, 1): from the first three st.lim_a / lim_b entries hi_a, lo_a (start 0 / 1024)
 *    and hi_b, lo_b: lim_a[0] = hi_a, lim_a[1] = lo_a, lim_b[0] = lo_b, lim_b[1] = hi_b;
 *  - with s0 > 3 (mode 1): lim_a[3 .. s0 - 1] = lo_a, lim_b[0 .. s0 - 4] = lo_b and
 *    lim_b[s0 - 3 .. s0 - 1] = st.lim_b[0 .. 2].
 * Returns -1 when acc[0] is empty (cannot happen after phase 14; the vendor would read below the table).
 */
static inline int t32_awb_ct_detect_pick(struct t32_awb *a, struct t32_awb_ctd *d)
{
	const struct t32_awb_par *par = a->par;
	const struct t32_awb_state *st = a->st;
	u32 mode = *(const u16 *)(par->raw + 5694), pct = *(const u16 *)(par->raw + 5692);
	u32 n = d->nlim, s0 = 3, i, omax = 0, omin = 12, hi_a = 0, lo_a = 1024, hi_b = 0, lo_b = 1024;

	d->omax = 0;
	d->omin = 12;
	if (mode != 0 && mode != 1) {
		d->s0 = 3;
		return 0;
	}
	if (mode == 1) {
		u32 sum = 0, thr = (u32)((int)(d->tot * pct + 50) / 100) & 0xffff;

		s0 = 0;
		for (i = 0; i < n; i++) {
			s0++;
			sum = (sum + d->acc[0][i]) & 0xffff;
			if (thr < sum)
				break;
		}
		if (s0 < 2)
			s0 = 2;
	}
	while (s0 && !d->acc[0][s0 - 1])
		s0--;
	if (!s0)
		return -1;
	for (i = 0; i < s0; i++) {
		if (omax < d->ord[i])
			omax = d->ord[i];
		if (d->ord[i] < omin)
			omin = d->ord[i];
	}
	d->omax = omax;
	d->omin = omin;
	d->s0 = s0;
	if (s0 == 3)
		return 0;
	for (i = 0; i < 3; i++) {
		if (hi_a < st->lim_a[i])
			hi_a = st->lim_a[i];
		if (st->lim_a[i] < lo_a)
			lo_a = st->lim_a[i];
		if (hi_b < st->lim_b[i])
			hi_b = st->lim_b[i];
		if (st->lim_b[i] < lo_b)
			lo_b = st->lim_b[i];
	}
	if (s0 < 3) {
		d->lim_a[0] = (u16)hi_a;
		d->lim_a[1] = (u16)lo_a;
		d->lim_b[0] = (u16)lo_b;
		d->lim_b[1] = (u16)hi_b;
		return 0;
	}
	for (i = 3; i < s0; i++)
		d->lim_a[i] = (u16)lo_a;
	for (i = 0; i + 3 < s0; i++)
		d->lim_b[i] = (u16)lo_b;
	for (i = 0; i < 3; i++)
		d->lim_b[s0 - 3 + i] = st->lim_b[i];
	return 0;
}

/*
 * Phase 17 (L_23f70 .. L_24140).  bl[i] = (lim_a[i] * ct_a + lim_b[i] * (1024 - ct_a)) / 1024 for the ten
 * entries (signed 32 bit as in the vendor, stored 16 bit).  omax - omin (8 bit, unsigned difference) below 2:
 * every bl[i] = 1024; the u16 par+5684 gets 1 when it is 2 or more, else 0.  With the u16 par+5700 == 1 the
 * result is blended again towards 1024 by ct_b: bl[i] = ((1024 - ct_b) * bl[i] + ct_b * 1024) / 1024.
 * sums[] = sums of acc[3], acc[0], cm[2], cm[3], cm[4] over the first s0 rectangles; ord[0 .. s0 - 1] sorted
 * ascending (bubble sort, strict) together with idx.
 */
static inline void t32_awb_ct_detect_blend(struct t32_awb *a, struct t32_awb_ctd *d)
{
	struct t32_awb_par *par = a->par;
	u32 s0 = d->s0, i, j, diff, flag;

	for (i = 0; i < 10; i++)
		d->bl[i] = (u16)((int)((u32)d->lim_a[i] * d->ct_a + (u32)d->lim_b[i] * (1024 - d->ct_a)) / 1024);
	diff = (d->omax < d->omin ? d->omin - d->omax : d->omax - d->omin) & 0xff;
	flag = diff >= 2;
	if (!flag)
		for (i = 0; i < 10; i++)
			d->bl[i] = 1024;
	*(u16 *)(par->raw + 5684) = (u16)flag;
	if (*(const u16 *)(par->raw + 5700) == 1)
		for (i = 0; i < 10; i++)
			d->bl[i] = (u16)((int)((1024 - d->ct_b) * d->bl[i] + (d->ct_b << 10)) / 1024);
	for (i = 0; i < 5; i++)
		d->sums[i] = 0;
	for (i = 0; i < s0; i++) {
		d->sums[0] += d->acc[3][i];
		d->sums[1] += d->acc[0][i];
		d->sums[2] += d->cm[2][i];
		d->sums[3] += d->cm[3][i];
		d->sums[4] += d->cm[4][i];
	}
	for (i = s0 - 1; i; i--) {
		for (j = 0; j < i; j++) {
			u8 t;

			if (!(d->ord[j + 1] < d->ord[j]))
				continue;
			t = d->ord[j]; d->ord[j] = d->ord[j + 1]; d->ord[j + 1] = t;
			t = d->idx[j]; d->idx[j] = d->idx[j + 1]; d->idx[j + 1] = t;
		}
	}
}

#endif /* TX_ISP_T32_AWBDET_H */
