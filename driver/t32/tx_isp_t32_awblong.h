/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AWB long algorithm, the vendor tisp_awb_long_alogrithm (0x3d85c, 1077 lines of recovered C), ported phase by phase.
 * Each phase is checked against Matt Davis' recovered C (PR #15) by its own oracle in tests/t32_oracle, which stops the
 * vendor function at the phase boundary (trunc_long.sh, env T32O_STOP=label) and compares the state there.
 *
 * Vendor arguments: a0 = the table of 47 context pointers (all constant field addresses, see tx_isp_t32_awbdet.h), a1 = the
 * channel.  Ours takes the three context areas (struct t32_awb) directly.
 *
 * Phase 1 (L_3d85c .. L_3dbfc): entry values, per zone the group 0 / group 1 statistics converted to the red / blue
 * ratios at the calibration gains (t32_awb_lg_zones).
 * Phase 2 (L_3dbfc .. L_3de3c): sums over group 1, mean red / blue of the zones with their inverses (t32_awb_lg_means).
 * Phase 3 (L_3de3c .. L_3dfe8): the group 1 shares per weight, and the zones whose weight share of the area is too small
 * cleared again (t32_awb_lg_shares, t32_awb_lg_clear).
 * Phase 4 (L_3dfe8 .. L_3e1bc): sat_weight for sat_mode 1 / 2, the early exit when the zone means are still, otherwise the
 * reference means refreshed and the group tables cleared (t32_awb_lg_gate).
 * Phase 5 (L_3e1bc .. L_3e348): the colour temperature detection (t32_awb_ct_detect) and the weighted mean of the
 * last 15 detections (t32_awb_lg_detect).
 * Phase 6 (L_3e348 .. end): the gains from the detection, and the change state par+8 / +12 / +16 .. +28 with the
 * smoothing of par+24 / +28 (t32_awb_lg_apply); t32_awb_long_alogrithm chains all phases.
 */
#ifndef TX_ISP_T32_AWBLONG_H
#define TX_ISP_T32_AWBLONG_H

#include "tx_isp_t32_awbdet.h"

/* The locals of the vendor function that outlive a phase (vendor stack slot in the comment) */
struct t32_awb_lg {
	u32 fp;			/* s3: par fp_shift */
	u32 os;			/* sp60: par out_shift */
	u32 rows, cols;		/* sp68, sp64: zone grid */
	u32 one;		/* sp56 (and sp40 .. sp52 at entry): 1 << fp */
	u32 s7;			/* fp - 10 */
	u32 mr, mb;		/* sp88, sp92: the calibration gains cal_r / cal_b << (fp - 10) times 1.0 in fp bits */
	u32 sat_thr;		/* sp96: par sat_thr */
	u32 p8, p12, p16, p20, p24, p28;	/* sp108, sp100, sp80, sp84, sp72, sp76: par +8 .. +28 at entry */
	u32 win, w3312, small, still;	/* s2, sp112, sp116, sp120 */
	u32 area;		/* sp104: lg_dim_a * lg_dim_b & 0xffff */
	u32 gain[2];		/* sp40, sp44 */
	u32 valid[2];		/* sp32, sp36 */
	u32 out[2];		/* sp48, sp52 */
	u32 sumr, sumb;		/* phase 2: sums of the group 1 red / blue ratios */
	u32 cntr, cntb;		/* ... and the number of non-zero ones */
	u32 half_hi;		/* sp88 after phase 2: sign word of half = (1 << os) / 2 */
	u32 half;		/* s8 of phase 2 */
	u64 xr, xb;		/* sp124/sp128, t3/t6: the two 64 bit quotients of phase 2 */
};

/* signed division as the vendor's DIV: x / 0 is 1 (x < 0) or 0xffffffff, INT_MIN / -1 is INT_MIN */
static inline u32 t32_awb_sdiv(u32 x, u32 y)
{
	if (x == 0x80000000U && y == 0xffffffffU)
		return 0x80000000U;
	if (y)
		return (u32)((int)x / (int)y);
	return (int)x < 0 ? 1 : 0xffffffffU;
}

