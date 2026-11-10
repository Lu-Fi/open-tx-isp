/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 local contrast enhancement (LCE, block 0xc000, plus the 0x25054..0x25070
 * smoothing words) from the IQ bin at AddSensor (S4 step 7): tisp_lce_init ->
 * tisp_lce_malloc_cfg, tisp_lce_size_para_calc, the register setup,
 * tisp_lce_curve_init_default -> tisp_lce_write_all_reg and
 * tisp_lce_write_ctrl_reg (with the 5x5 Gaussian of tisp_lce_gauss_lpf).
 *
 * IQ bin section 15 (712 bytes).  The grid geometry comes from the frame
 * size alone (blocks of 64 x 32 pixels; the 1920x1080 frame takes a
 * different, table driven path of the vendor that is not built here).  The
 * gain dependent strength tables are read at gain index 0 (the vendor's
 * initial state).  The default curve is the 20 byte pattern 0, 8 .. 0x38,
 * 0x40, 0x50 .. 0xf0 repeated 576 times, in both halves of a 0x5a00 byte
 * buffer (t32_lce_curve).
 *
 * Two DMA buffers are needed: the working buffer (0x26400 bytes; the four
 * address registers 0xc090 .. 0xc09c at +0, +0x9900, +0x13200, +0x1cb00)
 * and the curve buffer (0x5a00 bytes, register 0xc0a4); their physical
 * addresses are arguments.
 *
 * Refused: section byte +0x1fb set (the gain dependent strength tables are then interpolated
 * in two dimensions, tisp_lce_tgain_interp_strength; not built) and a grid of 0.
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_lce_oracle).
 */
#ifndef TX_ISP_T32_LCE_H
#define TX_ISP_T32_LCE_H

#include "tx_isp_t32_tp.h"

#define T32_SEC_LCE		15
#define T32_LCE_SEC_BYTES	712U
#define T32_LCE_ID		0x0134dacdU
#define T32_LCE_WORK_BYTES	0x26400U
#define T32_LCE_CURVE_BYTES	0x5a00U
#define T32_LCE_WRITES		33
#define T32_LCE_MAX_WRITES	33

/* the default curve: 576 x the 20 byte pattern, in out (0x2d00 bytes) */
static inline void t32_lce_curve(u8 *out)
{
	u8 pat[20];
	unsigned int i;

	for (i = 0; i < 8; i++)
		pat[i] = (u8)(8 * i);
	for (i = 0; i < 12; i++)
		pat[8 + i] = (u8)(0x40 + 16 * i);
	for (i = 0; i < 576; i++)
		memcpy(out + 20 * i, pat, 20);
}

/* tisp_lce_gauss_calc_exp: e^x in Q8 by 15 series terms with the vendor's 32 bit wrap */
static inline int t32_lce_exp(int x)
{
	int acc = 1 << 8, k;
	long long t = acc;

	for (k = 1; k <= 15; k++) {
		int f = (x / k) >> 2;

		t = (long long)((unsigned long long)t * (unsigned long long)(long long)f);
		t >>= 8;
		acc = (int)((unsigned int)acc + (unsigned int)t);
	}
	return acc;
}

/* tisp_lce_gauss_lpf: the 15 weights of a quadrant of the 9 x 9 kernel for strength sigma */
static inline void t32_lce_lpf(u32 sigma, u16 out[15])
{
	static u32 w[9][9];
	int dy, dx, tot = 0, r, c;
	u32 s2, u6 = 0xe;

	if (sigma < 9)
		sigma = 9;
	s2 = sigma * sigma;
	for (dy = 0; dy < 9; dy++) {
		for (dx = 0; dx < 9; dx++) {
			u32 r2 = (u32)((dy - 4) * (dy - 4) + (dx - 4) * (dx - 4));
			int e = t32_lce_exp((int)((r2 * 0x80000U + (s2 >> 1)) / s2));
			int v = e ? ((e >> 1) + 0x1000000) / e : 0;

			w[dy][dx] = (u32)v;
			tot += v;
		}
	}
	for (r = 0; r < 5; r++) {
		for (c = r; c < 5; c++) {
			unsigned long long num = (unsigned long long)(long long)(int)w[r][c] << 16;

			out[(r + u6 - c) & 0xff] = (u16)t32_tp_div64(num, (unsigned long long)(long long)tot);
		}
		u6 = (u6 + r - 5) & 0xff;
	}
}

/*
 * sec: section 15 (712 bytes, not modified), width/height of the frame,
 * work_pa / curve_pa: physical addresses of the two buffers.  Returns the
 * number of writes (33) or -1 for a refused case.
 */
static inline int t32_lce_init(const u8 *sec, u32 width, u32 height, u32 work_pa, u32 curve_pa,
			       struct t32_front_write *w)
{
	u32 q = height >> 1, r = q % 18, v = (width & 0x3f) >> 1, c2 = (width >> 5) & 0xfe;
	u32 c5 = ((q / 18) & 0x7f) * 2, l[9], p7[10], x, idx, n44[6];
	u16 g[15];
	unsigned int n = 0, i, j;

