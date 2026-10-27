/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA polyphase filter curves (S4).
 *
 * Per channel and direction the vendor tisp (tisp_msca_curve_calc,
 * tisp_msca_ch_curve_write) computes 25 coefficients from the scale ratio
 * and loads them through the MSCA shadow port.  This is our implementation
 * of that flow, checked against the vendor's writes in the QEMU traces
 * (r10/r13, S3 run v1: 1:2, 1:3, 1:6 of a 3840x2160 input):
 *
 *  - ratio = out << 14 / in, at most 27306 (the vendor's limit);
 *  - sample: 25 points, step ratio * 8 / 24, linear interpolation in the
 *    kernel table below (segments of 1024); x = 0 is the centre tap;
 *  - normalise: 8 phases of 6 taps each, tap j of phase p at
 *    |8 * (j - 2) - p|; every phase is scaled to a sum of 2048 (rounded),
 *    the rounding error is added to the centre taps first (2, 3, 1, 4, 0,
 *    5) or taken from the outer ones first (5, 0, 4, 1, 3, 2); later phases
 *    overwrite shared taps, as the vendor's loop does;
 *  - pack: 13 words, word i = c[2i + 1] << 16 | c[2i], 13-bit two's
 *    complement each (c[25] = 0).
 *
 * The kernel table is computed (tools/t32_msca_sinc_tab.py), not taken from
 * the vendor; with it 44 of the 50 traced coefficients are identical and
 * 6 differ by 1 (of 2048).
 */
#ifndef TX_ISP_T32_MSCA_CURVE_H
#define TX_ISP_T32_MSCA_CURVE_H

#define T32_CURVE_TAPS		25	/* coefficients per direction */
#define T32_CURVE_WORDS		13	/* packed words per direction */
#define T32_CURVE_PHASES	8
#define T32_CURVE_RATIO_MAX	27306	/* Q14 */
#define T32_CURVE_SUM		2048

static const short t32_msca_sinc_tab[257] = {
	1535, 1533, 1528, 1519, 1507, 1492, 1474, 1452, 1427, 1399, 1369, 1335,
	1300, 1262, 1221, 1179, 1135, 1090, 1043, 995, 946, 896, 846, 796,
	745, 695, 645, 595, 546, 498, 451, 406, 361, 318, 277, 238,
	200, 164, 130, 98, 69, 41, 16, -8, -29, -48, -65, -79,
	-92, -103, -112, -120, -125, -129, -132, -133, -132, -131, -129, -125,
	-121, -116, -110, -104, -98, -91, -84, -77, -70, -63, -56, -49,
	-43, -37, -31, -26, -21, -17, -13, -9, -7, -4, -3, -1,
	0, 0, 0, -1, -1, -3, -4, -6, -8, -10, -12, -14,
	-17, -19, -21, -24, -26, -28, -30, -31, -33, -34, -35, -35,
	-36, -36, -36, -35, -35, -34, -33, -31, -29, -28, -25, -23,
	-21, -18, -16, -13, -11, -8, -5, -3, 0, 3, 5, 7,
	9, 11, 13, 15, 16, 18, 19, 20, 20, 21, 21, 21,
	21, 21, 20, 20, 19, 18, 17, 16, 15, 14, 13, 12,
	11, 9, 8, 7, 6, 5, 4, 3, 2, 2, 1, 1,
	0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 3,
	4, 4, 5, 6, 7, 7, 8, 9, 9, 10, 10, 11,
	11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 9, 9,
	8, 7, 7, 6, 5, 4, 3, 2, 1, 0, -1, -1,
	-2, -3, -4, -5, -5, -6, -6, -7, -7, -8, -8, -8,
	-8, -8, -8, -8, -8, -8, -7, -7, -7, -6, -6, -6,
	-5, -5, -4, -4, -3, -3, -2, -2, -1, -1, -1, -1,
	0, 0, 0, 0, 0,
};

/* out << 14 / in, limited as the vendor's; in == 0 gives the limit */
static inline unsigned int t32_msca_curve_ratio(unsigned int in, unsigned int out)
{
	unsigned int r = in ? (out << 14) / in : T32_CURVE_RATIO_MAX;

	return r > T32_CURVE_RATIO_MAX ? T32_CURVE_RATIO_MAX : r;
}

static inline void t32_msca_curve_sample(unsigned int ratio, int c[T32_CURVE_TAPS])
{
	unsigned int step = ratio * 8 / (T32_CURVE_TAPS - 1), i;

	for (i = 0; i < T32_CURVE_TAPS; i++) {
		unsigned int x = i * step, seg = x >> 10, f = x & 1023;
		int a = seg < 257 ? t32_msca_sinc_tab[seg] : 0;
		int b = seg + 1 < 257 ? t32_msca_sinc_tab[seg + 1] : 0;
		int v = a * (int)(1024 - f) + b * (int)f;

		c[i] = (v >> 10) + ((v >> 9) & 1);
	}
}

static inline void t32_msca_curve_normalise(int c[T32_CURVE_TAPS])
{
	static const unsigned char add_order[6] = { 2, 3, 1, 4, 0, 5 };
	static const unsigned char sub_order[6] = { 5, 0, 4, 1, 3, 2 };
	int in[T32_CURVE_TAPS], tap[6], p, j;

	for (j = 0; j < T32_CURVE_TAPS; j++)
		in[j] = c[j];
	for (p = 0; p < T32_CURVE_PHASES; p++) {
		int k[6], sum = 0, err = 0;

		for (j = 0; j < 6; j++) {
			int pos = T32_CURVE_PHASES * (j - 2) - p;

			k[j] = pos < 0 ? -pos : pos;
			sum += in[k[j]];
		}
		if (!sum)
			continue;
		for (j = 0; j < 6; j++) {
			int v = in[k[j]] * T32_CURVE_SUM;

			v += in[k[j]] >= 0 ? sum / 2 : -(sum / 2);
			tap[j] = v / sum;
			err += tap[j];
		}
		err -= T32_CURVE_SUM;
		for (j = 0; err < 0 && j < 6; j++, err++)
			tap[add_order[j]]++;
		for (j = 0; err > 0 && j < 6; j++, err--)
			tap[sub_order[j]]--;
		for (j = 0; j < 6; j++)
			c[k[j]] = tap[j];
	}
}

/* the coefficients for scaling in to out pixels */
static inline void t32_msca_curve(unsigned int in, unsigned int out, int c[T32_CURVE_TAPS])
{
	t32_msca_curve_sample(t32_msca_curve_ratio(in, out), c);
	t32_msca_curve_normalise(c);
}

/* an unused channel: a flat 6-tap box (2048 / 6), as the vendor loads */
static inline void t32_msca_curve_box(int c[T32_CURVE_TAPS])
{
	int j;

	for (j = 0; j < T32_CURVE_TAPS; j++)
		c[j] = T32_CURVE_SUM / 6;
}

static inline void t32_msca_curve_pack(const int c[T32_CURVE_TAPS], unsigned int w[T32_CURVE_WORDS])
{
	int i;

	for (i = 0; i < T32_CURVE_WORDS; i++) {
		int lo = c[2 * i], hi = 2 * i + 1 < T32_CURVE_TAPS ? c[2 * i + 1] : 0;

		w[i] = ((unsigned int)hi & 0x1fff) << 16 | ((unsigned int)lo & 0x1fff);
	}
}

#endif /* TX_ISP_T32_MSCA_CURVE_H */
