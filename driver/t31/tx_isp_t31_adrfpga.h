/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 ADR curve generator: Tiziano_adr_fpga (vendor tx-isp-t31.ko 0x1c4ec) with its callees subsection_map,
 * subsection, subsection_up, subsection_light, interpolate_adr_x8_y12 and the curve helpers, moved out of
 * tx_isp_tuning.c so the binary oracle block t31_binoracle adrfpga can build it (openimp #563).
 * The includer provides fix_point_div_32, fix_point_mult2_32, tisp_math_exp2, pr_info, adr_log_curve11 and
 * adr_log_grid24.
 */
#ifndef TX_ISP_T31_ADRFPGA_H
#define TX_ISP_T31_ADRFPGA_H

/* OEM EXACT: subsection_up — LSC mesh subsection interpolation.
 * Decompiled from OEM at 0x1c314. Finds nearest CT match and scales. */
static int subsection_up(uint32_t *out, const uint32_t *ct_list,
                         const uint32_t *ct_table, uint32_t scale)
{
    /* vendor 0x1c364: signed compares (slt) and signed div, as the OEM; an
     * index sum of 0 gives -1 (openimp #563) */
    int i;
    out[0] = 0;
    out[8] = 0xfff;

    for (i = 1; i < 8; i++) {
        int32_t target = (int32_t)ct_list[i];
        int32_t best_j = 0, best_count = 0;
        int32_t best_dist = 10000;
        int j;

        for (j = 0; j < 512; j++) {
            int32_t t = (int32_t)ct_table[j];
            int32_t dist = (target < t) ? (int32_t)((uint32_t)t - (uint32_t)target) :
                                          (int32_t)((uint32_t)target - (uint32_t)t);
            if (dist < best_dist) {
                best_j = j;
                best_count = 1;
                best_dist = dist;
            } else if (dist == best_dist) {
                best_j += j;
                best_count++;
            }
        }
        if (best_count <= 0)
            best_count = 1;
        out[i] = (uint32_t)(((int32_t)((uint32_t)best_j << 12) / best_count + 0x800) / 0x1000) * scale - 1U;
    }
    return out[7];
}

/* OEM EXACT: subsection_light — LSC light source blending.
 * Decompiled from OEM at 0x1c3e8. Blends two mesh sets based on distance.
 * Vendor 0x1c438: signed compare and signed div by 1000 (a curve entry of -1
 * from subsection_up stays below the table, openimp #563). */
static int subsection_light(const uint32_t *mesh_a, const uint32_t *mesh_b,
                            uint32_t weight, uint32_t *out)
{
    int i;
    for (i = 0; i < 9; i++) {
        uint32_t a = mesh_a[i];
        uint32_t b = mesh_b[i];
        uint32_t val;

        if ((int32_t)b < (int32_t)a)
            val = a * 1000U - (a - b) * weight;
        else
            val = b * 1000U - (b - a) * weight;
        out[i] = (uint32_t)((int32_t)val / 1000);
    }
    return 1000;
}

/* ========================================================================
 * ADR FPGA helper functions — OEM-exact ports from binary decompilation
 * ======================================================================== */

/* subsection_map - OEM at 0x1bb40
 * Finds nearest bin in arg6 (512 entries) to target value arg1, then
 * maps through the gamma LUT (arg4/arg5) with blend factor arg3.
 * arg7 = number of output bins, arg8 = output precision, arg9 = internal precision
 * arg10 = blend mode (1 = always blend, else only blend down)
 */
static int32_t subsection_map(int32_t target, int32_t mapped_val, int32_t blend_pct,
			      int16_t *gam_x, int16_t *gam_y, int32_t *lut,
			      int32_t num_bins, int32_t out_prec, int32_t in_prec,
			      int32_t blend_mode)
{
	int32_t best_dist = 0x2710;
	int32_t best_idx = 0;
	int32_t best_cnt = 0;
	int i;

	for (i = 0; i < 512; i++) {
		int32_t val = lut[i];
		int32_t dist = (target >= val) ? (target - val) : (val - target);
		if (dist < best_dist) {
			best_cnt = 0;
			best_idx = i;
			best_dist = dist;
			best_cnt++;
		} else if (dist == best_dist) {
			best_idx += i;
			best_cnt++;
		}
	}

	if (best_cnt <= 0)
		best_cnt = 1;
	if (best_idx <= 0)
		best_idx = 1;

	{
		int32_t half = (1 << (in_prec & 0x1f)) / 2;
		int32_t t0_val = ((fix_point_div_32(in_prec,
				best_idx << (in_prec & 0x1f),
				best_cnt << (in_prec & 0x1f)) + half)
				>> (in_prec & 0x1f)) * num_bins - 1;
		int32_t s0_val;
		int j;

		/* Lookup t0_val in gam_x table */
		for (j = 0; j < 0x81; j++) {
			int32_t gx = (int32_t)(int16_t)gam_x[j];
			if (t0_val < gx) {
				int16_t *gy_ptr = &gam_y[j];
				int32_t gy_cur = (int32_t)(int16_t)*gy_ptr;
				int32_t gy_prev = (int32_t)(int16_t)*(gam_y + j - 1);
				int32_t gx_prev = (int32_t)(int16_t)*(gam_x + j - 1);
				int32_t slope = fix_point_div_32(in_prec,
					(gy_cur - gy_prev) << (in_prec & 0x1f),
					(gx - gx_prev) << (in_prec & 0x1f));
				int32_t interp = fix_point_mult2_32(in_prec,
					slope, (gx - t0_val) << (in_prec & 0x1f));
				s0_val = gy_cur - ((interp + half) >> (in_prec & 0x1f));
				break;
			}
		}
		if (j == 0x81)
			s0_val = (int32_t)(int16_t)gam_y[0x80];

		/* Blend with mapped_val */
		{
			int64_t blended;
			int32_t a1_val;

			if (blend_mode == 1) {
				blended = (int64_t)mapped_val * 100LL +
					  (int64_t)(s0_val - mapped_val) * blend_pct;
				a1_val = (int32_t)blended;
			} else if (mapped_val >= s0_val) {
				blended = (int64_t)mapped_val * 100LL +
					  (int64_t)(s0_val - mapped_val) * blend_pct;
				a1_val = (int32_t)blended;
			} else {
				a1_val = mapped_val * 100;
			}

			return ((1 << (out_prec & 0x1f)) / 2 +
				fix_point_div_32(out_prec,
					a1_val << (out_prec & 0x1f),
					100 << (out_prec & 0x1f)))
				>> (out_prec & 0x1f);
		}
	}
}

/* subsection - OEM at 0x1bd74
 * Computes 9 curve kneepoints for a given light level, using the gamma LUT.
 * result[0..8] are the 9 kneepoints. result[0]=0, result[8]=0xfff.
 * arg2 = blend percent, arg3 = gam_x (int16_t*), arg4 = gam_y (int16_t*),
 * arg5 = base LUT (512 entries), arg6 = num_bins, arg7/arg8/arg9 = precisions
 */
static void subsection(int32_t *result, int32_t blend_pct,
		       int16_t *gam_lut_a, int16_t *gam_lut_b, int32_t *lut,
		       int32_t num_bins, int32_t prec_a, int32_t prec_b,
		       int32_t blend_mode)
{
	/* OEM EXACT parameter mapping (verified from decompilation at 0x1bd74):
	 * - subsection_map(): forward lookup, iterate arg3 and interpolate arg4
	 * - subsection(): inverse lookup, iterate arg4 and interpolate arg3
	 *
	 * The inline lookups here therefore walk gamma Y and recover gamma X.
	 * This is not the same direction used by subsection_map(). */
	int16_t *lookup_x = gam_lut_a;
	int16_t *lookup_y = gam_lut_b;
	int32_t v0_val = 2 << (prec_a & 0x1f);
	int32_t fp;
	int32_t half_a = (1 << (prec_a & 0x1f)) / 2;
	int j;

	result[8] = 0xfff;
	result[0] = 0;

	/* Kneepoint 4 (midpoint) */
	{
		int32_t map_in = (fix_point_div_32(prec_a,
				0xfff << (prec_a & 0x1f), v0_val) + half_a)
				>> (prec_a & 0x1f);
		int32_t mapped = subsection_map(0x1388, map_in, blend_pct,
				gam_lut_a, gam_lut_b, lut, num_bins, prec_a, prec_b,
				blend_mode);

		/* OEM exact: iterate gamma Y and interpolate gamma X. */
		for (j = 0; j < 0x81; j++) {
			int32_t gy_cur = (int32_t)(int16_t)lookup_y[j];
			if (mapped < gy_cur) {
				int32_t gx_cur = (int32_t)(int16_t)lookup_x[j];
				int32_t gy_prev = (int32_t)(int16_t)lookup_y[j - 1];
				int32_t gx_prev = (int32_t)(int16_t)lookup_x[j - 1];
				int32_t slope = fix_point_div_32(prec_a,
					(gx_cur - gx_prev) << (prec_a & 0x1f),
					(gy_cur - gy_prev) << (prec_a & 0x1f));
				int32_t interp = fix_point_mult2_32(prec_a,
					slope, (gy_cur - mapped) << (prec_a & 0x1f));
				result[4] = gx_cur - ((interp + half_a) >> (prec_a & 0x1f));
				break;
			}
		}
		if (j == 0x81)
			result[4] = (int32_t)(int16_t)lookup_x[0x80];

		fp = num_bins << (prec_a & 0x1f);

		/* Kneepoint 2 */
		{
			int32_t idx = (fix_point_div_32(prec_a,
					result[4] << (prec_a & 0x1f), fp) + half_a)
					>> (prec_a & 0x1f);
			int32_t sub_arg1 = lut[idx] / 2;
			int32_t map_in2 = (fix_point_div_32(prec_a,
					mapped << (prec_a & 0x1f), v0_val) + half_a)
					>> (prec_a & 0x1f);
			int32_t mapped2 = subsection_map(sub_arg1, map_in2, blend_pct,
					gam_lut_a, gam_lut_b, lut, num_bins, prec_a, prec_b,
					blend_mode);

			for (j = 0; j < 0x81; j++) {
				int32_t gy_cur = (int32_t)(int16_t)lookup_y[j];
				if (mapped2 < gy_cur) {
					int32_t gx_cur = (int32_t)(int16_t)lookup_x[j];
					int32_t gy_prev = (int32_t)(int16_t)lookup_y[j - 1];
					int32_t gx_prev = (int32_t)(int16_t)lookup_x[j - 1];
					int32_t slope = fix_point_div_32(prec_a,
						(gx_cur - gx_prev) << (prec_a & 0x1f),
						(gy_cur - gy_prev) << (prec_a & 0x1f));
					int32_t interp = fix_point_mult2_32(prec_a,
						slope, (gy_cur - mapped2) << (prec_a & 0x1f));
					result[2] = gx_cur - ((interp + half_a) >> (prec_a & 0x1f));
					break;
				}
			}
			if (j == 0x81)
				result[2] = (int32_t)(int16_t)lookup_x[0x80];

			/* OEM EXACT: Kneepoints 1 and 3 via two separate subsection_map calls.
			 * Kneepoint 1 uses result[2] index and mapped2 alone.
			 * Kneepoint 3 uses average of result[4]/result[2] indices and mapped/mapped2. */
			{
				int32_t idx2 = (fix_point_div_32(prec_a,
						result[2] << (prec_a & 0x1f), fp) + half_a)
						>> (prec_a & 0x1f);
				int32_t idx4 = (fix_point_div_32(prec_a,
						result[4] << (prec_a & 0x1f), fp) + half_a)
						>> (prec_a & 0x1f);

				/* Kneepoint 1: OEM third subsection_map call */
				{
					int32_t sub_arg_kp1 = lut[idx2] / 2;
					int32_t map_in_kp1 = (fix_point_div_32(prec_a,
							mapped2 << (prec_a & 0x1f),
							v0_val) + half_a)
							>> (prec_a & 0x1f);
					int32_t mapped_kp1 = subsection_map(sub_arg_kp1, map_in_kp1,
							blend_pct, gam_lut_a, gam_lut_b, lut, num_bins,
							prec_a, prec_b, blend_mode);

					for (j = 0; j < 0x81; j++) {
						int32_t gy_cur = (int32_t)(int16_t)lookup_y[j];
						if (mapped_kp1 < gy_cur) {
							int32_t gx_cur = (int32_t)(int16_t)lookup_x[j];
							int32_t gy_prev = (int32_t)(int16_t)lookup_y[j - 1];
							int32_t gx_prev = (int32_t)(int16_t)lookup_x[j - 1];
							int32_t slope = fix_point_div_32(prec_a,
								(gx_cur - gx_prev) << (prec_a & 0x1f),
								(gy_cur - gy_prev) << (prec_a & 0x1f));
							int32_t interp = fix_point_mult2_32(prec_a,
								slope, (gy_cur - mapped_kp1) << (prec_a & 0x1f));
							result[1] = gx_cur - ((interp + half_a) >> (prec_a & 0x1f));
							break;
						}
					}
					if (j == 0x81)
						result[1] = (int32_t)(int16_t)lookup_x[0x80];
				}

				/* Kneepoint 3: OEM fourth subsection_map call */
				{
					int32_t sub_arg2 = (lut[idx4] + lut[idx2]) / 2;
					int32_t map_in3 = (fix_point_div_32(prec_a,
							(mapped + mapped2) << (prec_a & 0x1f),
							v0_val) + half_a)
							>> (prec_a & 0x1f);
					int32_t mapped3 = subsection_map(sub_arg2, map_in3,
							blend_pct, gam_lut_a, gam_lut_b, lut, num_bins,
							prec_a, prec_b, blend_mode);

					for (j = 0; j < 0x81; j++) {
						int32_t gy_cur = (int32_t)(int16_t)lookup_y[j];
						if (mapped3 < gy_cur) {
							int32_t gx_cur = (int32_t)(int16_t)lookup_x[j];
							int32_t gy_prev = (int32_t)(int16_t)lookup_y[j - 1];
							int32_t gx_prev = (int32_t)(int16_t)lookup_x[j - 1];
							int32_t slope = fix_point_div_32(prec_a,
								(gx_cur - gx_prev) << (prec_a & 0x1f),
								(gy_cur - gy_prev) << (prec_a & 0x1f));
							int32_t interp = fix_point_mult2_32(prec_a,
								slope, (gy_cur - mapped3) << (prec_a & 0x1f));
							result[3] = gx_cur - ((interp + half_a) >> (prec_a & 0x1f));
							break;
						}
					}
					if (j == 0x81)
						result[3] = (int32_t)(int16_t)lookup_x[0x80];
				}
			}
		}

		/* Fix up kneepoints 5-7 relative to kneepoint 4 */
		result[5] = result[4] + 1;
		result[7] = result[4] + 3;
		result[6] = result[4] + 2;

		if (result[4] + 3 >= 0xfff) {
			result[4] = 0xffb;
			result[5] = 0xffc;
			result[6] = 0xffd;
			result[7] = 0xffe;
		}

		if (result[8] >= 0x1000)
			result[8] = 0xfff;
	}
}

/* interpolate_adr_x8_y12 - OEM at 0x1c440
 * Linear interpolation between two kneepoint values.
 * Returns interpolated value at position arg5 between arg1..arg2,
 * mapping arg3..arg4 range.
 */
static int32_t interpolate_adr_x8_y12(int32_t ev_lo, int32_t ev_hi,
				       int32_t val_lo, int32_t val_hi,
				       int32_t ev_target)
{
	int32_t diff;
	int32_t val_10bit;
	int32_t interp;
	int sign;

	if (val_lo >= val_hi) {
		diff = val_lo - val_hi;
		sign = 0;
	} else {
		diff = val_hi - val_lo;
		sign = 1;
	}

	val_10bit = val_lo << 0xa;
	{
		int32_t range = ev_hi - ev_lo;
		if (range == 0) range = 1;
		interp = ((diff * (ev_target - ev_lo)) << 0xa) / range;
	}

	if (sign)
		return (val_10bit + interp + 0x200) / 0x400;
	else
		return (val_10bit - interp + 0x200) / 0x400;
}

/* Piecewise linear interpolation helper for 7-segment tone curve.
 * Used by both Path A and Path B to map input X values through the
 * kneepoint curve to produce output Y values.
 * knees[0..8] = the 9 kneepoint Y values (var_16c array)
 * arg10_base = arg10 array (base output Y values for 5 kneepoints + last entry)
 * arg3 = input X values (min_kneepoint_x, 11 entries)
 * arg4 = output Y values (11 entries, later committed into min_kneepoint_y[10]
 *        plus the tail register slot data_9f0a8)
 */
static void adr_compute_output_curve(int32_t *knees, int32_t *arg10,
				     int32_t *arg3_x, int32_t *arg4_out)
{
	int32_t knee0 = knees[0];  /* var_16c */
	int32_t knee1 = knees[1];  /* var_168 */
	int32_t knee2 = knees[2];  /* var_164 */
	int32_t knee3 = knees[3];  /* var_160 */
	int32_t knee4 = knees[4];  /* var_15c */
	int32_t knee8 = knees[8];  /* var_14c */
	int32_t y_last = arg10[8];
	int32_t y0 = arg10[0];
	int32_t gap01 = knee1 - knee0;
	int32_t gap12 = knee2 - knee1;
	int32_t gap23 = knee3 - knee2;
	int32_t gap34 = knee4 - knee3;
	int32_t s4, s2, s3, s1_10;
	int i;
	int32_t *x_ptr = arg3_x;

	/* Compute 5 output kneepoint Y values with minimum gap constraints */
	s4 = y0 + gap01;
	if (arg10[1] - y0 >= gap01)
		s4 = arg10[1];

	s2 = arg10[2] + s4 - arg10[1];
	if (s2 - s4 < gap12)
		s2 = gap12 + s4;

	s3 = arg10[3] + s2 - arg10[2];
	if (s3 - s2 < gap23)
		s3 = gap23 + s2;

	s1_10 = arg10[4] + s3 - arg10[3];
	if (s1_10 - s3 < gap34)
		s1_10 = gap34 + s3;

	if (s1_10 >= 0xfff)
		s1_10 = 0xfff;

	/* Piecewise linear interpolation for 11 output bins */
	for (i = 0; i < 11; i++) {
		int32_t x = *x_ptr++;
		int32_t out;

		if (x < knee0) {
			out = y0 - ((int32_t)(fix_point_mult2_32(0xa,
				fix_point_div_32(0xa, y0 << 0xa, knee0 << 0xa),
				(knee0 - x) << 0xa) + 0x200) >> 0xa);
		} else if (x < knee1) {
			out = s4 - ((int32_t)(fix_point_mult2_32(0xa,
				fix_point_div_32(0xa, (s4 - y0) << 0xa, gap01 << 0xa),
				(knee1 - x) << 0xa) + 0x200) >> 0xa);
		} else if (x < knee2) {
			out = s2 - ((int32_t)(fix_point_mult2_32(0xa,
				fix_point_div_32(0xa, (s2 - s4) << 0xa, gap12 << 0xa),
				(knee2 - x) << 0xa) + 0x200) >> 0xa);
		} else if (x < knee3) {
			out = s3 - ((int32_t)(fix_point_mult2_32(0xa,
				fix_point_div_32(0xa, (s3 - s2) << 0xa, gap23 << 0xa),
				(knee3 - x) << 0xa) + 0x200) >> 0xa);
		} else if (x < knee4) {
			out = s1_10 - ((int32_t)(fix_point_mult2_32(0xa,
				fix_point_div_32(0xa, (s1_10 - s3) << 0xa, gap34 << 0xa),
				(knee4 - x) << 0xa) + 0x200) >> 0xa);
		} else if (x >= knee8) {
			out = y_last;
		} else {
			out = y_last - ((int32_t)(fix_point_mult2_32(0xa,
				fix_point_div_32(0xa, (y_last - s1_10) << 0xa,
					(knee8 - knee4) << 0xa),
				(knee8 - x) << 0xa) + 0x200) >> 0xa);
		}
		arg4_out[i] = out;
	}
}

/* OEM stores the ADR tail entries immediately after the array bodies:
 *   min_kneepoint_y[10] -> data_9f0a8
 *   ctc_kneepoint_y[8]  -> data_9f0cc
 * Our C globals are not laid out that way, so commit the tail entries
 * explicitly instead of relying on out-of-bounds adjacency. */
static void adr_store_min_curve11(const int32_t *curve11, int32_t *min10)
{
	int i;

	for (i = 0; i < 10; i++)
		min10[i] = curve11[i];
	data_9f0a8 = curve11[10];
}

static void adr_store_ctc_curve9(const int32_t *curve9, int32_t *ctc8)
{
	int i;

	for (i = 0; i < 8; i++)
		ctc8[i] = curve9[i];
	data_9f0cc = curve9[8];
}

/* 9-point piecewise interpolation for per-block curves (Path A).
 * Maps arg1[i] (block X values) through a 9-kneepoint curve (var_1b4)
 * using arg13 (base Y values), writes to arg2[i].
 */
static void adr_interp_9pt(int32_t *arg1, int32_t *arg2, int32_t *arg13,
			   int32_t *curve9, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		int32_t x = arg1[i];

		/* Default: copy from arg13 */
		arg2[i] = arg13[i];

		if (x < curve9[0]) {
			int32_t y0 = arg13[0];
			if (y0 >= 0) {
				int32_t v = (int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, y0 << 0xa, curve9[0] << 0xa),
					(curve9[0] - x) << 0xa) + 0x200) >> 0xa;
				arg2[i] = arg13[0] - v;
			}
		} else if (x < curve9[1]) {
			int32_t y_hi = arg13[1];
			int32_t y_lo = arg13[0];
			int32_t diff = y_hi - y_lo;
			int32_t seg = (curve9[1] - curve9[0]) << 0xa;
			int32_t dist = (curve9[1] - x) << 0xa;
			if (diff < 0) {
				arg2[i] = ((int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, (y_lo - y_hi) << 0xa, seg),
					dist) + 0x200) >> 0xa) + arg13[1];
			} else {
				int32_t v = (int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, diff << 0xa, seg),
					dist) + 0x200) >> 0xa;
				arg2[i] = arg13[1] - v;
			}
		} else if (x < curve9[2]) {
			int32_t y_hi = arg13[2];
			int32_t y_lo = arg13[1];
			int32_t diff = y_hi - y_lo;
			int32_t seg = (curve9[2] - curve9[1]) << 0xa;
			int32_t dist = (curve9[2] - x) << 0xa;
			if (diff < 0) {
				arg2[i] = ((int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, (y_lo - y_hi) << 0xa, seg),
					dist) + 0x200) >> 0xa) + arg13[2];
			} else {
				int32_t v = (int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, diff << 0xa, seg),
					dist) + 0x200) >> 0xa;
				arg2[i] = arg13[2] - v;
			}
		} else if (x < curve9[3]) {
			int32_t y_hi = arg13[3];
			int32_t y_lo = arg13[2];
			int32_t diff = y_hi - y_lo;
			int32_t seg = (curve9[3] - curve9[2]) << 0xa;
			int32_t dist = (curve9[3] - x) << 0xa;
			if (diff < 0) {
				arg2[i] = ((int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, (y_lo - y_hi) << 0xa, seg),
					dist) + 0x200) >> 0xa) + arg13[3];
			} else {
				int32_t v = (int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, diff << 0xa, seg),
					dist) + 0x200) >> 0xa;
				arg2[i] = arg13[3] - v;
			}
		} else if (x < curve9[4]) {
			int32_t y_hi = arg13[4];
			int32_t y_lo = arg13[3];
			int32_t diff = y_hi - y_lo;
			int32_t seg = (curve9[4] - curve9[3]) << 0xa;
			int32_t dist = (curve9[4] - x) << 0xa;
			if (diff < 0) {
				arg2[i] = ((int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, (y_lo - y_hi) << 0xa, seg),
					dist) + 0x200) >> 0xa) + arg13[4];
			} else {
				int32_t v = (int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, diff << 0xa, seg),
					dist) + 0x200) >> 0xa;
				arg2[i] = arg13[4] - v;
			}
		} else if (x < curve9[5]) {
			int32_t y_hi = arg13[5];
			int32_t y_lo = arg13[4];
			int32_t diff = y_hi - y_lo;
			int32_t seg = (curve9[5] - curve9[4]) << 0xa;
			int32_t dist = (curve9[5] - x) << 0xa;
			if (diff < 0) {
				arg2[i] = ((int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, (y_lo - y_hi) << 0xa, seg),
					dist) + 0x200) >> 0xa) + arg13[5];
			} else {
				int32_t v = (int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, diff << 0xa, seg),
					dist) + 0x200) >> 0xa;
				arg2[i] = arg13[5] - v;
			}
		} else if (x < curve9[6]) {
			int32_t y_hi = arg13[6];
			int32_t y_lo = arg13[5];
			int32_t diff = y_hi - y_lo;
			int32_t seg = (curve9[6] - curve9[5]) << 0xa;
			int32_t dist = (curve9[6] - x) << 0xa;
			if (diff < 0) {
				arg2[i] = ((int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, (y_lo - y_hi) << 0xa, seg),
					dist) + 0x200) >> 0xa) + arg13[6];
			} else {
				int32_t v = (int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, diff << 0xa, seg),
					dist) + 0x200) >> 0xa;
				arg2[i] = arg13[6] - v;
			}
		} else if (x >= curve9[8]) {
			arg2[i] = arg13[8];
		} else if (x >= curve9[7]) {
			int32_t y_hi = arg13[8];
			int32_t y_lo = arg13[7];
			int32_t diff = y_hi - y_lo;
			int32_t seg = (curve9[8] - curve9[7]) << 0xa;
			int32_t dist = (curve9[8] - x) << 0xa;
			if (diff < 0) {
				arg2[i] = ((int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, (y_lo - y_hi) << 0xa, seg),
					dist) + 0x200) >> 0xa) + arg13[8];
			} else {
				int32_t v = (int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, diff << 0xa, seg),
					dist) + 0x200) >> 0xa;
				arg2[i] = arg13[8] - v;
			}
		} else {
			/* x >= curve9[6] && x < curve9[7] */
			int32_t y_hi = arg13[7];
			int32_t y_lo = arg13[6];
			int32_t diff = y_hi - y_lo;
			int32_t seg = (curve9[7] - curve9[6]) << 0xa;
			int32_t dist = (curve9[7] - x) << 0xa;
			if (diff < 0) {
				arg2[i] = ((int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, (y_lo - y_hi) << 0xa, seg),
					dist) + 0x200) >> 0xa) + arg13[7];
			} else {
				int32_t v = (int32_t)(fix_point_mult2_32(0xa,
					fix_point_div_32(0xa, diff << 0xa, seg),
					dist) + 0x200) >> 0xa;
				arg2[i] = arg13[7] - v;
			}
		}
	}
}