	if (t32_front_le32(sec) != T32_LCE_ID || sec[0x1fb] ||
	    !c2 || !c5)
		return -1;
	n44[0] = v ? c2 + 2 : c2;
	n44[1] = v ? c2 + 1 : c2;
	n44[2] = c2;
	n44[3] = r ? c5 + 2 : c5;
	n44[4] = r ? c5 + 1 : c5;
	n44[5] = c5;
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			l[3 * i + j] = n44[j] * n44[3 + i];
	if (l[8] > 0x4000) {
		idx = 3;
	} else if (l[8] > 0x2000) {
		idx = 4;
	} else if (l[8] > 0x1000) {
		idx = 5;
	} else if (l[8] > 0x800) {
		idx = 6;
	} else if (l[8] > 0x400) {
		idx = 7;
	} else if (l[8] > 0x200) {
		idx = 8;
	} else if (l[8] > 0x100) {
		idx = 9;
	} else {
		idx = 10;
	}
	x = 0x8000000U >> (idx - 3);
	for (i = 0; i < 9; i++) {
		if (!l[i])
			return -1;
		p7[i] = ((l[i] >> 1) + x) / l[i];
	}
	p7[9] = idx;

	/* tisp_lce_malloc_cfg */
	T32_FWR(w, n, 0xc090, work_pa);
	T32_FWR(w, n, 0xc094, work_pa + 0x9900);
	T32_FWR(w, n, 0xc098, work_pa + 0x13200);
	T32_FWR(w, n, 0xc09c, work_pa + 0x1cb00);
	T32_FWR(w, n, 0xc080, 3);
	/* tisp_lce_init */
	T32_FWR(w, n, 0xc040, c5 << 16 | c2);
	T32_FWR(w, n, 0xc044, (r & 0x1f) << 8 | (v & 0x3f) | (p7[9] & 0xf) << 20);
	T32_FWR(w, n, 0xc048, (p7[1] & 0x1fff) << 16 | (p7[0] & 0x1fff));
	T32_FWR(w, n, 0xc04c, p7[2] & 0x1fff);
	T32_FWR(w, n, 0xc050, (p7[4] & 0x1fff) << 16 | (p7[3] & 0x1fff));
	T32_FWR(w, n, 0xc054, p7[5] & 0x1fff);
	T32_FWR(w, n, 0xc058, (p7[7] & 0x1fff) << 16 | (p7[6] & 0x1fff));
	T32_FWR(w, n, 0xc05c, p7[8] & 0x1fff);
	T32_FWR(w, n, 0xc0a0, 0x280);
	T32_FWR(w, n, 0xc010, 1);
	/* tisp_lce_curve_init_default -> tisp_lce_write_all_reg */
	T32_FWR(w, n, 0xc0a4, curve_pa);
	/* tisp_lce_write_ctrl_reg */
	T32_FWR(w, n, 0xc068, (sec[0x1ac] & 1U) << 4 | (sec[0x1ad] & 3U) << 8 | (sec[0x1ab] & 1U) |
		(sec[0x1ae] & 1U) << 12 | (sec[0x1af] & 1U) << 16);
	for (i = 0; i < 4; i++)
		T32_FWR(w, n, 0xc06c + 4 * i, (t32_tp_le16(sec + 0xf0 + 0x16 * 2 * i) & 0x1ffU) << 16 |
			(t32_tp_le16(sec + 0xda + 0x16 * 2 * i) & 0x1ff));
	T32_FWR(w, n, 0xc07c, t32_tp_le16(sec + 0x18a) & 0x1ff);
	T32_FWR(w, n, 0xc060, (t32_tp_le16(sec + 0x1a2) & 0x3ffU) << 16 | (t32_tp_le16(sec + 0x1a0) & 0x3ff));
	T32_FWR(w, n, 0xc064, (t32_tp_le16(sec + 0x1a6) & 0x3ffU) << 16 | (t32_tp_le16(sec + 0x1a4) & 0x3ff));
	memset(g, 0, sizeof(g));
	if (sec[0x1b0])
		t32_lce_lpf(sec[0x1b0], g);
	for (i = 0; i < 7; i++)
		T32_FWR(w, n, 0x25054 + 4 * i, (u32)g[2 * i] | (u32)g[2 * i + 1] << 16);
	T32_FWR(w, n, 0x25070, g[14]);
	T32_FWR(w, n, 0xc010, 1);
	return (int)n;
}

/*
 * Per-frame helpers (no register writes): tisp_lce_hist_adjust,
 * tisp_lce_16bit_data_converge, tisp_lce_pdf_to_cdf, tisp_lce_hist_method,
 * tisp_lce_thres_search, tisp_lce_std_hist_transform.  The vendor reads a few
 * constant tables (t32_data_d6b50, d6c64/d6c65/d6c67, bf9ec, d6be4, d6b64);
 * the caller fills struct t32_lce_tab from data.c.  Checked by
 * tests/t32_oracle (t32_lceh_oracle).
 */
struct t32_lce_tab {
	u8 adj_base[20];	/* t32_data_d6b50 */
	u8 pdf_shift;		/* t32_data_d6c67 */
	u8 pdf_rnd;		/* t32_data_d6c64 */
	u8 pdf_sh;		/* t32_data_d6c65 */
	u16 w[32];		/* t32_data_bf9ec */
	u32 up[32];		/* t32_data_d6be4 */
	u32 down[32];		/* t32_data_d6b64 */
};