/*
 * fix_point_div (0x1d52c): (num << s) / den with 64 bit operands, the integer quotient shifted up and the s fraction
 * bits found by long division of the remainder (as t32_awb_fixdiv).  A zero divisor: the host oracle's div64_u64
 * gives 0 (the kernel would trap), the vendor prints an error first.
 */
static inline u64 t32_awb_fixdiv64(u32 s, u64 num, u64 den)
{
	u64 q = den ? num / den : 0;
	u64 r = num - den * q, frac = 0, qs = q << (s & 63);
	u32 i = 0;

	while (i != s) {
		r <<= 1;
		frac <<= 1;
		if (den < r) {
			frac |= 1;
			r -= den;
		} else if (den == r) {
			frac = (frac | 1) << ((s - 1 - i) & 63);
			break;
		}
		i++;
	}
	return frac | qs;
}

/* Entry: the values the vendor loads before anything is written */
static inline void t32_awb_lg_entry(struct t32_awb *a, struct t32_awb_lg *l)
{
	const struct t32_awb_par *par = a->par;
	u32 i;

	l->fp = par->fp_shift;
	l->os = par->out_shift;
	l->rows = par->zones_h;
	l->cols = par->zones_w;
	l->one = 1u << (l->fp & 31);
	l->s7 = l->fp - 10;
	l->sat_thr = par->sat_thr;
	l->p8 = par->lg_cnt_a;
	l->p12 = par->lg_cnt_b;
	l->p16 = par->lg_gr;
	l->p20 = par->lg_gb;
	l->p24 = par->wb_r;
	l->p28 = par->wb_b;
	l->win = par->lg_win;
	l->w3312 = par->lg_p3312;
	l->small = par->lg_small;
	l->still = par->lg_still;
	l->area = (par->lg_dim_a * par->lg_dim_b) & 0xffff;
	for (i = 0; i < 2; i++) {
		l->gain[i] = l->one;
		l->valid[i] = 0;
	}
	l->gain[0] = l->gain[1] = l->one;
	l->out[0] = l->out[1] = l->one;
}

/*
 * Phase 1 (L_3d85c .. L_3dbfc).  mr / mb are the cal_r / cal_b gains << (fp - 10) in fp bits.  Group 0 zones with a divisor:
 * the ratios (n << 10) / den become the zone statistics zstat[0] (at the calibration gains) and the 16 bit hardware tables,
 * group 1 likewise in zstat[1]; a zero divisor clears the zone.  Returns -1 for a grid over 32 x 32 (not compared).
 */
static inline int t32_awb_lg_zones(struct t32_awb *a, struct t32_awb_lg *l)
{
	const struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	struct t32_awb_hw *hw = a->hw;
	u32 row, col, z, fp = l->fp, s7 = l->s7 & 31;

	if (l->rows > 32 || l->cols > 32)
		return -1;
	l->mr = t32_awb_fixmul(fp, l->one, par->cal_r << s7);
	l->mb = t32_awb_fixmul(fp, l->one, par->cal_b << s7);
	for (row = 0; row < l->rows; row++) {
		for (col = 0; col < l->cols; col++) {
			z = row * 32 + col;
			if (!st->g0_den[z]) {
				st->zstat[0][0][z] = 0;
				st->zstat[0][1][z] = 0;
				st->zw_raw[z] = 0;
				hw->lg_ra[z] = 0;
				hw->lg_ba[z] = 0;
			} else {
				u32 d = st->g0_den[z];

				st->zstat[0][0][z] = t32_awb_fixmul(fp, t32_awb_udiv(st->g0_n_r[z] << 10, d) << s7, l->mr);
				st->zstat[0][1][z] = t32_awb_fixmul(fp, t32_awb_udiv(st->g0_n_b[z] << 10, d) << s7, l->mb);
				st->zw_raw[z] = st->g0_w[z];
				hw->lg_ra[z] = (u16)t32_awb_udiv(st->g0_n_r[z] << 10, d);
				hw->lg_ba[z] = (u16)t32_awb_udiv(st->g0_n_b[z] << 10, d);
			}
			st->zs2[0][z] = hw->lg_ra[z];
			st->zs2[1][z] = hw->lg_ba[z];
			if (!st->g1_den[z]) {
				st->zstat[1][0][z] = 0;
				st->zstat[1][1][z] = 0;
				st->zw_raw2[z] = 0;
			} else {
				u32 d = st->g1_den[z];

				st->zstat[1][0][z] = t32_awb_fixmul(fp, t32_awb_udiv(st->g1_n_r[z] << 10, d) << s7, l->mr);
				st->zstat[1][1][z] = t32_awb_fixmul(fp, t32_awb_udiv(st->g1_n_b[z] << 10, d) << s7, l->mb);
				st->zw_raw2[z] = st->g1_w[z];
			}
		}
	}
	return 0;
}

