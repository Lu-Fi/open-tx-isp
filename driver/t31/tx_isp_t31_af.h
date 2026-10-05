/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T31_AF_H
#define TX_ISP_T31_AF_H

/*
 * T31 auto-focus statistics chain as in the stock libt31-firmware-1.1.6
 * (tiziano_af_params_refresh, tiziano_af_set_hardware_param,
 * tisp_af_get_statistics, Tiziano_af_fpga, tisp_af_set_attr(_refresh),
 * tisp_af_get_attr, apical_isp_af_hist_s/g_attr, apical_isp_af_weight_*).
 * Plain data in and out so the host tests can run it; the driver adds the
 * locking, the register writes and the user copies.
 */
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#endif

#define T31_AF_ZONES 225U
#define T31_AF_COLS_MAX 15U
#define T31_AF_ROWS_MAX 15U
#define T31_AF_ATTR_BYTES 88U
/* the parameter block in an IQ bank (tparams day/night/active) */
#define T31_AF_BANK_OFFSET 0x11c3cU
#define T31_AF_BANK_WEIGHT_OFFSET 0x11e9cU

/*
 * The stock parameter arrays, in bank order (parameter ids 0x3ad..0x3bf).
 * zone: 0 hstart?, 1 rows, 2 vstart?, 3 cols, 4..18 column widths,
 * 19..33 row heights, 34/35 into 0xb828.
 */
struct t31_af_params {
	uint32_t zone[36];
	uint32_t thres[13];
	uint32_t fir0_v[5];
	uint32_t fir0_ldg[8];
	uint32_t fir0_cor[4];
	uint32_t fir1_v[5];
	uint32_t fir1_ldg[8];
	uint32_t fir1_cor[4];
	uint32_t iir0_h[10];
	uint32_t iir0_ldg[8];
	uint32_t iir0_cor[4];
	uint32_t iir1_h[10];
	uint32_t iir1_ldg[8];
	uint32_t iir1_cor[4];
	uint32_t point_pos[2];
	uint32_t tilt[5];
	uint32_t fv_wmean[15];
	uint32_t fv[3];
	uint32_t weight[225];
};

/* The statistics of one frame, unpacked (tisp_af_get_statistics). */
struct t31_af_stats {
	uint32_t fird0[T31_AF_ZONES];
	uint32_t fird1[T31_AF_ZONES];
	uint32_t iird0[T31_AF_ZONES];
	uint32_t iird1[T31_AF_ZONES];
	uint32_t y_sum[T31_AF_ZONES];
	uint32_t high_luma[T31_AF_ZONES];
	uint8_t frame_num;
};

/* What the focus-value run hands on. */
struct t31_af_result {
	uint32_t fv_value[T31_AF_ZONES];
	uint32_t fv_alt;
};

static inline uint32_t t31_af_rows(const struct t31_af_params *p)
{
	return p->zone[1] > T31_AF_ROWS_MAX ? T31_AF_ROWS_MAX : p->zone[1];
}

static inline uint32_t t31_af_cols(const struct t31_af_params *p)
{
	return p->zone[3] > T31_AF_COLS_MAX ? T31_AF_COLS_MAX : p->zone[3];
}

/* stock fix_point_mult2_32, bit for bit (32-bit products, q = 0 quirk) */
static inline uint32_t t31_af_mult(uint32_t q, uint32_t a, uint32_t b)
{
	uint32_t s = q & 31U;
	uint32_t mask = 0xffffffffU >> ((32U - s) & 31U);
	uint32_t ai = a >> s, bi = b >> s, af = a & mask, bf = b & mask;

	return ((ai * bi) << s) + af * bi + ai * bf + ((af * bf) >> s);
}

/*
 * tisp_af_get_statistics: 16 bytes per zone, rows x cols zones, the
 * output arrays have a row stride of 15.
 */