/* tisp_lce_hist_adjust: cur[20] moves towards the base table, scaled by up/down */
static inline void t32_lce_hist_adjust(u8 *cur, const u8 *up, const u8 *down,
				       const struct t32_lce_tab *tab)
{
	unsigned int i;

	for (i = 0; i < 20; i++) {
		u32 base = tab->adj_base[i];
		u32 d = (cur[i] - base) & 0xffff;
		int adj = 0;

		if ((short)d > 0)
			adj = (short)(d * up[i]);
		else if ((short)d < 0)
			adj = (short)(d * down[i]);
		cur[i] = (u8)(((base << 8) + 128 + (u32)adj) >> 8);
	}
}

static inline u32 t32_lce_round7(u32 v)
{
	return ((v >> 6) & 1) + ((int)v >> 7);
}

/* tisp_lce_16bit_data_converge: cur[20] follows target[20], step limited to lo..hi */
static inline void t32_lce_16bit_data_converge(u32 gain, u32 lo, u32 hi, const u8 *target, u8 *cur)
{
	unsigned int i;

	gain &= 0xff;
	lo &= 0xff;
	hi &= 0xff;
	for (i = 0; i < 20; i++) {
		u32 t = target[i], c = cur[i], d;

		if ((int)(t + lo) < (int)c) {
			d = t32_lce_round7(((c - t) & 0xffff) * gain) & 0xffff;
			if (d < lo)
				cur[i] = (u8)(t + lo);
			else if (hi < d)
				cur[i] = (u8)(t + hi);
			else
				cur[i] = (u8)(t + d);
		} else if ((int)(lo + c) < (int)t) {
			d = t32_lce_round7(((t - c) & 0xffff) * gain) & 0xffff;
			if (d < lo)
				cur[i] = (u8)(t - lo);
			else if (hi < d)
				cur[i] = (u8)(t - hi);
			else
				cur[i] = (u8)(t - d);
		}
	}
}

/*
 * tisp_lce_16bit_data_converge_simd: the MXU2 variant of tisp_lce_16bit_data_converge.  Elements 0..15 run
 * as 32 bit lanes, elements 16..19 as scalar code; lo and hi are masked to 8 bits once at the start for both,
 * gain is not masked at all.  All compares are signed 32 bit, d = ((cur - target) * gain + 64) >> 7
 * (arithmetic).  The lanes test hi < d before d < lo, the scalar tail the other way round (they differ only
 * for hi < d < lo, i.e. hi < lo).
 */
static inline void t32_lce_16bit_data_converge_simd(u32 gain, u32 lo, u32 hi, const u8 *target, u8 *cur)
{
	unsigned int i;

	for (i = 0; i < 20; i++) {
		int vec = i < 16;
		int l = lo & 0xff, h = hi & 0xff;
		int t = target[i], c = cur[i], d, r, dir = 0;

		if (t + l < c)
			dir = 1;
		else if (l + c < t)
			dir = -1;
		else
			continue;
		d = (int)((u32)(dir > 0 ? c - t : t - c) * gain + 64) >> 7;
		if (vec)
			r = h < d ? h : (d < l ? l : d);
		else
			r = d < l ? l : (h < d ? h : d);
		cur[i] = (u8)(dir > 0 ? t + r : t - r);
	}
}

/*
 * lce_16bit_data_converge_16bit_simd (0x541b0): the 16 bit lane MXU2 variant of the converge above.  All 20 elements
 * (16 + 4 in a second pass) run as 16 bit lanes: gain, lo and hi are sign extended from 8 bits (not masked), the
 * product |cur - target| * gain wraps to 16 bits, d = signed (p >> 7) rounded by bit 6, all compares are signed
 * 16 bit.  The clamp order is the same for every element: d < lo gives lo, else hi < d gives hi.  Not bit-exact
 * with t32_lce_16bit_data_converge (8 bit masks) nor with the 32 bit _simd variant above (sign extension, wrap, order).
 */
static inline void t32_lce_16bit_data_converge_16bit_simd(u32 gain, u32 lo, u32 hi, const u8 *target, u8 *cur)
{
	unsigned int i;
	u32 g = (u16)(s8)gain;
	int l = (s8)lo, h = (s8)hi;

	for (i = 0; i < 20; i++) {
		int t = target[i], c = cur[i], d, r;
		u32 p = (u32)(c > t ? c - t : t - c) * g;

		d = (int)(s16)p >> 7;
		d += (p >> 6) & 1;
		r = d < l ? l : (h < d ? h : d);
		if (c + l < t)		/* the lanes select this last: it wins when lo < 0 makes both tests true */
			cur[i] = (u8)(t - r);
		else if (t + l < c)
			cur[i] = (u8)(t + r);
	}
}

static inline u32 t32_lce_cdf_val(u32 v, const struct t32_lce_tab *tab)
{
	return ((v >> (tab->pdf_rnd & 31)) & 1) + (v >> (tab->pdf_sh & 31));
}