/* OEM ADR subsection tables copied by Tiziano_adr_fpga from .rodata+0xf70/+0xf94
 * in the module (file offsets 0x6b200 and 0x6b224). */
static const uint32_t adr_const_table_6b200[9] = {
	0, 1250, 2500, 3750, 5000, 6250, 7500, 8750, 10000
};
static const uint32_t adr_const_table_6b224[9] = {
	0, 157, 350, 608, 1017, 1621, 2309, 3133, 4095
};

/* Tiziano_adr_fpga - ADR tone-mapping curve generator
 * OEM: function at 0x1c49c. Computes map_kneepoint_y from histogram data,
 * block statistics, and all the interpolated parameters.
 *
 * Args (mapped from the caller tiziano_adr_algorithm):
 *   arg1  = ctc_kneepoint_x[9]        (TizianoAdrFpgaStructMe)
 *   arg2  = ctc_kneepoint_y[8] + data_9f0cc tail (data_c03cc)
 *   arg3  = min_kneepoint_x[11]       (data_c03d0)
 *   arg4  = min_kneepoint_y[10] + data_9f0a8 tail (data_c03d4)
 *   arg5  = map_kneepoint_x[11]       (data_c03d8)
 *   arg6  = map_kneepoint_y[264]      (data_c03dc) — output: 24 blocks x 11
 *   arg7  = adr_hist[512]             (data_c03e0)
 *   arg8  = adr_block_y[192]          (data_c03e4)
 *   arg9  = adr_block_hist[720]       (data_c03e8)
 *   arg10 = adr_tm_base_lut[9]        (data_c03ec)
 *   arg11 = param_adr_gam_x[0x102]    (data_c03f0) — int16_t*
 *   arg12 = param_adr_gam_y_def[0x102](data_c03f4) — int16_t*
 *   arg13 = adr_ctc_map2cut_y_now[9]  (data_c03f8)
 *   arg14 = adr_map_mode_now[]        (data_c03fc)
 *   arg15 = adr_light_end_now[]       (data_c0400)
 *   arg16 = adr_block_light_now[]     (data_c0404)
 */