/*
 * Phase 2 (L_3dbfc .. L_3de3c).  The group 1 statistics zstat[1] are summed over the grid and counted (non-zero zones), the
 * sums divided by the calibration gains in fp bits (64 bit, t32_awb_fixdiv64); the mean red / blue is that quotient plus half,
 * shifted down by out_shift, rounded by the count and cut to 16 bits (256 when no zone counted).  The previous mean moves to
 * avg[2..3]; hw gets the means and 65536 / them.
 */
static inline void t32_awb_lg_means(struct t32_awb *a, struct t32_awb_lg *l)
{
	struct t32_awb_state *st = a->st;
	struct t32_awb_hw *hw = a->hw;
	u32 row, col, z, nz;
	u64 half64;

	l->sumr = l->sumb = 0;
	nz = l->rows * l->cols;
	l->cntr = l->cntb = nz;
	for (row = 0; row < l->rows; row++) {
		for (col = 0; col < l->cols; col++) {
			z = row * 32 + col;
			l->sumr += st->zstat[1][0][z];
			l->sumb += st->zstat[1][1][z];
			l->cntr -= st->zstat[1][0][z] == 0;
			l->cntb -= st->zstat[1][1][z] == 0;
		}
	}
	l->xr = t32_awb_fixdiv64(l->fp, l->sumr, l->mr);
	l->half = t32_awb_sdiv(1u << (l->os & 31), 2);
	l->half_hi = (u32)((int)l->half >> 31);
	l->xb = t32_awb_fixdiv64(l->fp, l->sumb, l->mb);
	half64 = (u64)l->half_hi << 32 | l->half;
	st->avg[2] = st->avg[0];
	st->avg[3] = st->avg[1];
	if (!l->cntr) {
		st->avg[0] = hw->avg_r = 256;
	} else {
		u32 q = (u32)((l->xr + half64) >> (l->os & 63));

		st->avg[0] = hw->avg_r = (u16)(((l->cntr >> 1) + q) / l->cntr);
	}
	if (!l->cntb) {
		st->avg[1] = hw->avg_b = 256;
	} else {
		u32 q = (u32)((l->xb + half64) >> (l->os & 63));

		st->avg[1] = hw->avg_b = (u16)(((l->cntb >> 1) + q) / l->cntb);
	}
	hw->avg_ir = t32_awb_inverse(hw->avg_r);
	hw->avg_ib = t32_awb_inverse(hw->avg_b);
}

/*
 * Phase 3a (L_3de3c .. L_3ded4): per zone of group 1 with a weight the three sums divided by it (k_r, k_d, k_b), else 0.
 */