/* tisp_lce_pdf_to_cdf: 32 bin pdf -> 20 byte cdf; -1 (vendor logs an error, writes nothing) for a zero total */
static inline int t32_lce_pdf_to_cdf(const u16 *pdf, u8 *out, const struct t32_lce_tab *tab)
{
	u16 cum[32];
	u32 sum = 0, k, i;

	for (i = 0; i < 32; i++) {
		cum[i] = (u16)sum;
		sum = (sum + pdf[i]) & 0xffff;
	}
	if (!sum)
		return -1;
	k = (u32)((int)(1U << (tab->pdf_shift & 31)) / (int)sum);
	for (i = 0; i < 8; i++)
		out[i] = (u8)t32_lce_cdf_val(cum[i] * k, tab);
	for (i = 0; i < 12; i++)
		out[8 + i] = (u8)t32_lce_cdf_val(cum[9 + 2 * i] * k, tab);
	return 0;
}

/*
 * lce_pdf_to_cdf_16bit_simd (0x54984): 8 pdf rows of 32 u16 bins (pdf[r * 32 + k]) -> 8 cdf rows of 20 bytes
 * (out[r * 20 + j]).  Unlike t32_lce_pdf_to_cdf there is no normalisation by the total.  Per row a running 32 bit
 * sum: output j is the sum of the bins before it, clamped to 32767 when it reaches 32768, divided by 128 (floor,
 * low byte).  Outputs 0..7 step one bin, outputs 8..19 two bins (bins 8 + 2 (j - 8) and 9 + 2 (j - 8)).
 * The clamp is applied to the sum before each output only, so the sum can run past 32767 inside a two bin step.
 */
static inline void t32_lce_pdf_to_cdf_16bit_simd(const u16 *pdf, u8 *out)
{
	unsigned int r, j;

	for (r = 0; r < 8; r++) {
		const u16 *p = pdf + r * 32;
		u32 sum = 0;

		for (j = 0; j < 20; j++) {
			if (sum > 32767)
				sum = 32767;
			out[r * 20 + j] = (u8)(sum >> 7);
			if (j < 8)
				sum += p[j];
			else
				sum += p[2 * j - 8] + p[2 * j - 7];
		}
	}
}

/*
 * lce_hist_filter_and_judge_16bit_simd (0x54318): for 8 selected histogram rows (hist[idx[k] * 32 + bin], 32 u16 bins
 * per row) the index of the first bin that is not zero (0 for an empty row; the vendor keeps a saturating running
 * sum per lane and latches the bin index where it first becomes > 0).  out[k] is a u16.
 * Not related to any scalar function of this header.
 */
static inline void t32_lce_hist_filter_and_judge_16bit_simd(const u16 *hist, const u16 *idx, u16 *out)
{
	unsigned int k, i;

	for (k = 0; k < 8; k++) {
		out[k] = 0;
		for (i = 0; i < 32; i++) {
			if (hist[idx[k] * 32 + i]) {
				out[k] = (u16)i;
				break;
			}
		}
	}
}

/* lce_self_light_correct_local_simd_test column offsets (rodata d6b50, 20 bytes) */
static const u8 t32_lce_slc_ofs[20] = {
	0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x50,
	0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
};

/*
 * lce_self_light_correct_local_simd_test (0x54bd0): 8 rows x 20 bytes (in[r * 20 + j] -> out[r * 20 + j]).
 * With enable (byte 8 of the vendor's a2 structure) non-zero, the value's deviation from the column offset,
 * d = in - ofs[j], is scaled: d > 0 uses up[j], d < 0 uses down[j], d = (d * k) >> 7 (arithmetic, floor);
 * out = low byte of (d + ofs[j]).  The vendor does this in 16 bit lanes (wrap of the product at 16 bit), which
 * only changes d by multiples of 512 and so never the output byte.  With enable == 0 the
 * result is in unchanged.  Not related to any scalar function of this header.
 */
static inline void t32_lce_self_light_correct_local_simd_test(const u8 *in, u8 *out, u32 enable, const u8 *up,
							      const u8 *down)
{
	unsigned int r, j;

	for (r = 0; r < 8; r++) {
		for (j = 0; j < 20; j++) {
			int d = in[r * 20 + j] - t32_lce_slc_ofs[j];

			if (enable && d > 0)
				d = d * up[j] >> 7;
			else if (enable && d < 0)
				d = d * down[j] >> 7;
			out[r * 20 + j] = (u8)(d + t32_lce_slc_ofs[j]);
		}
	}
}

#define T32_LCE_HM16_SHIFT 20	/* rodata d6c67 (0x14), already within 0..31 */

/* lce_hist_method_16bit_simd: 1024 / n for n = 0..31 (rounded; rodata bf9a8, 32 u16) */
static const u16 t32_lce_hm16_recip[32] = {
	0x400, 0x400, 0x200, 0x155, 0x100, 0x0cd, 0x0ab, 0x092, 0x080, 0x072, 0x066, 0x05d, 0x055, 0x04f, 0x049, 0x044,
	0x040, 0x03c, 0x039, 0x036, 0x033, 0x031, 0x02f, 0x02d, 0x02b, 0x029, 0x027, 0x026, 0x025, 0x023, 0x022, 0x021,
};