static inline void t31_af_unpack(const uint32_t *src, uint32_t rows,
				 uint32_t cols, struct t31_af_stats *st)
{
	const uint8_t *b = (const uint8_t *)src;
	uint32_t r, c;

	for (r = 0; r < rows; r++) {
		for (c = 0; c < cols; c++) {
			const uint32_t *w = src + (r * cols + c) * 4U;
			uint32_t i = r * 15U + c;

			st->fird0[i] = w[0] & 0x3fffffU;
			st->fird1[i] = ((w[1] & 0xfffU) << 10) | (w[0] >> 22);
			st->iird0[i] = ((w[2] & 3U) << 20) | (w[1] >> 12);
			st->iird1[i] = (w[2] & 0xfffffcU) >> 2;
			st->y_sum[i] = ((w[3] & 0x7fffU) << 8) | (w[2] >> 24);
			st->high_luma[i] = (w[3] & 0x3fff1000U) >> 15;
		}
	}
	st->frame_num = (uint8_t)((b[63] & 0xc0U) | ((src[11] >> 30) << 4) |
				  (src[3] >> 30) | ((src[7] >> 30) << 2));
}

/*
 * Tiziano_af_fpga: per zone the FIR/IIR blend (tilt 0/1), the focus value
 * (tilt 2/3), and the weighted means; fv[] and fv_wmean[] of the
 * parameters are updated like stock, the zone values go to res.  tilt[4]
 * is the fixed-point position.  A zero weight sum gives zero means (the
 * stock division by zero is undefined).
 */
static inline void t31_af_fv_run(struct t31_af_params *p,
				 const struct t31_af_stats *st,
				 struct t31_af_result *res)
{
	uint32_t rows = t31_af_rows(p), cols = t31_af_cols(p);
	uint32_t q = p->tilt[4] & 31U, one = 1U << q;
	uint32_t t5 = p->tilt[0], fp = p->tilt[1], s7 = p->tilt[2],
		 s6 = p->tilt[3];
	uint32_t s1 = 0, s0 = 0, wsum = 0, lo = 0, lo1 = 0, s5, alt, n, i, j;

	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			uint32_t k = i * 15U + j;
			uint32_t a = t31_af_mult(q, st->iird0[k] << q, t5) +
				     t31_af_mult(q, st->fird0[k] << q, one - t5);
			uint32_t b = t31_af_mult(q, st->iird1[k] << q, fp) +
				     t31_af_mult(q, st->fird1[k] << q, one - fp);
			uint32_t w = p->weight[k];

			res->fv_value[k] = (t31_af_mult(q, a << q, s7) +
					    t31_af_mult(q, b << q, s6)) >> q;
			s1 += (a >> q) * w;
			s0 += (b >> q) * w;
			wsum += w;
		}
	}
	if (wsum) {
		lo = s1 / wsum;
		lo1 = s0 / wsum;
	}
	s5 = (t31_af_mult(q, lo << q, s7) + t31_af_mult(q, lo1 << q, s6)) >> q;
	alt = (t31_af_mult(q, (s1 >> 3) << q, s7) +
	       t31_af_mult(q, (s0 >> 3) << q, s6)) >> q;
	for (i = 1; i < 15U; i++)
		p->fv_wmean[i - 1U] = p->fv_wmean[i];
	p->fv_wmean[14] = s5;
	n = rows * cols;
	p->fv[0] = lo * n;
	p->fv[1] = lo1 * n;
	p->fv[2] = s5 * n;
	res->fv_alt = alt;
}

/*
 * tiziano_af_set_hardware_param, first part: the column widths
 * ((width - 15) / cols) and row heights ((height - 3) / rows), even.
 */
static inline void t31_af_zone_layout(struct t31_af_params *p, uint32_t width,
				      uint32_t height)
{
	uint32_t cols = t31_af_cols(p), rows = t31_af_rows(p), v, i;

	if (cols) {
		v = (width - 15U) / cols;
		v &= ~1U;
		for (i = 0; i < cols; i++)
			p->zone[4U + i] = v;
	}
	if (rows) {
		v = (height - 3U) / rows;
		v &= ~1U;
		for (i = 0; i < rows; i++)
			p->zone[19U + i] = v;
	}
}

#define T31_AF_REGS_MAX 42U

