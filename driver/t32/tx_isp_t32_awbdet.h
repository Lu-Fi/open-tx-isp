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
 * (cand_mode != 1, L_22924) is not ported.
 * Phase 5 (L_21f18 .. L_22184): every candidate point moves to the mean of the zone statistics
 * around it until it stops moving (t32_awb_ct_detect_refine).
 */
#ifndef TX_ISP_T32_AWBDET_H
#define TX_ISP_T32_AWBDET_H

#include "tx_isp_t32_awbct.h"

/* Working set of one detection.  The vendor keeps the arrays as .bss scratch
 * (t32_data_dc02c, dc004, dbfdc, dbfb4, dbf8c, dbf64, dbf3c) cleared on every call. */
struct t32_awb_ctd {
	u32 acc[7][10];		/* ten accumulators each (filled by later phases) */
	u8 idx[10];		/* 0 .. 9 (rodata bf150) */
	u16 lim_a[3];		/* first three entries of st.lim_a (t32_data_dbf28) */
	u16 lim_b[3];		/* first three entries of st.lim_b (t32_data_dbf14) */
	u16 tab_b[4];		/* the next four entries of lim_a / lim_b */
	u16 tab_c[4];
	u32 nlim;		/* number of leading non-zero ct_lim entries, 0 when all ten are */
	u32 unit;		/* 1000000 << out_shift */
	u32 half;		/* 1 << (out_shift - 1) */
	u32 w;			/* t32_data_dbf10: weight of the last zone, 1.0 at 16 bits when none */
	u16 zwt[1024];		/* t32_data_db710: weight of each zone >> 6 */
	u32 prod[32];		/* t32_data_db690: products of one row (phase 3) */
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
 * Phase 4.  cand_mode must be 1 (else 1 is returned: the other branch is not ported).
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

#endif /* TX_ISP_T32_AWBDET_H */