/* parameter block of lce_hist_method_16bit_simd (the vendor reads these words of its a2 argument) */
struct t32_lce_hm16_par {
	u32 r0;
	u32 n;		/* +4: low 8 bits, centre bin, 0..31 */
	u32 r2[3];
	u32 c3;		/* +20: low 8 bits, offset of the b ramp */
	u32 b;		/* +24: low 8 bits, slope of the b ramp (scaled by 1024 / n) */
	u32 k8;		/* +28: low 8 bits, histogram gain (128 = 1.0) */
	u32 c1;		/* +32: low 16 bits, offset of the a ramp */
	u32 a;		/* +36: low 16 bits, slope of the a ramp (scaled by 1024 / n) */
	u32 e1;		/* +40: low 16 bits, a at the centre bin */
	u32 e2;		/* +44: low 16 bits, b at the centre bin */
	u32 sh;		/* +48: low 8 bits (used & 31), shift of the over-full branch */
};

/*
 * lce_hist_method_16bit_simd (0x54438): reshape a 32 bin histogram (u16) into out[32] (u16) with a tent:
 * ramp a (offset c1, slope A) and ramp b (offset c3, slope B) rising to the centre bin n and falling again
 * (mirrored, 31 - i), A = (a * 1024/n) >> 10, B = (b * 1024/n) >> 10 as 32 bit values, e1 / e2 at the centre.
 * m[i] = (u16)min(a[i], g[i]) (unsigned), g[i] = (((hist[i] - 1024) * k8) >> 7) + 1024; S = sum m.  If S > 0x8000 only
 * the high four of every eight bins are processed (vendor behaviour, the low four bins of each group are written
 * from the same four inputs); else m[i] + b[i] * (0x8000 - S) / sum(b) (unsigned, ~0 for sum 0).  Finally the
 * remaining 0x8000 - total is spread: h = (rem << 5) >> 10 on every bin and rem % 32 on bin 31.
 * v = (sum << 5 >> 14) + 1 is at least 65 there (and sum < 2^26 for every parameter), so the vendor's divide-by-zero
 * handling is never reached.  Only n <= 31 is supported (the vendor overflows its stack arrays above).  The vendor's memcpy of a 64 byte
 * table into its scratch buffer (bf860) is overwritten completely before it is read and is not ported.
 * Not related to t32_lce_hist_method (different algorithm).
 */
static inline void t32_lce_hist_method_16bit_simd(const u16 *hist, u16 *out, const struct t32_lce_hm16_par *p)
{
	u32 n = p->n & 0xff, c1 = p->c1 & 0xffff, c3 = p->c3 & 0xff, k8 = p->k8 & 0xff;
	u32 ra = (p->a & 0xffff) * t32_lce_hm16_recip[n & 31], rb = (p->b & 0xff) * t32_lce_hm16_recip[n & 31];
	u32 sa = (u32)((int)ra >> 10), sb = (u32)((int)rb >> 10);
	u32 a[32], b[32], bsum = 0, sum = 0, tot = 0, rem, i;
	u16 m[32];
	u16 buf[32];
	int h;

	for (i = 0; i < 32; i++) {
		u32 x = i < n ? i : 31 - i;

		a[i] = i == n ? p->e1 & 0xffff : c1 + sa * x;
		b[i] = i == n ? p->e2 & 0xffff : c3 + sb * x;
		bsum += b[i];
	}
	for (i = 0; i < 32; i++) {
		u32 g = (u32)(((int)((hist[i] - 1024U) * k8)) >> 7) + 1024U;

		g = a[i] < g ? a[i] : g;
		m[i] = (u16)g;
		sum += g;
	}
	rem = 0x8000U - sum;
	if ((int)rem < 0) {
		u32 sh = 1U << T32_LCE_HM16_SHIFT, v = (u32)(((int)(sum << 5)) >> 14) + 1;
		u32 r = (u32)((int)sh / (int)v), s = p->sh & 31, g4, j;

		for (g4 = 0; g4 < 4; g4++) {
			for (j = 0; j < 4; j++) {
				u32 x = m[g4 * 8 + 4 + j], y = (u32)((int)(r * x) >> s), z = (u32)((int)x >> s);

				buf[g4 * 8 + j] = (u16)y;
				buf[g4 * 8 + 4 + j] = (u16)z;
				tot += y + z;
			}
		}
	} else {
		for (i = 0; i < 32; i++) {
			u32 q = bsum ? b[i] * rem / bsum : ~0U;

			buf[i] = (u16)(m[i] + q);
			tot += m[i] + q;
		}
	}
	rem = 0x8000U - tot;
	h = (int)(rem << 5) >> 10;
	for (i = 0; i < 32; i++)
		out[i] = (u16)(buf[i] + h);
	out[31] = (u16)(out[31] + (int)rem % 32);
}

static inline u32 t32_lce_w(const struct t32_lce_tab *tab, u32 idx, int *bad)
{
	if (idx > 31) {
		*bad = 1;
		return 0;
	}
	return tab->w[idx];
}