/*
 * The register values of tiziano_af_set_hardware_param: the zone grid
 * (0xb804..0xb824) only when first is set (stock af_first), then the
 * control/threshold, filter, ldg and coring words (0xb828..0xb8a4).  The
 * caller precedes 0xb828 with 0xb800 = 1 (system_reg_write_af).
 * Returns the number of {reg, value} pairs.
 */
static inline unsigned int t31_af_hw_regs(const struct t31_af_params *p,
					  int first, uint32_t (*out)[2])
{
	const uint32_t *z = p->zone, *t = p->thres;
	unsigned int n = 0;

#define T31_AF_PUT(r, v) do { out[n][0] = (r); out[n][1] = (v); n++; } while (0)
#define T31_AF_B4(a, b, c, d) (((a) << 24) | ((b) << 16) | (c) | ((d) << 8))
	if (first) {
		T31_AF_PUT(0xb804, (z[3] << 28) | (z[2] << 16) | z[0] | (z[1] << 12));
		T31_AF_PUT(0xb808, T31_AF_B4(z[7], z[6], z[4], z[5]));
		T31_AF_PUT(0xb80c, T31_AF_B4(z[11], z[10], z[8], z[9]));
		T31_AF_PUT(0xb810, T31_AF_B4(z[15], z[14], z[12], z[13]));
		T31_AF_PUT(0xb814, (z[18] << 16) | (z[17] << 8) | z[16]);
		T31_AF_PUT(0xb818, T31_AF_B4(z[22], z[21], z[19], z[20]));
		T31_AF_PUT(0xb81c, T31_AF_B4(z[26], z[25], z[23], z[24]));
		T31_AF_PUT(0xb820, T31_AF_B4(z[30], z[29], z[27], z[28]));
		T31_AF_PUT(0xb824, (z[33] << 16) | (z[32] << 8) | z[31]);
	}
	T31_AF_PUT(0xb828, (z[35] << 16) | (t[4] << 8) | z[34] | (t[3] << 7) |
		   (t[2] << 6) | (t[1] << 5) | (t[0] << 4));
	T31_AF_PUT(0xb82c, (t[12] << 28) | (t[11] << 24) | t[5] | (t[10] << 20) |
		   (t[9] << 16) | (t[8] << 12) | (t[7] << 8) | (t[6] << 4));
	T31_AF_PUT(0xb830, (p->fir0_v[1] << 16) | p->fir0_v[0]);
	T31_AF_PUT(0xb834, (p->fir0_v[3] << 16) | p->fir0_v[2]);
	T31_AF_PUT(0xb838, p->fir0_v[4]);
	T31_AF_PUT(0xb83c, (p->fir1_v[1] << 16) | p->fir1_v[0]);
	T31_AF_PUT(0xb840, (p->fir1_v[3] << 16) | p->fir1_v[2]);
	T31_AF_PUT(0xb844, p->fir1_v[4]);
	T31_AF_PUT(0xb848, (p->iir0_h[2] << 16) | p->iir0_h[0]);
	T31_AF_PUT(0xb84c, (p->iir0_h[4] << 16) | p->iir0_h[3]);
	T31_AF_PUT(0xb850, (p->iir0_h[7] << 16) | p->iir0_h[5]);
	T31_AF_PUT(0xb854, (p->iir0_h[9] << 16) | p->iir0_h[8]);
	T31_AF_PUT(0xb858, (p->iir1_h[2] << 16) | p->iir1_h[0]);
	T31_AF_PUT(0xb85c, (p->iir1_h[4] << 16) | p->iir1_h[3]);
	T31_AF_PUT(0xb860, (p->iir1_h[7] << 16) | p->iir1_h[5]);
	T31_AF_PUT(0xb864, (p->iir1_h[9] << 16) | p->iir1_h[8]);
	T31_AF_PUT(0xb868, T31_AF_B4(p->fir0_ldg[3], p->fir0_ldg[2], p->fir0_ldg[0], p->fir0_ldg[1]));
	T31_AF_PUT(0xb86c, T31_AF_B4(p->fir0_ldg[7], p->fir0_ldg[6], p->fir0_ldg[4], p->fir0_ldg[5]));
	T31_AF_PUT(0xb870, T31_AF_B4(p->fir1_ldg[3], p->fir1_ldg[2], p->fir1_ldg[0], p->fir1_ldg[1]));
	T31_AF_PUT(0xb874, T31_AF_B4(p->fir1_ldg[7], p->fir1_ldg[6], p->fir1_ldg[4], p->fir1_ldg[5]));
	T31_AF_PUT(0xb878, T31_AF_B4(p->iir0_ldg[3], p->iir0_ldg[2], p->iir0_ldg[0], p->iir0_ldg[1]));
	T31_AF_PUT(0xb87c, T31_AF_B4(p->iir0_ldg[7], p->iir0_ldg[6], p->iir0_ldg[4], p->iir0_ldg[5]));
	T31_AF_PUT(0xb880, T31_AF_B4(p->iir1_ldg[3], p->iir1_ldg[2], p->iir1_ldg[0], p->iir1_ldg[1]));
	T31_AF_PUT(0xb884, T31_AF_B4(p->iir1_ldg[7], p->iir1_ldg[6], p->iir1_ldg[4], p->iir1_ldg[5]));
	T31_AF_PUT(0xb888, (p->fir0_cor[1] << 16) | p->fir0_cor[0]);
	T31_AF_PUT(0xb88c, (p->fir0_cor[3] << 16) | p->fir0_cor[2]);
	T31_AF_PUT(0xb890, (p->fir1_cor[1] << 16) | p->fir1_cor[0]);
	T31_AF_PUT(0xb894, (p->fir1_cor[3] << 16) | p->fir1_cor[2]);
	T31_AF_PUT(0xb898, (p->iir0_cor[1] << 16) | p->iir0_cor[0]);
	T31_AF_PUT(0xb89c, (p->iir0_cor[3] << 16) | p->iir0_cor[2]);
	T31_AF_PUT(0xb8a0, (p->iir1_cor[1] << 16) | p->iir1_cor[0]);
	T31_AF_PUT(0xb8a4, (p->iir1_cor[3] << 16) | p->iir1_cor[2]);
#undef T31_AF_B4
#undef T31_AF_PUT
	return n;
}

