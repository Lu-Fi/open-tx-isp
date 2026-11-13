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
 * The kernel table is data transcribed from the vendor T32 module's
 * .rodata (the table tisp_sin interpolates; the same as the T33 module's
 * 24 interval table lut_25).  tools/t32_msca_sinc_tab.py computes its
 * shape, which misses the vendor's by 1 in 58 entries.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_msca_oracle).
 */
#ifndef TX_ISP_T32_MSCA_CURVE_H
#define TX_ISP_T32_MSCA_CURVE_H

#include "tx_isp_t32_tp.h"

#define T32_CURVE_TAPS		25	/* coefficients per direction */
#define T32_CURVE_WORDS		13	/* packed words per direction */
#define T32_CURVE_PHASES	8
#define T32_CURVE_RATIO_MAX	27306	/* Q14 */
#define T32_CURVE_SUM		2048

static const short t32_msca_sinc_tab[257] = {
	1535, 1533, 1528, 1520, 1507, 1492, 1473, 1452, 1427, 1399, 1369, 1336,
	1300, 1261, 1221, 1180, 1135, 1089, 1043, 995, 946, 897, 847, 796,
	745, 695, 645, 595, 546, 499, 452, 406, 361, 318, 277, 238,
	200, 164, 130, 99, 69, 41, 16, -8, -29, -47, -64, -80,
	-92, -103, -112, -119, -125, -129, -131, -132, -132, -131, -128, -125,
	-121, -116, -110, -104, -98, -91, -84, -77, -70, -62, -55, -49,
	-43, -37, -31, -25, -21, -16, -12, -9, -7, -5, -3, -1,
	0, 0, 0, 0, -1, -2, -4, -6, -8, -9, -12, -15,
	-17, -19, -22, -24, -26, -27, -30, -32, -33, -34, -35, -35,
	-36, -36, -36, -36, -35, -33, -33, -31, -29, -28, -26, -23,
	-21, -19, -16, -13, -11, -8, -5, -2, 0, 2, 4, 7,
	9, 11, 13, 14, 16, 18, 19, 19, 20, 21, 21, 21,
	21, 21, 21, 20, 19, 18, 18, 17, 15, 14, 13, 12,
	11, 10, 8, 7, 6, 5, 4, 3, 2, 2, 2, 1,
	0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3,
	4, 5, 5, 6, 7, 7, 7, 8, 9, 10, 10, 10,
	11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 10, 9,
	8, 7, 7, 6, 5, 4, 3, 2, 1, 0, 0, -1,
	-2, -3, -3, -4, -5, -6, -7, -7, -7, -8, -8, -8,
	-8, -8, -8, -8, -8, -8, -8, -7, -7, -6, -6, -6,
	-5, -4, -4, -4, -3, -3, -3, -2, -1, -1, -1, -1,
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

/*
 * Per-frame paths: tisp_msca_para_calc, tisp_msca_init_chx_cfg,
 * tisp_msca_ch_curve_write(_multi), tisp_msca_scaling_algorithm and
 * tisp_msca_params_update (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Checked by tests/t32_oracle (t32_mscac_oracle).  Direct register mode only
 * (t32_data_d03a0 == 0).
 *
 * par is the vendor's *(ctx + 0) block (flags at +172.., +181..; tuning words
 * from +4, 32 bytes per set; overrides from +100).  sets is t32_data_e2768[ch]
 * (872 bytes per set).  coef holds the 25-tap arrays (set, direction) of the
 * vendor ctx + 4.
 */
#define T32_MSCA_SET_BYTES	872U
#define T32_MSCA_COEFS		150

struct t32_msca_ch {
	u8 *par;
	u16 coef[T32_MSCA_COEFS];
	u32 shadow[6];		/* t32_data_e2740 */
	u32 scale_bits;		/* t32_data_e2758 */
	u8 pending;		/* t32_data_e2898[ch] */
};

static inline u32 t32_msca_rd8(const u8 *p, unsigned int o)
{
	return p[o];
}

static inline u32 t32_msca_rd16(const u8 *p, unsigned int o)
{
	u16 v;

	__builtin_memcpy(&v, p + o, 2);
	return v;
}

static inline u32 t32_msca_rd32(const u8 *p, unsigned int o)
{
	u32 v;

	__builtin_memcpy(&v, p + o, 4);
	return v;
}

static inline void t32_msca_wr16(u8 *p, unsigned int o, u32 val)
{
	u16 v = (u16)val;

	__builtin_memcpy(p + o, &v, 2);
}

static inline void t32_msca_wr32(u8 *p, unsigned int o, u32 val)
{
	__builtin_memcpy(p + o, &val, 4);
}

/* tuning words of a set inside par */
static inline unsigned int t32_msca_pb(unsigned int set)
{
	return 4 + 32 * set;
}

static inline u32 t32_msca_w6(const u8 *p, unsigned int b)
{
	return (t32_msca_rd32(p, b + 4) & 0x3ff) << 16 | (t32_msca_rd32(p, b) & 0x3ff);
}

static inline u32 t32_msca_w7(const u8 *p, unsigned int b)
{
	return t32_msca_rd8(p, b + 8) | t32_msca_rd32(p, b + 20) << 28 |
	       ((t32_msca_rd32(p, b + 12) << 8) & 0xffff) |
	       (t32_msca_rd32(p, b + 16) & 0x1ff) << 16;
}

/* tisp_msca_para_calc */
static inline void t32_msca_para_calc(struct t32_msca_ch *c, const u8 *sets, unsigned int set)
{
	const u8 *s;
	u8 *p = c->par;
	unsigned int b, t;
	u32 a, e, f, d, span, half, dif, q = 0, sh = 8;

	set &= 0xff;
	if (set > 2)
		return;
	s = sets + set * T32_MSCA_SET_BYTES;
	b = t32_msca_pb(set);
	a = t32_msca_rd32(p, b);
	e = t32_msca_rd32(p, b + 4);
	f = t32_msca_rd32(p, b + 8);
	d = t32_msca_rd32(p, b + 12);
	span = (e - a) & 0xffff;
	half = span >> 1;
	dif = d - f;
	for (t = 8; t; t--) {
		u32 v = 0;

		if (span) {
			v = dif * (1U << t);
			v = d < f ? v - half : v + half;
			v /= span;
		}
		if (v + 128 < 257) {
			q = v;
			sh = t;
			break;
		}
	}
	t32_msca_wr32(p, b + 16, q);
	t32_msca_wr32(p, b + 20, sh);
	t32_msca_wr32(p, b + 24, t32_msca_rd16(s, 34) ?
		      (t32_msca_rd16(s, 26) << 14) / t32_msca_rd16(s, 34) : ~0U);
	t32_msca_wr32(p, b + 28, t32_msca_rd16(s, 32) ?
		      (t32_msca_rd16(s, 28) << 14) / t32_msca_rd16(s, 32) : ~0U);
}

/* tisp_msca_init_chx_cfg */
static inline void t32_msca_init_chx_cfg_w(struct t32_msca_ch *c, u8 *sets, unsigned int chn,
					   struct t32_front_write *w, unsigned int *n)
{
	const u8 *p = c->par;
	u8 *s;
	unsigned int o, b;
	u32 limit;

	chn &= 0xff;
	o = chn * 0x100;
	s = sets + chn * T32_MSCA_SET_BYTES;
	T32_FWR(w, *n, 0xf0100 + o, t32_msca_rd16(s, 22) << 16 | t32_msca_rd16(s, 24));
	T32_FWR(w, *n, 0xf0104 + o, t32_msca_rd32(s, 32));
	if (chn < 3) {
		T32_FWR(w, *n, 0xf0178 + o, t32_msca_rd16(s, 26));
	}
	limit = t32_msca_rd16(s, 42) + t32_msca_rd16(s, 46);
	if (t32_msca_rd16(s, 34) < t32_msca_rd16(s, 44) + t32_msca_rd16(s, 40))
		t32_msca_wr16(s, 44, t32_msca_rd16(s, 34) - t32_msca_rd16(s, 40));
	if (t32_msca_rd16(s, 32) < limit)
		t32_msca_wr16(s, 46, t32_msca_rd16(s, 32) - t32_msca_rd16(s, 42));
	T32_FWR(w, *n, 0xf0108 + o, (t32_msca_rd16(s, 40) & 0x1fff) << 16 |
		(t32_msca_rd16(s, 42) & 0xfff));
	T32_FWR(w, *n, 0xf010c + o, (t32_msca_rd16(s, 44) & 0x1fff) << 16 |
		(t32_msca_rd16(s, 46) & 0xfff));
	if (chn < 3) {
		b = t32_msca_pb(chn);
		T32_FWR(w, *n, 0xf0170 + o, t32_msca_w6(p, b));
		T32_FWR(w, *n, 0xf0174 + o, t32_msca_w7(p, b));
		T32_FWR(w, *n, 0xf0114 + o, t32_msca_rd32(p, b + 24) & 0x7ffff);
		T32_FWR(w, *n, 0xf0110 + o, t32_msca_rd32(p, b + 28) & 0x7ffff);
		T32_FWR(w, *n, 0xf0128 + o, (t32_msca_rd8(p, 176 + 2 * chn) & 1) << 6 |
			(t32_msca_rd8(p, 175 + 2 * chn) & 3));
	}
	T32_FWR(w, *n, 0xf0128 + o, 0);
}

static inline int t32_msca_init_chx_cfg(struct t32_msca_ch *c, u8 *sets, unsigned int chn,
					struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_msca_init_chx_cfg_w(c, sets, chn, w, &n);
	return (int)n;
}

/* the 13 packed words of one set / direction (override words from par + 100) */
static inline void t32_msca_words(const struct t32_msca_ch *c, unsigned int set,
				  unsigned int dir, u32 wd[T32_CURVE_WORDS])
{
	const u8 *p = c->par;
	int t[T32_CURVE_TAPS];
	unsigned int i;

	for (i = 0; i < T32_CURVE_TAPS; i++)
		t[i] = c->coef[50 * set + 25 * dir + i];
	if (t32_msca_rd8(p, 181 + 2 * set + dir) == 1)
		for (i = 0; i < 6; i++)
			t[i] = (int)t32_msca_rd16(p, 100 + 24 * set + 12 * dir + 2 * i);
	t32_msca_curve_pack(t, wd);
}

static inline u32 t32_msca_ram_reg(unsigned int set, unsigned int dir, unsigned int i)
{
	return 0xf0820 + 160 * set + 80 * dir + 4 * i;
}

/* tisp_msca_ch_curve_write */
static inline void t32_msca_ch_curve_write_w(struct t32_msca_ch *c, u16 *cnt,
					     struct t32_front_write *w, unsigned int *n)
{
	unsigned int set, dir, i;
	u32 wd[T32_CURVE_WORDS];

	for (set = 0; set < 3; set++) {
		if (t32_msca_rd8(c->par, 172 + set) != 1)
			continue;
		for (dir = 0; dir < 2; dir++) {
			t32_msca_words(c, set, dir, wd);
			for (i = 0; i < T32_CURVE_WORDS; i++) {
				T32_FWR(w, *n, 0xf8004, wd[i]);
				T32_FWR(w, *n, 0xf8004, t32_msca_ram_reg(set, dir, i));
				if (set == 1 && i < 3)
					c->shadow[3 * dir + i] = wd[i];
			}
		}
		*cnt = (u16)(*cnt + 26);
	}
	c->pending = 0;
}

static inline int t32_msca_ch_curve_write(struct t32_msca_ch *c, u16 *cnt,
					  struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_msca_ch_curve_write_w(c, cnt, w, &n);
	return (int)n;
}

/* tisp_msca_ch_curve_write_multi */
static inline int t32_msca_ch_curve_write_multi(struct t32_msca_ch *c, struct t32_front_write *w)
{
	unsigned int set, dir, i, n = 0;
	u32 wd[T32_CURVE_WORDS];

	for (set = 0; set < 3; set++) {
		for (dir = 0; dir < 2; dir++) {
			t32_msca_words(c, set, dir, wd);
			for (i = 0; i < T32_CURVE_WORDS; i++) {
				T32_FWR(w, n, t32_msca_ram_reg(set, dir, i), wd[i]);
			}
		}
	}
	c->pending = 0;
	return (int)n;
}

/* tisp_msca_scaling_algorithm */
static inline void t32_msca_scaling_algorithm_w(struct t32_msca_ch *c,
						struct t32_front_write *w, unsigned int *n)
{
	const u8 *p = c->par;
	u32 bits;

	bits = (t32_msca_rd8(p, 183) & 1) << 1 | (t32_msca_rd8(p, 185) & 1) << 2 |
	       (t32_msca_rd8(p, 181) & 1) | (t32_msca_rd8(p, 182) & 1) << 4 |
	       (t32_msca_rd8(p, 184) & 1) << 5 | (t32_msca_rd8(p, 186) & 1) << 6;
	T32_FWR(w, *n, 0xf004c, bits);
	T32_FWR(w, *n, 0xf0010, 1);
	c->scale_bits = bits;
}

static inline int t32_msca_scaling_algorithm(struct t32_msca_ch *c, struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_msca_scaling_algorithm_w(c, w, &n);
	return (int)n;
}

/* tisp_msca_params_update */
static inline int t32_msca_params_update(struct t32_msca_ch *c, const u8 *sets,
					 struct t32_front_write *w)
{
	const u8 *p = c->par;
	unsigned int set, b, n = 0;

	for (set = 0; set < 3; set++) {
		if (t32_msca_rd8(p, 172 + set) != 1)
			continue;
		t32_msca_para_calc(c, sets, set);
		b = t32_msca_pb(set);
		T32_FWR(w, n, 0xf0170 + set * 0x100, t32_msca_w6(p, b));
		T32_FWR(w, n, 0xf0174 + set * 0x100, t32_msca_w7(p, b));
	}
	c->pending = 1;
	t32_msca_scaling_algorithm_w(c, w, &n);
	T32_FWR(w, n, 0xf0010, 1);
	return (int)n;
}

#endif /* TX_ISP_T32_MSCA_CURVE_H */