/*
 * tisp_lce_hist_method: clip the 32 bin histogram at thr[0], spread the clipped
 * mass with ramps; par = { lo, mid, hi }.  Returns -1 (hist untouched) where the
 * vendor would overflow its stack (a parameter above 32) or read before the
 * weight table (negative index).
 */
static inline int t32_lce_hist_method(u16 *hist, const u8 *thr, const u8 *par,
				      const struct t32_lce_tab *tab)
{
	u16 h[32];
	u32 w[32];
	u8 b[32];
	u32 lo = par[0], mid = par[1], hi = par[2];
	u32 exc = 0, half = 0, quart = 0, quart2 = 0, rem = 0, rest = 0, x, i;
	int narrow, ramp, bad = 0;

	if (lo > 32 || mid > 32 || hi > 32)
		return -1;
	for (i = 0; i < 32; i++) {
		h[i] = hist[i];
		w[i] = 0;
		b[i] = 0;
		if (thr[0] < h[i]) {
			exc = (exc - thr[0] + h[i]) & 0xffff;
			h[i] = thr[0];
		}
	}
	narrow = (int)(mid - lo) < 3;
	if (narrow)
		half = ((exc + 1) >> 1) & 0xffff;
	else
		quart = ((exc + 2) >> 2) & 0xffff;
	if ((int)(hi - mid) < 3) {
		ramp = 0;
		if (narrow)
			rem = (exc - half) & 0xffff;
		else
			rem = (exc - 2 * quart) & 0xffff;
	} else {
		ramp = 1;
		quart2 = ((exc + 2) >> 2) & 0xffff;
		if (narrow)
			rest = (exc - half - quart2) & 0xffff;
		else
			rest = (exc - 2 * quart - quart2) & 0xffff;
	}

	if (narrow) {
		if (hi == 0) {
			hi = 1;
			mid = 1;
		}
		x = t32_lce_w(tab, mid - 1, &bad) * half;
		if (bad)
			return -1;
		for (i = 0; i < mid; i++) {
			w[i] = x;
			b[i] = (u8)tab->up[i];
		}
	} else {
		if (hi == 0) {
			hi = 1;
			mid = 1;
			lo = 1;
		}
		x = t32_lce_w(tab, lo - 1, &bad) * quart;
		if (bad)
			return -1;
		for (i = 0; i < lo; i++) {
			w[i] = x;
			b[i] = (u8)tab->down[32 - lo + i];
		}
		x = t32_lce_w(tab, mid - lo - 1, &bad) * quart;
		if (bad)
			return -1;
		for (i = lo; i < mid; i++) {
			w[i] = x;
			b[i] = (u8)tab->up[i - lo];
		}
	}

	if (!ramp) {
		x = t32_lce_w(tab, 31 - mid, &bad) * rem;
		if (bad)
			return -1;
		for (i = mid; i < 32; i++) {
			w[i] = x;
			b[i] = (u8)tab->down[i];
		}
	} else {
		x = t32_lce_w(tab, hi - mid - 1, &bad) * quart2;
		if (bad)
			return -1;
		for (i = mid; i < hi; i++) {
			w[i] = x;
			b[i] = (u8)tab->down[32 - hi + i];
		}
		x = t32_lce_w(tab, 31 - hi, &bad) * rest;
		if (bad)
			return -1;
		for (i = hi; i < 32; i++) {
			w[i] = x;
			b[i] = (u8)tab->up[i - hi];
		}
	}

	for (i = 0; i < 32; i++) {
		u32 v = (u32)b[i] * w[i];

		hist[i] = (u16)(h[i] + (((v >> 14) & 1) + (v >> 15)));
	}
	return 0;
}

/*
 * lce_hist_filter_and_judge_16bit (0x53b40): running sum of the 32 u16 bins; the first bin where the sum
 * reaches 16385 gives its index, else 0.  *out = index (u8).
 */
static inline void t32_lce_hist_filter_and_judge_16bit(const u16 *hist, u8 *out)
{
	u32 sum = 0, i;

	for (i = 0; i < 32; i++) {
		sum += hist[i];
		if (sum >= 16385) {
			*out = (u8)i;
			return;
		}
	}
	*out = 0;
}

/* lce_hist_filter_and_judge (0x53b7c): the same on 32 bit bins */
static inline void t32_lce_hist_filter_and_judge(const u32 *hist, u8 *out)
{
	u32 sum = 0, i;

	for (i = 0; i < 32; i++) {
		sum += hist[i];
		if (sum >= 16385) {
			*out = (u8)i;
			return;
		}
	}
	*out = 0;
}

/*
 * lce_pdf_to_cdf (0x53c3c): 32 u32 pdf bins -> 20 byte cdf, no normalisation.  Running 32 bit sum, clamped to
 * 32767 when it reaches 32768, output = sum >> 7 (low byte).  Outputs 0..7 step one bin, 8..19 two bins.
 */
static inline void t32_lce_pdf_to_cdf_raw(const u32 *pdf, u8 *out)
{
	u32 sum = 0, i;

	for (i = 0; i < 8; i++) {
		if (sum >> 15)
			sum = 32767;
		out[i] = (u8)(sum >> 7);
		sum += pdf[i];
	}
	for (i = 0; i < 12; i++) {
		if (sum >> 15)
			sum = 32767;
		out[8 + i] = (u8)(sum >> 7);
		sum += pdf[8 + 2 * i] + pdf[9 + 2 * i];
	}
}