static inline void t32_awb_lg_shares(struct t32_awb *a, const struct t32_awb_lg *l)
{
	struct t32_awb_state *st = a->st;
	u32 row, col, z;

	for (row = 0; row < l->rows; row++) {
		for (col = 0; col < l->cols; col++) {
			u32 w;

			z = row * 32 + col;
			w = st->g1_w[z];
			if (!w) {
				st->k_r[z] = 0;
				st->k_d[z] = 0;
				st->k_b[z] = 0;
			} else {
				st->k_r[z] = t32_awb_udiv(st->g1_n_r[z], w);
				st->k_d[z] = t32_awb_udiv(st->g1_den[z], w);
				st->k_b[z] = t32_awb_udiv(st->g1_n_b[z], w);
			}
		}
	}
}

/*
 * Phase 3b (L_3ded4 .. L_3dfe8): with area = lg_dim_a * lg_dim_b the share 1000 * weight / area (16 bit) of a zone must exceed
 * sat_thr to keep its group 0 (zstat[0], weights, hardware ratios, zs2) and, by the group 1 weight, its group 1 (zstat[1],
 * zw_raw2); otherwise they are cleared.
 */
static inline void t32_awb_lg_clear(struct t32_awb *a, const struct t32_awb_lg *l)
{
	struct t32_awb_state *st = a->st;
	struct t32_awb_hw *hw = a->hw;
	u32 row, col, z;

	for (row = 0; row < l->rows; row++) {
		for (col = 0; col < l->cols; col++) {
			u32 s0, s1;

			z = row * 32 + col;
			s1 = t32_awb_udiv(1000 * st->g1_w[z], l->area) & 0xffff;
			s0 = t32_awb_udiv(1000 * st->g0_w[z], l->area) & 0xffff;
			if (!(l->sat_thr < s0)) {
				st->zstat[0][0][z] = 0;
				st->zstat[0][1][z] = 0;
				st->zw_raw[z] = 0;
				hw->lg_ra[z] = 0;
				hw->lg_ba[z] = 0;
				st->zs2[0][z] = 0;
				st->zs2[1][z] = hw->lg_ba[z];
			}
			if (!(l->sat_thr < s1)) {
				st->zstat[1][0][z] = 0;
				st->zstat[1][1][z] = 0;
				st->zw_raw2[z] = 0;
			}
		}
	}
}

/*
 * Phase 4 (L_3dfe8 .. L_3e1bc).  With sat_mode 1 or 2 sat_weight weighs the zones of group 0 into zt2.  With a history
 * (lg_have == 1), a calm last frame (lg_calm == 1), a change of the zone means against the reference (avg[4], avg[5]) of
 * less than lg_still and neither the update nor the mode byte set (st+107596 & 0x00ff00ff) the vendor returns here
 * (this function returns 1, nothing more is written).  Otherwise the means become the reference and the detection tables
 * (cell sums, top cells, points, clusters, gw_map, zdist, zwt3, zw_norm, zlim) are cleared; 0 = go on.
 */
static inline int t32_awb_lg_gate(struct t32_awb *a, struct t32_awb_lg *l)
{
	const struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;

	if ((u16)(par->sat_mode - 1) < 2)
		t32_awb_sat_weight(par, st->g0_n_r, st->g0_den, st->g0_n_b, st->g0_w, st->zt2, l->gain);
	if (st->lg_have == 1) {
		u32 dr = (u16)(st->avg[4] > st->avg[0] ? st->avg[4] - st->avg[0] : st->avg[0] - st->avg[4]);
		u32 db = (u16)(st->avg[5] > st->avg[1] ? st->avg[5] - st->avg[1] : st->avg[1] - st->avg[5]);

		if (st->lg_calm == 1 && (u16)(dr + db) < l->still && !(t32_awb_u32at(st->raw, 107596) & 0x00ff00ffU))
			return 1;
	}
	st->avg[4] = st->avg[0];
	st->avg[5] = st->avg[1];
	memset(st->raw + 79228, 0, 784);
	memset(st->raw + 80012, 0, 784);
	memset(st->raw + 80796, 0, 784);
	memset(st->raw + 81188, 0, 144);
	memset(st->raw + 81332, 0, 720);
	memset(st->raw + 82052, 0, 960);
	memset(st->raw + 87108, 0, 4096);
	memset(st->raw + 91204, 0, 4096);
	memset(st->raw + 95300, 0, 4096);
	memset(st->raw + 99396, 0, 4096);
	memset(st->raw + 103492, 0, 4096);
	return 0;
}