static inline uint16_t t31_af_rd16(const uint8_t *a, unsigned int o)
{
	return (uint16_t)(a[o] | (a[o + 1U] << 8));
}

static inline void t31_af_wr16(uint8_t *a, unsigned int o, uint32_t v)
{
	a[o] = (uint8_t)v;
	a[o + 1U] = (uint8_t)(v >> 8);
}

static inline void t31_af_wr32(uint8_t *a, unsigned int o, uint32_t v)
{
	t31_af_wr16(a, o, v);
	t31_af_wr16(a, o + 2U, v >> 16);
}

/* the 8-word ldg blocks: bytes 0, 1, u16 2, bytes 4, 5, 6, u16 8, byte 10 */
static const uint8_t t31_af_ldg_off[8] = { 0, 1, 2, 4, 5, 6, 8, 10 };
static const uint8_t t31_af_ldg_w16[8] = { 0, 0, 1, 0, 0, 0, 1, 0 };
static const uint8_t t31_af_ldg_base[4] = { 38, 50, 62, 74 };

static inline uint32_t *t31_af_ldg(struct t31_af_params *p, unsigned int i)
{
	return i == 0 ? p->fir0_ldg : i == 1 ? p->fir1_ldg :
	       i == 2 ? p->iir0_ldg : p->iir1_ldg;
}

/*
 * tisp_af_set_attr_refresh: the 88-byte attribute (af_attr) into the
 * parameter arrays; returns the AF enable byte.
 */
static inline uint8_t t31_af_attr_apply(const uint8_t *a,
					struct t31_af_params *p)
{
	unsigned int i, k;