/*
 * lce_self_light_correct_local (0x53cc0): 20 bytes in -> out.  ofs = t32_data_d6b50 (the 20 byte column
 * offsets), d = in - ofs; d > 0 scales by up[j] (a3), d < 0 by down[j] (arg4), both only when the enable
 * bytes a2[8] / a2[12] are non-zero; out = low byte of (d + ofs).
 */
static inline void t32_lce_self_light_correct_local(const u8 *in, u8 *out, const u8 *en, const u8 *up,
						    const u8 *down)
{
	unsigned int j;

	for (j = 0; j < 20; j++) {
		int d = in[j] - t32_lce_slc_ofs[j];

		if (d > 0) {
			if (en[8])
				d = d * up[j] >> 7;
			else
				d = 0;
		} else if (d < 0) {
			if (en[12])
				d = d * down[j] >> 7;
			else
				d = 0;
		}
		out[j] = (u8)(d + t32_lce_slc_ofs[j]);
	}
}

/* signed divide as the vendor's mechanical C does it (min / -1 and divide by zero cases) */
static inline u32 t32_lce_sdiv(u32 n, u32 d)
{
	if (n == 0x80000000U && d == ~0U)
		return 0x80000000U;
	if (d)
		return (u32)((int)n / (int)d);
	return (int)n < 0 ? 1 : ~0U;
}

/*
 * lce_hist_method (0x53690): 32 u32 bins in, 32 u32 bins out, parameters in the same block as
 * t32_lce_hm16_par (n +4, c3 +20, b +24, k8 +28, c1 +32, a +36, e1 +40, e2 +44, sh +48).
 * Tent ramps a / b as in the 16 bit variant, m = signed min(a, ((h - 1024) * k8 >> 7) + 1024).
 * rem = 0x8000 - sum(m): rem >= 0 adds rem * b * q >> sh_b with q = (1 << sh_b) / sum(b) (sh_b = rodata d6c66);
 * rem < 0 scales every bin by (1 << sh_a) / v >> p->sh, v = (sum << 5 >> 14) + 1 (sh_a = rodata d6c67).
 * The rest of the 0x8000 total is spread over the bins, rem % 32 on bin 31.  Only n <= 31 (the vendor reads
 * its 1024 / n table unchecked).
 */
static inline void t32_lce_hist_method32(const u32 *hist, u32 *out, const struct t32_lce_hm16_par *p,
					 u32 sh_b, u32 sh_a)
{
	u32 n = p->n & 0xff, c1 = p->c1 & 0xffff, c3 = p->c3 & 0xff, k8 = p->k8 & 0xff;
	u32 ra = (p->a & 0xffff) * t32_lce_hm16_recip[n & 31];
	u32 rb = (p->b & 0xff) * t32_lce_hm16_recip[n & 31];
	u32 sa = (u32)((int)ra >> 10), sb = (u32)((int)rb >> 10);
	u32 a[32], b[32], m[32], buf[32], bsum = 0, sum = 0, tot = 0, rem, i;
	int h;

	for (i = 0; i < 32; i++) {
		u32 x = i < n ? i : 31 - i;

		a[i] = i == n ? p->e1 & 0xffff : c1 + sa * x;
		b[i] = i == n ? p->e2 & 0xffff : c3 + sb * x;
		bsum += b[i];
	}
	for (i = 0; i < 32; i++) {
		u32 g = (u32)((int)((hist[i] - 1024U) * k8) >> 7) + 1024U;

		m[i] = (int)a[i] < (int)g ? a[i] : g;
		sum += m[i];
	}
	rem = 0x8000U - sum;
	if ((int)rem < 0) {
		u32 r = t32_lce_sdiv(1U << (sh_a & 31), (u32)((int)(sum << 5) >> 14) + 1);
		u32 s = p->sh & 31;

		for (i = 0; i < 32; i++) {
			buf[i] = (r * m[i]) >> s;
			tot += buf[i];
		}
	} else {
		u32 q = t32_lce_sdiv(1U << (sh_b & 31), bsum);

		for (i = 0; i < 32; i++) {
			buf[i] = m[i] + ((rem * b[i] * q) >> (sh_b & 31));
			tot += buf[i];
		}
	}
	rem = 0x8000U - tot;
	h = (int)(rem << 5) >> 10;
	for (i = 0; i < 32; i++)
		out[i] = buf[i] + (u32)h;
	out[31] += (u32)((int)rem % 32);
}

/* tisp_lce_thres_search: bins where the running sum reaches thr[0..2] -> out[3] (8, 16, 24 when not found) */
static inline void t32_lce_thres_search(const u16 *hist, const u16 *thr, u8 *out)
{
	u32 cum = 0, i = 0;

	for (;;) {
		cum = (cum + hist[i]) & 0xffff;
		if (cum >= thr[0])
			break;
		if (++i == 32) {
			out[0] = 8;
			out[1] = 16;
			out[2] = 24;
			return;
		}
	}
	out[0] = (u8)i;
	while (cum < thr[1]) {
		if (i == 31) {
			out[1] = 16;
			out[2] = 24;
			return;
		}
		i++;
		cum = (cum + hist[i]) & 0xffff;
	}
	out[1] = (u8)i;
	while (cum < thr[2] && i < 31) {
		i++;
		cum = (cum + hist[i]) & 0xffff;
	}
	out[2] = (u8)i;
}