/*
 * Phase 5 (L_3e1bc .. L_3e348).  The detection runs on the zone statistics (out = the detected r / b gains, valid its flags);
 * d is its working set (about 40 kB, the caller provides it).  With a history (lg_have == 1) the three rings of the last 15
 * detections (r, b, ct) shift by one and take this frame's out[0], out[1], ct; the window length lg_win (1 .. 15) of the newest
 * entries, weighted by their index + 1, gives the new out[0], out[1] and ct (rounded).  Without a history every ring entry
 * becomes this frame's value (out and ct stay).  Returns -1 when the detection refuses the case (vendor out of bounds).
 */
static inline int t32_awb_lg_detect(struct t32_awb *a, struct t32_awb_lg *l, struct t32_awb_ctd *d, u32 tbl)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 i;

	if (t32_awb_ct_detect(a, d, (const u32 *)(par->raw + tbl), l->out, l->valid))
		return -1;
	if (st->lg_have == 1) {
		u32 w, sw = 0, sa = 0, sb = 0, sc = 0;

		for (i = 0; i < 14; i++) {
			st->ring_r[i] = st->ring_r[i + 1];
			st->ring_b[i] = st->ring_b[i + 1];
			st->ring_ct[i] = st->ring_ct[i + 1];
		}
		st->ring_r[14] = l->out[0];
		st->ring_b[14] = l->out[1];
		st->ring_ct[14] = par->ct;
		if (l->win >= 16)
			l->win = 15;
		else if (l->win == 0)
			l->win = 1;
		for (i = 15 - l->win; i < 15; i++) {
			w = i + 1;
			sw += w;
			sa += w * st->ring_r[i];
			sb += w * st->ring_b[i];
			sc += w * st->ring_ct[i];
		}
		l->out[0] = t32_awb_udiv(sa + (sw >> 1), sw);
		l->out[1] = t32_awb_udiv(sb + (sw >> 1), sw);
		par->ct = t32_awb_udiv(sc + (sw >> 1), sw);
	} else {
		for (i = 0; i < 15; i++) {
			st->ring_r[i] = l->out[0];
			st->ring_b[i] = l->out[1];
			st->ring_ct[i] = par->ct;
		}
	}
	return 0;
}

/*
 * Phase 6 (L_3e348 .. end).  Without a history (lg_have != 1) the history is started (lg_have = 1, lg_calm = 0) and the
 * window is 1.  With one, nothing changing (the newest two detections equal in r and b, update / mode byte clear, par +16 / +20
 * equal to par +24 / +28) just marks the frame calm and ends.  Otherwise the detected gains out[0], out[1] become
 * 1 / out in the out_shift fixed point: gR, gB (in l->gain); d = |gR - par+16| + |gB - par+20|.  Then the change state:
 *   par+8 == 0:  if d > lg_small and the detection is valid (valid[0] != 1): par +8 / +12 = 1, +16 / +20 = gR / gB and
 *                +24 / +28 += (gR - par+16, gB - par+20) / window (signed), else +8 / +12 = 0;
 *   par+8 != 0:  if d > lg_p3312 and valid[0] != 1: +16 / +20 = gR / gB, +8 / +12 = 1, +24 / +28 += (gR - par+24, gB - par+28) / window;
 *                else with +12 + 1 == window or window == 1: +8 = 0, +24 / +28 = +16 / +20, then +12 + 1;
 *                else +8 = 1, +24 / +28 += (gR - par+24, gB - par+28) / (window - par+12), then +12 + 1.
 * Returns which path ran (1 calm frame, 2 .. 6 the change states above in order), for coverage.
 */