	p->thres[5] = a[33];
	p->thres[6] = a[34];
	p->thres[7] = a[35];
	p->thres[8] = a[36];
	p->tilt[2] = t31_af_rd16(a, 18);
	p->tilt[3] = t31_af_rd16(a, 20);
	p->thres[4] = t31_af_rd16(a, 22);
	p->tilt[0] = t31_af_rd16(a, 24);
	p->tilt[1] = t31_af_rd16(a, 26);
	p->zone[2] = a[28];
	p->zone[0] = a[29];
	p->zone[3] = a[30];
	p->zone[1] = a[31];
	for (k = 0; k < 4U; k++) {
		uint32_t *l = t31_af_ldg(p, k);

		for (i = 0; i < 8U; i++) {
			unsigned int o = t31_af_ldg_base[k] + t31_af_ldg_off[i];

			l[i] = t31_af_ldg_w16[i] ? t31_af_rd16(a, o) : a[o];
		}
	}
	return a[16];
}

/*
 * tisp_af_get_attr: the attribute as read back (focus values shifted by
 * attribute byte 17, the parameters, the frame number).  Bytes the stock
 * code does not write are 0.
 */
static inline void t31_af_attr_read(const struct t31_af_params *p,
				    uint32_t fv_alt, uint8_t enable,
				    uint8_t shift, uint8_t frame_num,
				    uint8_t *g)
{
	uint32_t s = shift & 31U;
	unsigned int i, k;

	memset(g, 0, T31_AF_ATTR_BYTES);
	t31_af_wr32(g, 0, p->fv[2] >> s);
	t31_af_wr32(g, 4, fv_alt >> s);
	t31_af_wr32(g, 8, p->fv[0] >> s);
	t31_af_wr32(g, 12, p->fv[1] >> s);
	g[16] = enable;
	g[17] = shift;
	t31_af_wr16(g, 18, p->tilt[2]);
	t31_af_wr16(g, 20, p->tilt[3]);
	t31_af_wr16(g, 22, p->thres[4]);
	t31_af_wr16(g, 24, p->tilt[0]);
	t31_af_wr16(g, 26, p->tilt[1]);
	g[28] = (uint8_t)p->zone[2];
	g[29] = (uint8_t)p->zone[0];
	g[30] = (uint8_t)p->zone[3];
	g[31] = (uint8_t)p->zone[1];
	g[32] = frame_num;
	g[33] = (uint8_t)p->thres[5];
	g[34] = (uint8_t)p->thres[6];
	g[35] = (uint8_t)p->thres[7];
	g[36] = (uint8_t)p->thres[8];
	for (k = 0; k < 4U; k++) {
		const uint32_t *l = t31_af_ldg((struct t31_af_params *)p, k);

		for (i = 0; i < 8U; i++) {
			unsigned int o = t31_af_ldg_base[k] + t31_af_ldg_off[i];

			if (t31_af_ldg_w16[i])
				t31_af_wr16(g, o, l[i]);
			else
				g[o] = (uint8_t)l[i];
		}
	}
}

/*
 * apical_isp_af_hist_s_attr: the user's IMPISPAFHist (88 bytes, same
 * offsets as the attribute) with the stock checks: a zero byte 28 becomes
 * 1, byte 29 below 3 becomes 3, bytes 30 and 31 (columns, rows) must be
 * 5..15.  The bytes the stock conversion does not copy are 0 here (stack
 * leftovers in stock; nothing reads them).
 */
static inline int t31_af_hist_from_user(const uint8_t *u, uint8_t *a)
{
	static const uint8_t keep[] = {
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
		28, 29, 30, 31, 33, 34, 35, 36,
	};
	unsigned int i, k;

	if (u[30] < 5U || u[30] > 15U || u[31] < 5U || u[31] > 15U)
		return -EINVAL;
	memset(a, 0, T31_AF_ATTR_BYTES);
	for (i = 0; i < sizeof(keep); i++)
		a[keep[i]] = u[keep[i]];
	for (k = 0; k < 4U; k++)
		for (i = 0; i < 8U; i++) {
			unsigned int o = t31_af_ldg_base[k] + t31_af_ldg_off[i];

			a[o] = u[o];
			if (t31_af_ldg_w16[i])
				a[o + 1U] = u[o + 1U];
		}
	if (!a[28])
		a[28] = 1;
	if (a[29] < 3U)
		a[29] = 3;
	return 0;
}