/*
 * tisp_lce_thres_search_simd_v4: thres_search for 16 histograms at once.  hist[16][32] (u16, hist k at byte
 * 64 * k), thr[0..2] (the vendor loads 8 halfwords from the thr pointer and uses the first three).
 * Per histogram the running sum saturates at 65535 (the scalar version wraps).  out0 / out1 / out2 get, per
 * histogram, the bin where the sum reaches thr[0] / thr[1] / thr[2], independent of each other, defaulting
 * to 8 / 16 / 24.  The vendor lanes latch a found threshold by replacing it with 0xffff, so a sum that
 * saturates at 65535 reaches it again and moves the result to every later bin.
 */
static inline void t32_lce_thres_search_simd(const u16 hist[16][32], const u16 *thr, u8 *out0, u8 *out1, u8 *out2)
{
	unsigned int k, i;

	for (k = 0; k < 16; k++) {
		u32 sum = 0, t0 = thr[0], t1 = thr[1], t2 = thr[2];

		out0[k] = 8;
		out1[k] = 16;
		out2[k] = 24;
		for (i = 0; i < 32; i++) {
			sum += hist[k][i];
			if (sum > 0xffff)
				sum = 0xffff;
			if (t0 <= sum) {
				out0[k] = (u8)i;
				t0 = 0xffff;
			}
			if (t1 <= sum) {
				out1[k] = (u8)i;
				t1 = 0xffff;
			}
			if (t2 <= sum) {
				out2[k] = (u8)i;
				t2 = 0xffff;
			}
		}
	}
}

/* tisp_lce_std_hist_transform: scale the 32 bins, then bring their sum to 0x8000 (or report the excess in *over) */
static inline void t32_lce_std_hist_transform(u16 *h, u32 mult, u32 shift, u16 *over)
{
	u32 sum = 0, i;

	for (i = 0; i < 32; i++) {
		u32 v = ((u32)h[i] * mult) >> (shift & 31);

		h[i] = (u16)v;
		sum += v;
	}
	if ((int)sum < 0x8000) {
		u32 d = 0x8000 - sum;
		u32 add = ((int)d >> 5) & 0xffff;
		u32 rem = d & 0x1f;

		for (i = 0; i < rem; i++)
			h[i] = (u16)(h[i] + 1);
		for (i = 0; i < 32; i++)
			h[i] = (u16)(h[i] + add);
	} else if (sum != 0x8000) {
		*over = (u16)(sum - 0x8000);
	}
}

/*
 * tisp_lce_std_hist_transform_simd: std_hist_transform with signed arithmetic.  v = (int)(h * mult) >> (shift & 31)
 * (product wrapped to 32 bit, arithmetic shift), h[] keeps the low 16 bits, sum is the 32 bit sum of the full v.
 * sum < 0x8000 (signed): add (0x8000 - sum) >> 5 (arithmetic, low 16 bits) to every bin and 1 more to the first
 * (0x8000 - sum) & 31 bins; sum > 0x8000 (signed): *over = sum - 0x8000; sum == 0x8000: nothing.
 * Unlike t32_lce_std_hist_transform the product is not treated as unsigned.
 */
static inline void t32_lce_std_hist_transform_simd(u16 *h, u32 mult, u32 shift, u16 *over)
{
	u32 sum = 0, i;

	for (i = 0; i < 32; i++) {
		u32 v = (u32)((int)((u32)h[i] * mult) >> (shift & 31));

		h[i] = (u16)v;
		sum += v;
	}
	if ((int)sum < 0x8000) {
		u32 d = 0x8000 - sum;
		u32 add = d >> 5;	/* only the low 16 bits are used, so the arithmetic shift of the vendor makes no difference */
		u32 rem = d & 0x1f;

		for (i = 0; i < rem; i++)
			h[i] = (u16)(h[i] + 1);
		for (i = 0; i < 32; i++)
			h[i] = (u16)(h[i] + add);
	} else if (sum != 0x8000) {
		*over = (u16)(sum - 0x8000);
	}
}

/* lce_std_hist_transform_simd: the same on 32 bit bins, no excess output (the vendor does nothing for sum > 0x8000) */
static inline void t32_lce_std_hist_transform_simd32(u32 *h, u32 mult, u32 shift)
{
	u32 sum = 0, i;

	for (i = 0; i < 32; i++) {
		h[i] = (u32)((int)(h[i] * mult) >> (shift & 31));
		sum += h[i];
	}
	if ((int)sum < 0x8000) {
		u32 d = 0x8000 - sum;
		u32 add = (u32)((int)d >> 5);
		u32 rem = d & 0x1f;

		for (i = 0; i < rem; i++)
			h[i]++;
		for (i = 0; i < 32; i++)
			h[i] += add;
	}
}

#endif /* TX_ISP_T32_LCE_H */