static inline int t32_awb_lg_apply(struct t32_awb *a, struct t32_awb_lg *l)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 os = l->os & 31, half = 1u << ((l->os - 1) & 31), gr, gb, dr, db, d, win = l->win;
	int path;

	if (st->lg_have != 1) {
		st->lg_have = 1;
		st->lg_calm = 0;
		win = l->win = 1;
	} else if (st->ring_r[13] == st->ring_r[14] && st->ring_b[13] == st->ring_b[14] &&
		   !(t32_awb_u32at(st->raw, 107596) & 0x00ff00ffU) && par->lg_gr == par->wb_r && par->lg_gb == par->wb_b) {
		st->lg_calm = 1;
		return 1;
	}
	gr = (u32)t32_awb_fixdiv64(l->fp, l->one, l->out[0]);
	gb = (u32)t32_awb_fixdiv64(l->fp, l->one, l->out[1]);
	gr = (half + gr) >> os;
	gb = (half + gb) >> os;
	l->gain[0] = gr;
	l->gain[1] = gb;
	dr = gr > l->p16 ? gr - l->p16 : l->p16 - gr;
	db = gb > l->p20 ? gb - l->p20 : l->p20 - gb;
	d = dr + db;
	if (!l->p8) {
		if (l->small < d && l->valid[0] != 1) {
			par->lg_gb = gb;
			par->lg_cnt_a = 1;
			par->lg_cnt_b = 1;
			par->lg_gr = gr;
			par->wb_r = t32_awb_sdiv(gr - l->p16, win) + l->p24;
			par->wb_b = t32_awb_sdiv(gb - l->p20, win) + l->p28;
			return 3;
		}
		par->lg_cnt_a = 0;
		par->lg_cnt_b = 0;
		return 2;
	}
	if (l->w3312 < d && l->valid[0] != 1) {
		par->lg_gr = gr;
		par->lg_gb = gb;
		par->lg_cnt_a = 1;
		par->lg_cnt_b = 1;
		par->wb_r = t32_awb_sdiv(gr - l->p24, win) + l->p24;
		par->wb_b = t32_awb_sdiv(gb - l->p28, win) + l->p28;
		return 4;
	}
	if (l->p12 + 1 == win || win == 1) {
		par->lg_cnt_a = 0;
		par->wb_r = par->lg_gr;
		par->wb_b = par->lg_gb;
		path = 5;
	} else {
		u32 n = win - l->p12;

		par->lg_cnt_a = 1;
		par->wb_r = t32_awb_sdiv(gr - l->p24, n) + l->p24;
		par->wb_b = t32_awb_sdiv(gb - l->p28, n) + l->p28;
		path = 6;
	}
	par->lg_cnt_b++;
	return path;
}

/*
 * The whole vendor tisp_awb_long_alogrithm: phases 1 .. 6.  tbl = the parameter table choice of tisp_awb_long_par_update
 * (byte offset in par), d = the working set of the detection (about 40 kB).  Returns 0 when it ran, 1 when it left early
 * (still picture), -1 when the vendor would run past one of its tables (not compared).  *path (if given) receives the
 * path of t32_awb_lg_apply.
 */
static inline int t32_awb_long_alogrithm(struct t32_awb *a, struct t32_awb_ctd *d, u32 tbl, int *path)
{
	struct t32_awb_lg l;
	int p;

	t32_awb_lg_entry(a, &l);
	if (t32_awb_lg_zones(a, &l))
		return -1;
	t32_awb_lg_means(a, &l);
	t32_awb_lg_shares(a, &l);
	t32_awb_lg_clear(a, &l);
	if (t32_awb_lg_gate(a, &l))
		return 1;
	if (t32_awb_lg_detect(a, &l, d, tbl))
		return -1;
	p = t32_awb_lg_apply(a, &l);
	if (path)
		*path = p;
	return 0;
}

#endif /* TX_ISP_T32_AWBLONG_H */