/* apical_isp_af_weight_s_attr: 225 bytes, each 0..8, to the u32 table */
static inline int t31_af_weight_from_user(const uint8_t *u, uint32_t *w)
{
	unsigned int i;

	for (i = 0; i < T31_AF_ZONES; i++)
		if (u[i] > 8U)
			return -EINVAL;
	for (i = 0; i < T31_AF_ZONES; i++)
		w[i] = u[i];
	return 0;
}

/*
 * tiziano_af_init: the attribute a fresh start reports (tilt, threshold 4
 * and zone grid from the parameters, shift 0).
 */
static inline void t31_af_attr_init(const struct t31_af_params *p,
				    uint8_t *a)
{
	memset(a, 0, T31_AF_ATTR_BYTES);
	t31_af_wr16(a, 18, p->tilt[2]);
	t31_af_wr16(a, 20, p->tilt[3]);
	t31_af_wr16(a, 22, p->thres[4]);
	t31_af_wr16(a, 24, p->tilt[0]);
	t31_af_wr16(a, 26, p->tilt[1]);
	a[28] = (uint8_t)p->zone[2];
	a[29] = (uint8_t)p->zone[0];
	a[30] = (uint8_t)p->zone[3];
	a[31] = (uint8_t)p->zone[1];
}

/*
 * The user-visible IMPISPAFHist of the T31 1.1.6 libimp is 24 bytes
 * (af_stat{metrics, metrics_alt}, enable, shift, delta, theta, hilight_th,
 * alpha_alt, hstart, vstart, nodeh, nodev, frame_num); the stock libimp
 * hands that struct to the driver as it is.  The stock T31 1.1.6 kernel
 * copies its 88-byte internal attribute (the T23 1.3.0 layout with
 * af_wl/af_wh, belta_alt, ldg_en and the four ldg blocks) to and from it,
 * overrunning the caller's struct by 64 bytes.  Here the driver boundary
 * is the 24-byte struct; the fields it does not carry keep their current
 * values on a set.
 */
#define T31_AF_HIST_PUB_BYTES 24U

static const uint8_t t31_af_pub_map[][2] = {
	/* public offset, attribute offset (bytes) */
	{ 8, 16 }, { 9, 17 },                       /* enable, shift */
	{ 10, 18 }, { 11, 19 }, { 12, 20 }, { 13, 21 }, /* delta, theta */
	{ 14, 22 }, { 15, 23 }, { 16, 24 }, { 17, 25 }, /* hilight, alpha */
	{ 18, 28 }, { 19, 29 }, { 20, 30 }, { 21, 31 }, /* hstart..nodev */
	{ 22, 32 },                                 /* frame_num */
};

/* GetAfHist: the 88-byte attribute read back -> the 24-byte struct */
static inline void t31_af_hist_to_pub(const uint8_t *g, uint8_t *u)
{
	unsigned int i;

	memset(u, 0, T31_AF_HIST_PUB_BYTES);
	memcpy(u, g, 8);                            /* af_metrics, _alt */
	for (i = 0; i < sizeof(t31_af_pub_map) / sizeof(t31_af_pub_map[0]); i++)
		u[t31_af_pub_map[i][0]] = g[t31_af_pub_map[i][1]];
}

/*
 * SetAfHist: the 24-byte struct over the current attribute -> the 88-byte
 * form apical_isp_af_hist_s_attr takes (then t31_af_hist_from_user).
 */
static inline void t31_af_hist_from_pub(const uint8_t *u, const uint8_t *cur,
					uint8_t *full)
{
	unsigned int i;

	memcpy(full, cur, T31_AF_ATTR_BYTES);
	for (i = 0; i < sizeof(t31_af_pub_map) / sizeof(t31_af_pub_map[0]); i++)
		full[t31_af_pub_map[i][1]] = u[t31_af_pub_map[i][0]];
}

#endif