static int Tiziano_adr_fpga(void *arg1, uint32_t *arg2, uint32_t *arg3,
			     uint32_t *arg4, uint32_t *arg5, uint32_t *arg6,
			     uint32_t *arg7, uint32_t *arg8, uint32_t *arg9,
			     uint32_t *arg10, uint8_t *arg11, uint8_t *arg12,
			     uint32_t *arg13, uint32_t *arg14,
			     uint32_t *arg15, uint32_t *arg16)
{
	int32_t *arg1_s = (int32_t *)arg1;
	int32_t *arg2_s = (int32_t *)arg2;
	int32_t *arg3_s = (int32_t *)arg3;  /* min_kneepoint_x */
	int32_t *arg4_s = (int32_t *)arg4;  /* min_kneepoint_y */
	int32_t *arg5_s = (int32_t *)arg5;  /* map_kneepoint_x */
	int32_t *arg6_s = (int32_t *)arg6;  /* map_kneepoint_y output */
	int32_t *arg7_s = (int32_t *)arg7;
	int32_t *arg8_s = (int32_t *)arg8;
	int32_t *arg10_s = (int32_t *)arg10;
	int32_t *arg13_s = (int32_t *)arg13;
	int32_t *arg14_s = (int32_t *)arg14;
	int32_t *arg15_s = (int32_t *)arg15;
	int32_t *arg16_s = (int32_t *)arg16;

	/* Local variables matching OEM register/stack assignments */
	int32_t v0_3;     /* scaling constant from fix_point_div_32 */
	int32_t v0_4;     /* arg16[0] — mean Y mode */
	int32_t v1_1;     /* arg16[0xa] — kneepoint gap[0] */
	int32_t v0_5;     /* arg16[1] — contrast mode */
	int32_t var_128;  /* arg16[2] — mean_y / contrast param */
	int32_t v1_2;     /* arg16[0xb] — kneepoint gap[1] */
	int32_t var_11c;  /* arg16[3] — sigma_bright initial */
	int32_t v0_8;     /* arg16[4] — sigma param */
	int32_t v1_3;     /* arg16[0xc] — kneepoint gap[2] */
	int32_t v0_9;     /* arg16[7] — light level ($v0_9, used in Path A second search) */
	int32_t v1_4;     /* arg16[0xd] — kneepoint gap[3] */
	int32_t s1;       /* arg16[6] — current light level for Path A first search */
	int32_t v0_10;    /* arg16[8] — block size W */
	int32_t a1_val;   /* arg16[9] — block size H */
	int32_t t2;       /* arg16[0xe] — accumulate mode flag */
	int32_t v1_5;     /* arg15[0x12] — clamp control flag */
	int32_t v1_6, v1_7; /* arg15[0x13], arg15[0x14] — clamp limits */
	int32_t v1_8, v1_9; /* arg15[0x15], arg15[0x16] — sigma clamp limits */
	int32_t v1_10, v1_11; /* arg15[0x17], arg15[0x18] — sigma_light clamp limits */
	int32_t v1_12;    /* arg15[0x19] — slope limiting enable */
	int32_t v1_13;    /* arg15[0x1c] — spatial weight scale */
	int32_t s6_1;     /* arg15[0xe] — path select (!=1=direct, 1=subsection path) */
	int32_t t0;       /* arg15[0x1a] — blend mode for subsection */

	/* arg14 fields */
	int32_t a0_13, a0_14, a0_15, a0_16, a0_17;
	int32_t var_124_1;  /* arg14[5] */
	int32_t a0_19, a0_20, a0_21, a0_22;
	int32_t v1_15;      /* arg14[0xa] */

	/* Slope limit table from arg15[0..10] */
	int32_t slope_limits[11];

	/* Light level thresholds (8 entries) */
	int32_t var_148[8] = { 0, 0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x64 };

	/* var_16c: working curve, var_190: per-block curve, var_1d8: spatial curve */
	int32_t var_16c[9], var_190[9], var_1d8[9];
	int32_t min_curve11[11];
	int32_t ctc_curve9[9];

	/* 8 light levels x 9 kneepoints = 72 entries */
	int32_t var_420[72];

	/* 9-entry monotonic curve for per-block processing */
	int32_t var_1b4[9];

	int32_t v0_12;     /* total pixel count per block */
	int32_t lo_2;      /* histogram sum / pixel count */
	int32_t var_114_1; /* min block mean Y */
	int32_t var_128_1, var_11c_1, var_124_2;

	int i, j, k;
	int result = 1;

	/* === Setup: compute scaling constant === */
	v0_3 = fix_point_div_32(0x10, 0x385b0000, 0x27100000);

	/* === Read arg16 parameters === */
	v0_4   = arg16_s[0];    /* mean Y mode */
	v1_1   = arg16_s[0xa];  /* kneepoint gap[0] */
	v0_5   = arg16_s[1];    /* contrast mode */
	var_128 = arg16_s[2];   /* mean Y / contrast value */
	v1_2   = arg16_s[0xb];  /* kneepoint gap[1] */
	var_11c = arg16_s[3];   /* sigma_bright */
	v0_8   = arg16_s[4];    /* sigma param */
	v1_3   = arg16_s[0xc];  /* kneepoint gap[2] */
	v0_9   = arg16_s[7];    /* light level for per-block curve */
	v1_4   = arg16_s[0xd];  /* kneepoint gap[3] */
	s1     = arg16_s[6];    /* light level for global curve */
	a1_val = arg16_s[9];    /* block H */
	v0_10  = arg16_s[8];    /* block W */
	t2     = arg16_s[0xe];  /* accumulate mode */

	/* === Read arg15 parameters === */
	for (i = 0; i < 11; i++)
		slope_limits[i] = arg15_s[i];

	v1_5   = arg15_s[0x12];
	v1_6   = arg15_s[0x13];
	v1_7   = arg15_s[0x14];
	v1_8   = arg15_s[0x15];
	v1_9   = arg15_s[0x16];
	v1_10  = arg15_s[0x17];
	v1_11  = arg15_s[0x18];
	v1_12  = arg15_s[0x19];
	v1_13  = arg15_s[0x1c];
	s6_1   = arg15_s[0xe];
	t0     = arg15_s[0x1a];

	/* === Read arg14 parameters === */
	a0_13  = arg14_s[0];
	a0_14  = arg14_s[1];
	a0_15  = arg14_s[2];
	a0_16  = arg14_s[3];
	a0_17  = arg14_s[4];
	var_124_1 = arg14_s[5];
	a0_19  = arg14_s[6];
	a0_20  = arg14_s[7];
	a0_21  = arg14_s[8];
	a0_22  = arg14_s[9];
	v1_15  = arg14_s[0xa];

	/* === Initialize 3 working copies of arg10 base LUT === */
	for (i = 0; i < 9; i++) {
		int32_t val = arg10_s[i];
		var_16c[i] = val;
		var_190[i] = val;
		var_1d8[i] = val;
	}

	/* === Compute pixel count per block === */
	v0_12 = (int32_t)v0_10 * a1_val / 4;
	if (v0_12 < 0x1400)
		v0_12 = 0x1400;

	/* === Sum first 48 entries of arg8, compute mean === */
	{
		int32_t sum = 0;
		for (i = 0; i < 0x30; i++)
			sum += arg8_s[i];
		lo_2 = sum / v0_12;
	}

	/* === Build cumulative histogram in adr_hist_512 === */
	{
		int32_t cum = 0;
		for (i = 0; i < 512; i++) {
			cum += arg7_s[i];
			if (cum >= v0_12)
				cum = v0_12;
			adr_hist_512[i] = cum;
		}
	}

	/* === Store histogram metric === */
	arg15_s[0xb] = lo_2;

	/* === Normalize histogram into data_980b0 === */
	{
		int32_t denom = v0_12 >> 8;
		for (i = 0; i < 512; i++) {
			int32_t num = ((int32_t)adr_hist_512[i]) << 8;
			data_980b0[i] = (num / denom) * 0x2710 / 0x10000;
		}
	}

	/* === Extract block histogram data from arg9 === */
	/* arg9 layout: starts at offset 0x78 bytes (30 words).
	 * For each of 6 rows (i_4 from 0 to 0x1e0 step 0x50=80):
	 *   Inner loop j=1..4: reads 5 values per iteration from arg9
	 *   Each row produces 20 histogram entries in adr_block_hist_120 */
	{
		uint8_t *s7 = (uint8_t *)arg9 + 0x78;
		for (i = 0; i < 6; i++) {
			uint32_t *src = (uint32_t *)s7;
			int32_t *dst = (int32_t *)&adr_block_hist_120[i * 20];

			/* OEM HLIL:
			 *   dst[0] = *(src + 0x14)
			 *   dst[1] = *(src + 0x18)
			 *   dst[2] = *(src + 0x1c)
			 *   src += 0x14
			 *   dst[3] = *(src + 0x0c)
			 *   dst[4] = *(src + 0x10)
			 * repeated 4 times per row. */
			for (j = 0; j < 4; j++) {
				dst[0] = src[5];
				dst[1] = src[6];
				dst[2] = src[7];
				src += 5;
				dst[3] = src[3];
				dst[4] = src[4];
				dst += 5;
			}
			s7 += 0x78;
		}
	}

	/* === Extract per-block Y values from arg8 into adr_block_y_24 === */
	{
		uint32_t *src = &arg8[6];
		for (i = 0; i < 6; i++) {
			for (j = 0; j < 4; j++)
				adr_block_y_24[i * 4 + j] = src[j + 1];
			src += 6;
		}
	}

	/* === Accumulate mode (t2 == 1): add corner/edge adjustments === */
	if (t2 == 1) {
		adr_block_y_24[0] += arg8_s[0] + arg8_s[1] + arg8_s[6];
		adr_block_y_24[1] += arg8_s[2];
		adr_block_y_24[2] += arg8_s[3];
		adr_block_y_24[3] += arg8_s[4] + arg8_s[5] + arg8_s[0xb];
		adr_block_y_24[4] += arg8_s[0xc];
		adr_block_y_24[7] += arg8_s[0x11];
		adr_block_y_24[8] += arg8_s[0x12];
		adr_block_y_24[11] += arg8_s[0x17];
		adr_block_y_24[12] += arg8_s[0x18];
		adr_block_y_24[15] += arg8_s[0x1d];
		adr_block_y_24[16] += arg8_s[0x1e];
		adr_block_y_24[19] += arg8_s[0x23];
		adr_block_y_24[20] += arg8_s[0x24] + arg8_s[0x2a] + arg8_s[0x2b];
		adr_block_y_24[21] += arg8_s[0x2c];
		adr_block_y_24[22] += arg8_s[0x2d];
		adr_block_y_24[23] += arg8_s[0x29] + arg8_s[0x2e] + arg8_s[0x2f];
	}

	/* === Compute per-block mean luminance === */
	{
		int32_t max_y = 0, min_y = 0xfff;
		int max_idx = 0, min_idx = 0;
		int32_t div = v0_12 / 0x18;

		for (i = 0; i < 24; i++) {
			int32_t mean = (int32_t)adr_block_y_24[i] / div;
			block_mean_y[i] = mean;
			if (mean > max_y) {
				max_y = mean;
				max_idx = i;
			}
			if (mean < min_y) {
				min_y = mean;
				min_idx = i;
			}
		}
		var_114_1 = min_y;
		arg15_s[0xc] = max_y * 0x3e8 + max_idx;
		arg15_s[0xd] = min_y * 0x3e8 + min_idx;
	}

	/* === Compute 8x9 kneepoint table (var_420) ===
	 * For each of 8 light levels, call subsection() to compute 9 kneepoints.
	 * OEM: calls (&data_20000 - 0x428c) = subsection at 0x1bd74 */
	for (i = 0; i < 8; i++) {
		int32_t result[9];
		subsection(result, var_148[i],
			   (int16_t *)arg11, (int16_t *)arg12,
			   (int32_t *)data_980b0, 8, 0xa, 0x10, t0);
		for (j = 0; j < 9; j++)
			var_420[i * 9 + j] = result[j];
	}

	/* === Main curve computation — two paths based on s6_1 === */
	if (s6_1 != 1) {
		/* ====== Path B (s6_1 != 1): Direct interpolation ======
		 * Search var_148[0..7] for the bracket containing s1,
		 * then interpolate or copy the 9-entry curve. */
		int found = 0;
		int idx;
		int32_t v0_172;

		for (idx = 0; idx < 8; idx++) {
			int32_t thresh = var_148[idx];
			int32_t base = idx * 9;

			if (s1 == thresh) {
				/* Exact match: copy directly */
				for (k = 0; k < 9; k++)
					var_16c[k] = var_420[base + k];
				v0_172 = var_16c[0];
				found = 1;
				break;
			}
			if (s1 < thresh) {
				if (idx != 0) {
					/* Interpolate between idx-1 and idx */
					int32_t ev_lo = var_148[idx - 1];
					int32_t ev_hi = thresh;
					/* OEM EXACT: only entries 1..4 come from the light-level table
					 * in the interpolated case. Entry 0 and entries 5..8 stay on the
					 * arg10-derived base curve initialized at function entry. */
					var_16c[1] = interpolate_adr_x8_y12(ev_lo, ev_hi,
						var_420[(idx - 1) * 9 + 1],
						var_420[base + 1], s1);
					var_16c[2] = interpolate_adr_x8_y12(ev_lo, ev_hi,
						var_420[(idx - 1) * 9 + 2],
						var_420[base + 2], s1);
					var_16c[3] = interpolate_adr_x8_y12(ev_lo, ev_hi,
						var_420[(idx - 1) * 9 + 3],
						var_420[base + 3], s1);
					var_16c[4] = interpolate_adr_x8_y12(ev_lo, ev_hi,
						var_420[(idx - 1) * 9 + 4],
						var_420[base + 4], s1);
				} else {
					/* Below first level: use first entry */
					for (k = 0; k < 9; k++)
						var_16c[k] = var_420[k];
				}
				v0_172 = var_16c[0];
				found = 1;
				break;
			}
		}
		if (!found) {
			/* Above all levels: use last entry (level 7) */
			for (k = 0; k < 9; k++)
				var_16c[k] = var_420[7 * 9 + k];
			v0_172 = var_16c[0];
		}

		/* === Monotonicity enforcement on var_16c === */
		if (v0_172 <= 0)
			var_16c[0] = 0;
		{
			int32_t prev = var_16c[0];
			for (k = 1; k < 9; k++) {
				if (prev < var_16c[k]) {
					prev = var_16c[k];
				} else {
					var_16c[k] = prev + 1;
					prev = var_16c[k];
				}
			}
		}

		/* === Compute global output curve (arg4) === */
		adr_compute_output_curve(var_16c, arg10_s, arg3_s, min_curve11);
		adr_store_min_curve11(min_curve11, arg4_s);

		/* === Per-block curve: find bracket for v0_9 in var_148 ===
		 * Then compute var_190 (9-entry curve for per-block weighting) */
		{
			int blk_found = 0;
			int blk_idx = 0;

			for (blk_idx = 0; blk_idx < 8; blk_idx++) {
				int32_t thresh = var_148[blk_idx];

				if (v0_9 == thresh) {
					int32_t base = blk_idx * 9;
					for (k = 0; k < 9; k++)
						var_190[k] = var_420[base + k];
					blk_found = 1;
					break;
				}
				if (v0_9 < thresh) {
					if (blk_idx != 0) {
						int32_t base = blk_idx * 9;
						int32_t ev_lo = var_148[blk_idx - 1];
						int32_t ev_hi = thresh;
						int32_t seg_range = (ev_hi - ev_lo) << 0xa;
						int32_t seg_pos = (v0_9 - ev_lo) << 0xa;

						/* OEM EXACT: Only interpolate entries 1..4.
						 * Entries 0 and 5-8 retain their arg10 init values
						 * (set at the top of the function), NOT from var_420. */
						for (k = 1; k <= 4; k++) {
							int32_t val_hi = var_420[base + k];
							int32_t val_lo = var_420[base - 9 + k];
							int32_t diff, interp;
							int sign;
							if (val_lo >= val_hi) {
								diff = val_lo - val_hi;
								sign = 0;
							} else {
								diff = val_hi - val_lo;
								sign = 1;
							}
							interp = (int32_t)(fix_point_mult2_32(0xa,
								fix_point_div_32(0xa,
									diff << 0xa, seg_range),
								seg_pos) + 0x200) >> 0xa;
							var_190[k] = sign ? val_lo + interp : val_lo - interp;
						}
					} else {
						for (k = 0; k < 9; k++)
							var_190[k] = var_420[k];
					}
					blk_found = 1;
					break;
				}
			}
			if (!blk_found) {
				for (k = 0; k < 9; k++)
					var_190[k] = var_420[7 * 9 + k];
			}
		}

		/* Monotonicity on var_190, min 1 for entry[1] */
		if (var_190[1] < 2)
			var_190[1] = 1;
		/* Copy to var_1b4 with monotonicity enforcement */
		{
			int32_t prev = var_190[0];
			var_1b4[0] = prev;
			for (k = 1; k < 9; k++) {
				if (k < 8 && prev >= var_190[k]) {
					var_190[k] = prev + 1;
				}
				var_1b4[k] = var_190[k];
				prev = var_190[k];
			}
		}

		/* === Per-block 9-point interpolation === */
		adr_interp_9pt(arg1_s, arg2_s, arg13_s, var_1b4, 8);
	} else {
		/* ====== Path A (s6_1 == 1): OEM subsection path ====== */
		uint32_t subsection_up_curve[9];
		uint32_t blended_curve[9];

		subsection_up(subsection_up_curve, adr_const_table_6b200,
			      data_980b0, 8);
		subsection_light(subsection_up_curve, adr_const_table_6b224,
				 v0_9 * 10, blended_curve);

		for (k = 0; k < 9; k++)
			var_16c[k] = blended_curve[k];

		if (var_16c[0] <= 0)
			var_16c[0] = 0;
		for (k = 1; k < 9; k++) {
			if (var_16c[k - 1] >= var_16c[k])
				var_16c[k] = var_16c[k - 1] + 1;
		}

		adr_compute_output_curve(var_16c, arg10_s, arg3_s, min_curve11);
		adr_store_min_curve11(min_curve11, arg4_s);

		subsection_light(subsection_up_curve, adr_const_table_6b224,
				 0, (uint32_t *)var_190);
		for (k = 0; k < 9; k++)
			var_1b4[k] = var_190[k];

		/* OEM special-case: x < curve[0] holds arg13[0] instead of scaling down. */
		adr_interp_9pt(arg1_s, ctc_curve9, arg13_s, var_1b4, 9);
		for (k = 0; k < 9; k++) {
			if (arg1_s[k] < var_1b4[0])
				ctc_curve9[k] = arg13_s[0];
		}
		adr_store_ctc_curve9(ctc_curve9, arg2_s);
	}

	{
		static int adr_curve_log;

		if (adr_curve_log < 3 || (adr_curve_log % 300) == 0) {
			pr_info("ADR_X_CTC[%d]: %d,%d,%d,%d,%d,%d,%d,%d,%d\n",
				adr_curve_log,
				arg1_s[0], arg1_s[1], arg1_s[2], arg1_s[3], arg1_s[4],
				arg1_s[5], arg1_s[6], arg1_s[7], arg1_s[8]);
			pr_info("ADR_CTCSRC[%d]: %d,%d,%d,%d,%d,%d,%d,%d,%d\n",
				adr_curve_log,
				arg13_s[0], arg13_s[1], arg13_s[2], arg13_s[3], arg13_s[4],
				arg13_s[5], arg13_s[6], arg13_s[7], arg13_s[8]);
			pr_info("ADR_CTCY[%d]: %d,%d,%d,%d,%d,%d,%d,%d last=%u\n",
				adr_curve_log,
				arg2_s[0], arg2_s[1], arg2_s[2], arg2_s[3],
				arg2_s[4], arg2_s[5], arg2_s[6], arg2_s[7], data_9f0cc);
			pr_info("ADR_CURVE_BASE[%d]: %d,%d,%d,%d,%d,%d,%d,%d,%d\n",
				adr_curve_log,
				arg10_s[0], arg10_s[1], arg10_s[2], arg10_s[3], arg10_s[4],
				arg10_s[5], arg10_s[6], arg10_s[7], arg10_s[8]);
			pr_info("ADR_CURVE_G9[%d]: %d,%d,%d,%d,%d,%d,%d,%d,%d\n",
				adr_curve_log,
				var_16c[0], var_16c[1], var_16c[2], var_16c[3], var_16c[4],
				var_16c[5], var_16c[6], var_16c[7], var_16c[8]);
			pr_info("ADR_CURVE_OUT11[%d]: %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
				adr_curve_log,
				arg4_s[0], arg4_s[1], arg4_s[2], arg4_s[3], arg4_s[4],
				arg4_s[5], arg4_s[6], arg4_s[7], arg4_s[8], arg4_s[9],
				data_9f0a8);
			pr_info("ADR_CURVE_BLK9[%d]: %d,%d,%d,%d,%d,%d,%d,%d,%d\n",
				adr_curve_log,
				var_1b4[0], var_1b4[1], var_1b4[2], var_1b4[3], var_1b4[4],
				var_1b4[5], var_1b4[6], var_1b4[7], var_1b4[8]);
		}
		adr_curve_log++;
	}

	/* === Mean Y / contrast parameter computation (OEM: $v0_171 switch) ===
	 * Based on v0_4 (arg16[0]):
	 *   0: no change to var_128
	 *   1: var_128 = lo_2 (histogram mean)
	 *   2: var_128 = a0_13 (arg14[0])
	 *   3: var_128 = blend(a0_13, lo_2, a0_14) */
	switch (v0_4) {
	case 1:
		var_128 = lo_2;
		break;
	case 2:
		var_128 = a0_13;
		break;
	case 3: {
		uint32_t blend = (uint32_t)a0_13 * (uint32_t)a0_14 +
				 (uint32_t)(0x100 - a0_14) * (uint32_t)lo_2;
		var_128 = (int32_t)(blend / 0x100);
		break;
	}
	default:
		break;
	}

	/* === Contrast/sigma parameter computation ===
	 * Complex multi-mode logic for var_128_1, var_11c_1, var_124_2
	 * Based on v1_5 (clamp flag), v0_5 (contrast mode), a0_17 (sigma mode) */
	if (v1_5 != 1) {
		/* Non-clamped path */
		if (var_128 < 0x14)
			var_128_1 = 0x14;
		else if (var_128 >= 0xfa1)
			var_128_1 = 0xfa0;
		else
			var_128_1 = var_128;

		/* Sigma bright computation based on v0_5 */
		if (v0_5 == 0) {
			/* No override, use var_11c as-is */
		} else if (v0_5 == 1) {
			var_11c = (var_128_1 - var_114_1) / 2;
		} else if (v0_5 == 2) {
			var_11c = a0_15;
		} else if (v0_5 == 3) {
			uint32_t blend = (uint32_t)a0_15 * (uint32_t)a0_16 +
					 (uint32_t)((var_128_1 - var_114_1) / 2) *
					 (uint32_t)(0x100 - a0_16);
			var_11c = (int32_t)(blend / 0x100);
		}

		/* Clamp var_11c */
		if (var_11c < 0x14)
			var_11c_1 = 0x14;
		else if (var_11c >= 0x259)
			var_11c_1 = 0x258;
		else
			var_11c_1 = var_11c;

		/* Sigma light computation based on a0_17 */
		if (a0_17 == 1) {
			var_124_1 = (v0_8 + 0xc8) / 2;
		} else if (a0_17 == 2) {
			var_124_1 = v0_8;
		} else if (a0_17 == 3) {
			uint32_t blend = (uint32_t)var_124_1 * (uint32_t)a0_19 +
					 (uint32_t)((v0_8 + 0xc8) / 2) *
					 (uint32_t)(0x100 - a0_19);
			var_124_1 = (int32_t)(blend / 0x100);
		}

		/* Clamp var_124 */
		if (var_124_1 < 0x14)
			var_124_2 = 0x14;
		else if (var_124_1 >= 0x259)
			var_124_2 = 0x258;
		else
			var_124_2 = var_124_1;
	} else {
		/* Clamped path (v1_5 == 1) */
		int32_t clamped = var_128;
		if (clamped < v1_6)
			clamped = v1_6;
		if (clamped < v1_7)
			; /* keep clamped */
		else
			clamped = v1_7;
		var_128_1 = clamped;

		/* Sigma bright with clamp */
		if (v0_5 != 0) {
			if (v0_5 == 1) {
				var_11c = (var_128_1 - var_114_1) / 2;
			} else if (v0_5 == 2) {
				var_11c = a0_15;
			} else if (v0_5 == 3) {
				uint32_t blend = (uint32_t)a0_15 * (uint32_t)a0_16 +
						 (uint32_t)((var_128_1 - var_114_1) / 2) *
						 (uint32_t)(0x100 - a0_16);
				var_11c = (int32_t)(blend / 0x100);
			}
		}
		{
			int32_t sc = var_11c;
			if (sc < v1_8) sc = v1_8;
			if (sc < v1_9) ; else sc = v1_9;
			var_11c_1 = sc;
		}

		/* Sigma light with clamp */
		if (a0_17 != 0) {
			if (a0_17 == 1)
				var_124_1 = (v0_8 + 0xc8) / 2;
			else if (a0_17 == 2)
				var_124_1 = v0_8;
			else if (a0_17 == 3) {
				uint32_t blend = (uint32_t)var_124_1 * (uint32_t)a0_19 +
						 (uint32_t)((v0_8 + 0xc8) / 2) *
						 (uint32_t)(0x100 - a0_19);
				var_124_1 = (int32_t)(blend / 0x100);
			}
		}
		{
			int32_t lc = var_124_1;
			if (lc < v1_10) lc = v1_10;
			if (lc < v1_11) ; else lc = v1_11;
			var_124_2 = lc;
		}
	}

	/* Store computed parameters back to arg15 */
	arg15_s[0xf] = var_128_1;
	arg15_s[0x10] = var_11c_1;
	arg15_s[0x11] = var_124_2;

	/* === Gaussian spatial weighting: per-block output curves ===
	 * For each of 24 blocks, compute spatial weight based on distance
	 * from the block's mean_y to var_128_1, then use tisp_math_exp2
	 * to generate a Gaussian weight. Blend between per-block and global curves. */
	{
		int32_t *fp = arg6_s;  /* output: 24 blocks x 11 entries */
		int32_t *out_ptr = fp;
		int32_t v0_248 = v1_13 << 0x10;
		int32_t *bmy_ptr = (int32_t *)block_mean_y;
		int32_t mapped_weight_grid[24] = {0};
		int32_t max_sq = 0x3ffffc;

		{
			static int adr_wt_log;
			if (adr_wt_log < 5 || (adr_wt_log % 300) == 0) {
				pr_info("ADR_WT[%d]: v1_13=%d v0_3=%d var_128_1=%d var_11c_1=%d var_124_2=%d v0_248=0x%x s6_1=%d v0_9=%d s1=%d\n",
					adr_wt_log, v1_13, v0_3, var_128_1, var_11c_1, var_124_2, v0_248, s6_1, v0_9, s1);
			}
			adr_wt_log++;
		}

		for (i = 0; i < 24; i++) {
			int32_t bmy = *bmy_ptr;
			int32_t weight;

			/* Ensure minimum block_mean_y of 1 */
			if (bmy < 2) {
				bmy = 1;
				block_mean_y[i] = 1;
			}

			/* Compute Gaussian weight based on distance to var_128_1 */
			if (var_128_1 >= bmy) {
				int32_t d = var_128_1 - bmy;
				int32_t d_sq = d * d;
				int32_t sigma_sq;
				int32_t exp_arg, exp_val;

				if (d_sq >= 0x3ffffd)
					d_sq = max_sq;

				sigma_sq = ((var_11c_1 << 1) * var_11c_1) << 0xa;
				exp_arg = fix_point_div_32(0xa, d_sq << 0xa, sigma_sq);
				exp_val = fix_point_mult2_32(0x10, exp_arg << 6, v0_3);
				if (exp_val >= 0xf0000)
					exp_val = 0xeffff;

				weight = ((v0_248 - fix_point_div_32(0x10, v0_248,
					tisp_math_exp2(exp_val, 0x10, 0x10))) >> 0x10) + v1_13;
			} else {
				int32_t d = bmy - var_128_1;
				int32_t d_sq = d * d;
				int32_t sigma_sq;
				int32_t exp_arg, exp_val;

				if (d_sq >= 0x3ffffd)
					d_sq = max_sq;

				sigma_sq = ((var_124_2 << 1) * var_124_2) << 0xa;
				exp_arg = fix_point_div_32(0xa, d_sq << 0xa, sigma_sq);
				exp_val = fix_point_mult2_32(0x10, exp_arg << 6, v0_3);
				if (exp_val >= 0xf0000)
					exp_val = 0xeffff;

				weight = fix_point_div_32(0x10, v0_248,
					tisp_math_exp2(exp_val, 0x10, 0x10)) >> 0x10;
			}

			/* Clamp weight to 0x2710 (10000) max */
			if (weight >= 0x2711)
				weight = 0x2710;

			/* Map weight through normalized histogram.
			 * OEM EXACT: uses fix_point_div_32, NOT mult.
			 * 0x19000 = 102400 = 100*1024. Division normalizes
			 * weight (0..10000) into an index for tone curve
			 * interpolation. Multiplication overflows → garbage. */
			{
				int32_t mapped_weight = (int32_t)(fix_point_div_32(0xa,
					weight << 0xa, 0x19000) + 0x200) >> 0xa;

				int32_t blk_found2;
				mapped_weight_grid[i] = mapped_weight;

				{
					static int adr_blk_log;
					if (adr_blk_log < 48) {
						pr_info("ADR_BLK[%d/%d]: bmy=%d wt=%d mw=%d\n",
							adr_blk_log / 24, i, bmy, weight, mapped_weight);
					}
					adr_blk_log++;
				}

				/* Find bracket in var_148 for mapped_weight, interpolate var_1d8 */
				blk_found2 = 0;
				for (j = 0; j < 8; j++) {
					int32_t thresh = var_148[j];
					if (mapped_weight == thresh) {
						int32_t base = j * 9;
						for (k = 0; k < 9; k++)
							var_1d8[k] = var_420[base + k];
						blk_found2 = 1;
						break;
					}
					if (mapped_weight < thresh) {
						if (j != 0) {
							int32_t base = j * 9;
							int32_t ev_lo = var_148[j - 1];
							int32_t seg = (thresh - ev_lo) << 0xa;
							int32_t pos = (mapped_weight - ev_lo) << 0xa;
							/* OEM HLIL only rewrites kneepoints 1..4 in the
							 * interpolated spatial path. Entries 0 and 5..8
							 * retain the current working state. */
							for (k = 1; k <= 4; k++) {
								int32_t vhi = var_420[base + k];
								int32_t vlo = var_420[base - 9 + k];
								int32_t diff, interp;
								int sign;
								if (vlo >= vhi) {
									diff = vlo - vhi;
									sign = 0;
								} else {
									diff = vhi - vlo;
									sign = 1;
								}
								interp = (int32_t)(fix_point_mult2_32(0xa,
									fix_point_div_32(0xa,
										diff << 0xa, seg),
									pos) + 0x200) >> 0xa;
								var_1d8[k] = sign ? vlo + interp : vlo - interp;
							}
						} else {
							for (k = 0; k < 9; k++)
								var_1d8[k] = var_420[k];
						}
						blk_found2 = 1;
						break;
					}
				}
				if (!blk_found2) {
					for (k = 0; k < 9; k++)
						var_1d8[k] = var_420[7 * 9 + k];
				}
			}

			/* Monotonicity on var_1d8 */
			if (var_1d8[0] < 2)
				var_1d8[0] = 1;
			{
				int32_t prev = var_1d8[0];
				for (k = 1; k < 9; k++) {
					if (prev >= var_1d8[k]) {
						var_1d8[k] = prev + 1;
					}
					prev = var_1d8[k];
				}
			}

			/* Compute per-block output curve (11 entries) using 7-segment
			 * piecewise interpolation through var_1d8 kneepoints */
			{
				int32_t knee0 = var_1d8[0];
				int32_t knee1 = var_1d8[1] - knee0;
				int32_t knee2_gap = var_1d8[2] - var_1d8[1];
				int32_t knee3_gap = var_1d8[3] - var_1d8[2];
				int32_t knee4_gap = var_1d8[4] - var_1d8[3];
				int32_t knee8 = var_1d8[8];
				int32_t y0 = arg10_s[0];
				int32_t y_last = arg10_s[8];
				int32_t gap0 = knee1;
				int32_t s4b, s2b, s3b, s1b;

				s4b = y0 + gap0;
				if (v1_1 < gap0)
					s4b = y0 + gap0;
				else
					s4b = y0 + v1_1;

				s2b = v1_2 + s4b;
				if (v1_2 < knee2_gap)
					s2b = s4b + knee2_gap;

				s3b = v1_3 + s2b;
				if (v1_3 < knee3_gap)
					s3b = s2b + knee3_gap;

				s1b = v1_4 + s3b;
				if (v1_4 < knee4_gap)
					s1b = s3b + knee4_gap;
				/* no 0xfff clamp here: vendor 0x1e000..0x1e0d8 (this block path) has none; only the
				 * tm-base curves (0x1cdb0, 0x1e658) clamp (openimp #563, oracle block adrfpga) */

				for (j = 0; j < 11; j++) {
					int32_t x = arg5_s[j];
					int32_t out;

					if (x < knee0) {
						out = y0 - ((int32_t)(fix_point_mult2_32(0xa,
							fix_point_div_32(0xa,
								y0 << 0xa, knee0 << 0xa),
							(knee0 - x) << 0xa) + 0x200) >> 0xa);
					} else if (x < var_1d8[1]) {
						out = s4b - ((int32_t)(fix_point_mult2_32(0xa,
							fix_point_div_32(0xa,
								(s4b - y0) << 0xa,
								gap0 << 0xa),
							(var_1d8[1] - x) << 0xa) + 0x200) >> 0xa);
					} else if (x < var_1d8[2]) {
						out = s2b - ((int32_t)(fix_point_mult2_32(0xa,
							fix_point_div_32(0xa,
								(s2b - s4b) << 0xa,
								knee2_gap << 0xa),
							(var_1d8[2] - x) << 0xa) + 0x200) >> 0xa);
					} else if (x < var_1d8[3]) {
						out = s3b - ((int32_t)(fix_point_mult2_32(0xa,
							fix_point_div_32(0xa,
								(s3b - s2b) << 0xa,
								knee3_gap << 0xa),
							(var_1d8[3] - x) << 0xa) + 0x200) >> 0xa);
					} else if (x < var_1d8[4]) {
						out = s1b - ((int32_t)(fix_point_mult2_32(0xa,
							fix_point_div_32(0xa,
								(s1b - s3b) << 0xa,
								knee4_gap << 0xa),
							(var_1d8[4] - x) << 0xa) + 0x200) >> 0xa);
					} else if (x >= knee8) {
						out = y_last;
					} else {
						out = y_last - ((int32_t)(fix_point_mult2_32(0xa,
							fix_point_div_32(0xa,
								(y_last - s1b) << 0xa,
								(knee8 - var_1d8[4]) << 0xa),
							(knee8 - x) << 0xa) + 0x200) >> 0xa);
					}
					out_ptr[j] = out;
				}

				/* === Slope limiting (when v1_12 == 1) === */
				if (v1_12 == 1) {
					for (j = 0; j < 11; j++) {
						int32_t x_cur = arg5_s[j];
						int32_t max_slope_val = slope_limits[j];

						if (j != 0) {
							int32_t x_prev = arg5_s[j - 1];
							int32_t x_diff = x_cur - x_prev;
							int32_t y_prev = out_ptr[j - 1];
							int32_t y_cur = out_ptr[j];
							int32_t actual_slope;

							actual_slope = ((y_cur - y_prev) << 0x10) / x_diff;
							if (max_slope_val < actual_slope) {
								out_ptr[j] = (x_diff * max_slope_val + (y_prev << 0x10)) / 0x10000;
							}
						} else {
							int32_t actual_slope;

							actual_slope = (out_ptr[0] << 0x10) / x_cur;
							if (max_slope_val < actual_slope) {
								out_ptr[0] = x_cur * max_slope_val / 0x10000;
							}
						}
					}
				}

				if (i == 0) {
					static int adr_blk_curve_log;

					if (adr_blk_curve_log < 5 || (adr_blk_curve_log % 300) == 0) {
						pr_info("ADR_BLK9[%d]: %d,%d,%d,%d,%d,%d,%d,%d,%d\n",
							adr_blk_curve_log,
							var_1d8[0], var_1d8[1], var_1d8[2], var_1d8[3], var_1d8[4],
							var_1d8[5], var_1d8[6], var_1d8[7], var_1d8[8]);
						pr_info("ADR_BLKOUT11[%d]: %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
							adr_blk_curve_log,
							out_ptr[0], out_ptr[1], out_ptr[2], out_ptr[3], out_ptr[4],
							out_ptr[5], out_ptr[6], out_ptr[7], out_ptr[8], out_ptr[9],
							out_ptr[10]);
					}
					adr_blk_curve_log++;
				}
			}

			bmy_ptr++;
			out_ptr += 11;
		}

		{
			static int adr_preblend_log;

			if (adr_preblend_log < 5 || (adr_preblend_log % 300) == 0) {
				int32_t hist_idx = v1_15;
				int32_t hist_val = 0;

				if (hist_idx >= 0 && hist_idx < 512)
					hist_val = data_980b0[hist_idx];

				pr_info("ADR_BLEND[%d]: en=%d idx=%d hist=%d lo=%d hi=%d path=%d t0=%d mean0=%d mean5=%d mean12=%d mean23=%d\n",
					adr_preblend_log, a0_20, hist_idx, hist_val, a0_21, a0_22,
					s6_1, t0, block_mean_y[0], block_mean_y[5],
					block_mean_y[12], block_mean_y[23]);
				adr_log_curve11("ADR_PRE11", adr_preblend_log, 0, (uint32_t *)&arg6_s[0 * 11]);
				adr_log_curve11("ADR_PRE11", adr_preblend_log, 5, (uint32_t *)&arg6_s[5 * 11]);
				adr_log_curve11("ADR_PRE11", adr_preblend_log, 12, (uint32_t *)&arg6_s[12 * 11]);
				adr_log_curve11("ADR_PRE11", adr_preblend_log, 23, (uint32_t *)&arg6_s[23 * 11]);
			}
			adr_preblend_log++;
		}

			/* === Final blending (when a0_20 == 1) ===
			 * OEM exact: blend per-block Y curves toward arg5_s, which is the
			 * shared map_kneepoint_x axis. This looks counterintuitive, but the
			 * decompiled OEM routine does in fact use arg5 here, not arg4_s. */
			if (a0_20 == 1) {
				result = (int32_t)data_980b0[v1_15];

				if (result >= a0_21) {
					if (a0_22 >= result) {
						/* Blend proportionally. */
						int32_t blend_amt = result - a0_21;
						int32_t blend_range = a0_22 - a0_21;
						int32_t *blk_ptr = arg6_s;

						for (i = 0; i < 24; i++) {
							for (j = 0; j < 11; j++) {
								int32_t blk_y = blk_ptr[j];
								int32_t glob_y = arg5_s[j];
								int32_t diff_val = (blk_y - glob_y) * blend_amt;
								blk_ptr[j] = ((blk_y << 0xa) - ((diff_val << 0xa) / blend_range)) / 0x400;
							}
							blk_ptr += 11;
						}
					} else {
						/* Above threshold: OEM copies arg5_s to all blocks and returns 0. */
						int32_t *blk_ptr = arg6_s;

						result = 0;
						for (i = 0; i < 24; i++) {
							for (j = 0; j < 11; j++)
								blk_ptr[j] = arg5_s[j];
							blk_ptr += 11;
						}
					}
				}
			}

		if (adr_grid_debug_budget > 0) {
			int32_t mean_grid[24];
			int32_t curve_p4[24];
			int32_t curve_p10[24];
			int log_idx = adr_grid_debug_budget;

			for (i = 0; i < 24; i++) {
				mean_grid[i] = (int32_t)block_mean_y[i];
				curve_p4[i] = arg6_s[i * 11 + 4];
				curve_p10[i] = arg6_s[i * 11 + 10];
			}

			adr_log_grid24("ADR_GRID_MEAN", log_idx, mean_grid);
			adr_log_grid24("ADR_GRID_MW", log_idx, mapped_weight_grid);
			adr_log_grid24("ADR_GRID_P4", log_idx, curve_p4);
			adr_log_grid24("ADR_GRID_P10", log_idx, curve_p10);
			adr_grid_debug_budget--;
		}
	}

	return result;
}

#endif /* TX_ISP_T31_ADRFPGA_H */
