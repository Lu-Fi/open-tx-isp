/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AWB long algorithm: tisp_t33_awb_long_alogrithm and tisp_t33_awb_sat_weight, on raw
 * byte blocks (the AWB parameter section "par", the run state "st", the history block "hist" and the
 * statistics buffer).  The helpers the vendor code calls (register read, fixed point multiply / divide,
 * the MXU bracket and the colour temperature detection) come in through t33_awbL_ops.
 *
 * Statistics buffer: 28 byte zones, fields from +8 (the first 8 bytes are the head with the tag), a row
 * of an odd column count padded by 4 bytes; the buffer is 29696 bytes, index from register 0x1608c.
 */
#ifndef TX_ISP_T33_AWBL_H
#define TX_ISP_T33_AWBL_H

#include "tx_isp_t33_blk.h"
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

struct t33_awbL_ops {
	u32 (*reg_read)(u32 reg);					/* system_reg_read */
	u32 (*mult)(u32 bits, u32 a, u32 b);				/* fix_point_mult2_32 */
	unsigned long long (*div)(u32 bits, unsigned long long num, unsigned long long den);	/* fix_point_div */
	void (*mxu_begin)(u32 mask);					/* kernel_mxu_begin_partial */
	void (*ct_detect)(u8 *d, u32 *rb, u32 *flag);			/* tisp_awb_ct_detect_simd_3 */
	void (*mxu_end)(void);						/* kernel_mxu_end */
};

#define T33_AWBL_RA	9517U	/* word index of the three 15 entry rings in the state */
#define T33_AWBL_RB	9532U
#define T33_AWBL_RC	9547U

static inline u32 t33_aL_r32(const u8 *p, u32 off)
{
	u32 v;

	memcpy(&v, p + off, 4);
	return v;
}

static inline u32 t33_aL_r16(const u8 *p, u32 off)
{
	u16 v;

	memcpy(&v, p + off, 2);
	return v;
}

static inline void t33_aL_w32(u8 *p, u32 off, u32 v)
{
	memcpy(p + off, &v, 4);
}

static inline void t33_aL_w16(u8 *p, u32 off, u32 v)
{
	u16 x = (u16)v;

	memcpy(p + off, &x, 2);
}

static inline u32 t33_aL_sr(const u8 *st, u32 i)
{
	return t33_aL_r32(st, 4 * i);
}

static inline void t33_aL_sw(u8 *st, u32 i, u32 v)
{
	t33_aL_w32(st, 4 * i, v);
}

/* start of the zones of a row: rows of an odd column count carry 4 pad bytes each */
static inline const u8 *t33_aL_zrow(const u8 *data, u32 cols, u32 row)
{
	return data + ((cols & 1) ? 4 * row : 0);
}

static inline u32 t33_aL_absd(u32 a, u32 b)
{
	return a < b ? b - a : a - b;
}

/* tisp_t33_awb_sat_weight: out[row * 32 + col] = saturation weight of the zone; wt the two channel weights */
static inline void t33_awbL_sat_weight(const struct t33_awbL_ops *o, const u8 *par, const u8 *buf, u8 *out,
				       const u32 *wt)
{
	const u32 rows = t33_aL_r16(par, 3186), cols = t33_aL_r16(par, 3190);
	const u32 lim = t33_aL_r32(par, 4), sh = t33_aL_r16(par, 3306) & 31;
	u32 idx, row, col, rowz = 0, off = 0, t3;

	idx = o->reg_read(0x1608c) & 3;
	for (row = 0; row < rows; row++) {
		const u8 *zb = buf + idx * 29696U + ((cols & 1) ? 4 * row : 0);

		t3 = rowz;
		for (col = 0; col < cols; col++) {
			const u8 *r = zb + ((t3 + off) & 0xffff) * 28;
			u32 wg = t33_aL_r32(r, 24), v = 1;

			if (wg && lim < t33_aL_r16(r, 34)) {
				u32 a = (t33_aL_r32(r, 20) * wt[0]) >> sh;
				u32 b = (t33_aL_r32(r, 28) * wt[1]) >> sh;
				u32 mn = a < b ? a : b;
				u32 den;

				if (wg < mn)
					mn = wg;
				if (mn) {
					mn <<= sh;
					if (t33_aL_r16(par, 3438) == 1) {
						u32 mx = a > b ? a : b;

						den = wg > mx ? wg : mx;
					} else {
						den = (a + b + wg) / 3;
					}
					v = mn / den;
				}
			}
			t33_aL_w32(out, 4 * t3, v);
			t3 = (t3 + 1) & 0xffff;
		}
		rowz = (rowz + 32) & 0xffff;
		off = (off + ((cols - 32) & 0xffff)) & 0xffff;
	}
}

/* tisp_t33_awb_long_alogrithm; d the ct detection context, drops the counter word (awbInfo + 20) */
static inline void t33_awbL_long(const struct t33_awbL_ops *o, u8 *d, u8 *par, u8 *st, u8 *hist, u8 *drops,
				 const u8 *buf, u32 num)
{
	const u32 rows = t33_aL_r16(par, 3186), cols = t33_aL_r16(par, 3190);
	const u32 sh = t33_aL_r16(par, 3306), sh2 = t33_aL_r16(par, 3308);
	const u32 lim = t33_aL_r32(par, 4), p8 = t33_aL_r32(par, 8), p12 = t33_aL_r32(par, 12);
	const u32 v76 = t33_aL_r32(par, 16), v80 = t33_aL_r32(par, 20);
	const u32 v64 = t33_aL_r32(par, 24), v68 = t33_aL_r32(par, 28);
	const u32 th116 = t33_aL_r16(par, 3312), th120 = t33_aL_r16(par, 3314), th124 = t33_aL_r16(par, 3316);
	const u32 one = 1U << (sh & 31), shl = (sh - 10) & 31;
	const u32 area = (t33_aL_r16(par, 3290) * t33_aL_r16(par, 3292)) & 0xffff;
	const u8 *data = buf + num * 29696U, *zb, *z;
	const u32 tag = t33_aL_r16(data, 0);
	u32 wt[2], rb[2], flag[2], m84, m88, row, col, rowbase, s8, cnt1, cnt2, sumA, sumB, s2, k, have;
	u32 zr, zo, ws, sa, sb, sc, g1, g2, half, sum, toff, avg, vi;
	unsigned long long d1, d2;
	int s7;

	wt[0] = wt[1] = rb[0] = rb[1] = one;
	flag[0] = flag[1] = 0;
	m84 = o->mult(sh, one, t33_aL_r32(par, 52) << shl);
	m88 = o->mult(sh, one, t33_aL_r32(par, 56) << shl);

	/* per zone ratios into the state tables (and the history block) */
	rowbase = 0;
	for (row = 0; row < rows; row++) {
		zb = t33_aL_zrow(data, cols, row);
		s8 = ((row + 64) * 32) & 0xffff;
		for (col = 0; col < cols; col++) {
			u32 p = (s8 - 2048) & 0xffff, q1 = (s8 - 1024) & 0xffff, q3 = (s8 + 1024) & 0xffff;
			u32 g, x, y;

			z = zb + ((rowbase + col) & 0xffff) * 28;
			g = t33_aL_r32(z, 24);
			if (!g) {
				t33_aL_sw(st, p, 0);
				t33_aL_sw(st, q1, 0);
				t33_aL_sw(st, p + 4096, 0);
				t33_aL_w16(hist, 150 + 2 * p, 0);
				t33_aL_w16(hist, 2198 + 2 * p, 0);
			} else {
				x = (t33_aL_r32(z, 20) << 10) / g;
				y = (t33_aL_r32(z, 28) << 10) / g;
				t33_aL_sw(st, p, o->mult(sh, x << shl, m84));
				t33_aL_sw(st, q1, o->mult(sh, y << shl, m88));
				t33_aL_sw(st, p + 4096, t33_aL_r16(z, 34));
				t33_aL_w16(hist, 150 + 2 * p, x);
				t33_aL_w16(hist, 2198 + 2 * p, y);
			}
			t33_aL_sw(st, 7450U + p, t33_aL_r16(hist, 150 + 2 * p));
			t33_aL_sw(st, 7450U + q1, t33_aL_r16(hist, 2198 + 2 * p));
			g = t33_aL_r32(z, 12);
			if (!g) {
				t33_aL_sw(st, s8, 0);
				t33_aL_sw(st, q3, 0);
				t33_aL_sw(st, q1 + 4096, 0);
			} else {
				x = (t33_aL_r32(z, 8) << 10) / g;
				y = (t33_aL_r32(z, 16) << 10) / g;
				t33_aL_sw(st, s8, o->mult(sh, x << shl, m84));
				t33_aL_sw(st, q3, o->mult(sh, y << shl, m88));
				t33_aL_sw(st, q1 + 4096, t33_aL_r16(z, 32));
			}
			s8 = (s8 + 1) & 0xffff;
		}
		rowbase = (rowbase + cols) & 0xffff;
	}

	/* sums and non zero counts of the two ratio tables */
	cnt1 = cnt2 = cols * rows;
	sumA = sumB = 0;
	zr = 2048;
	for (row = 0; row < rows; row++) {
		vi = zr;
		for (col = 0; col < cols; col++) {
			u32 ea = t33_aL_sr(st, vi), eb = t33_aL_sr(st, (vi + 1024) & 0xffff);

			sumA += ea;
			sumB += eb;
			vi = (vi + 1) & 0xffff;
			cnt1 -= (ea == 0);
			cnt2 -= (eb == 0);
		}
		zr = (zr + 32) & 0xffff;
	}
	d1 = o->div(sh, sumA, m84);
	s7 = (int)(1U << (sh2 & 31)) / 2;
	d2 = o->div(sh, sumB, m88);

	/* mean ratios: reference pair moves to the previous pair */
	t33_aL_w16(st, 0x10448, t33_aL_r16(st, 0x10444));
	t33_aL_w16(st, 0x1044a, t33_aL_r16(st, 0x10446));
	if (!cnt1) {
		t33_aL_w16(st, 0x10444, 256);
		t33_aL_w16(hist, 4278, 256);
	} else {
		u32 v = (u32)((d1 + (unsigned long long)(long long)s7) >> (sh2 & 63));

		avg = (cnt1 / 2 + v) / cnt1;
		t33_aL_w16(st, 0x10444, avg);
		t33_aL_w16(hist, 4278, avg);
	}
	if (!cnt2) {
		t33_aL_w16(st, 0x10446, 256);
		t33_aL_w16(hist, 4280, 256);
	} else {
		u32 v = (u32)((d2 + (unsigned long long)(long long)s7) >> (sh2 & 63));

		avg = (cnt2 / 2 + v) / cnt2;
		t33_aL_w16(st, 0x10446, avg);
		t33_aL_w16(hist, 4280, avg);
	}
	t33_aL_w16(hist, 4286, 0x10000 / (int)t33_aL_r16(hist, 4278));
	t33_aL_w16(hist, 4288, 0x10000 / (int)t33_aL_r16(hist, 4280));

	/* per zone count ratios */
	zr = 0;
	zo = 0;
	for (row = 0; row < rows; row++) {
		u32 zc = zr;

		zb = t33_aL_zrow(data, cols, row);
		for (col = 0; col < cols; col++) {
			u32 n;

			z = zb + ((zc + zo) & 0xffff) * 28;
			n = t33_aL_r16(z, 32);
			if (!n) {
				t33_aL_sw(st, 16662U + zc, 0);
				t33_aL_sw(st, 16662U + 1024 + zc, 0);
				t33_aL_sw(st, 16662U + 2048 + zc, 0);
			} else {
				t33_aL_sw(st, 16662U + zc, t33_aL_r32(z, 8) / n);
				t33_aL_sw(st, 16662U + 1024 + zc, t33_aL_r32(z, 12) / n);
				t33_aL_sw(st, 16662U + 2048 + zc, t33_aL_r32(z, 16) / n);
			}
			zc = (zc + 1) & 0xffff;
		}
		zr = (zr + 32) & 0xffff;
		zo = (zo + ((cols - 32) & 0xffff)) & 0xffff;
	}

	/* clear the zones whose counts per area are within the limit */
	zr = 0;
	zo = 0;
	for (row = 0; row < rows; row++) {
		u32 v0 = zr;

		zb = t33_aL_zrow(data, cols, row);
		for (col = 0; col < cols; col++) {
			u32 n, wn, na, wa, v1;

			z = zb + ((zo + v0) & 0xffff) * 28;
			n = t33_aL_r16(z, 32);
			wn = t33_aL_r16(z, 34);
			na = (u32)((int)(n * 1000) / (int)area) & 0xffff;
			wa = (u32)((int)(wn * 1000) / (int)area) & 0xffff;
			v1 = (v0 + 1024) & 0xffff;
			if (!(lim < wa)) {
				t33_aL_sw(st, v0, 0);
				t33_aL_sw(st, v1, 0);
				t33_aL_sw(st, v0 + 4096, 0);
				t33_aL_w16(hist, 150 + 2 * v0, 0);
				t33_aL_w16(hist, 2198 + 2 * v0, 0);
				t33_aL_sw(st, 7450U + v0, 0);
				t33_aL_sw(st, 7450U + v1, 0);
			}
			if (!(lim < na)) {
				t33_aL_sw(st, (v0 + 2048) & 0xffff, 0);
				t33_aL_sw(st, (v0 + 3072) & 0xffff, 0);
				t33_aL_sw(st, v1 + 4096, 0);
			}
			v0 = (v0 + 1) & 0xffff;
		}
		zr = (zr + 32) & 0xffff;
		zo = (zo + ((cols - 32) & 0xffff)) & 0xffff;
	}

	if (((t33_aL_r16(par, 3438) - 1) & 0xffff) < 2)
		t33_awbL_sat_weight(o, par, buf, st + 24576, wt);

	/* the closing tag of the buffer must be the head tag */
	if (cols & 1)
		toff = ((7 * (cols >> 1) + 4) * rows + 1) * 8;
	else
		toff = (cols >> 1) * 56 * rows + 8;
	if (t33_aL_r16(data, toff) != tag) {
		t33_aL_w32(drops, 0, t33_aL_r32(drops, 0) + 1);
		return;
	}

	/* still picture test against the reference means */
	have = st[0x94a8];
	if (have == 1) {
		u32 dr = t33_aL_absd(t33_aL_r16(st, 0x10444), t33_aL_r16(st, 0x1044c)) & 0xffff;
		u32 db = t33_aL_absd(t33_aL_r16(st, 0x10446), t33_aL_r16(st, 0x1044e)) & 0xffff;

		if (st[0x94b0] == 1 && ((dr + db) & 0xffff) < th124 && !(t33_aL_r32(st, 0x10438) & 0x00ff00ffU))
			return;
	}
	t33_aL_w16(st, 0x1044c, t33_aL_r16(st, 0x10444));
	t33_aL_w16(st, 0x1044e, t33_aL_r16(st, 0x10446));
	memset((void *)(unsigned long)t33_aL_r32(d, 144), 0, 784);
	memset((void *)(unsigned long)t33_aL_r32(d, 148), 0, 784);
	memset((void *)(unsigned long)t33_aL_r32(d, 152), 0, 784);
	memset((void *)(unsigned long)t33_aL_r32(d, 156), 0, 144);
	memset((void *)(unsigned long)t33_aL_r32(d, 160), 0, 720);
	memset((void *)(unsigned long)t33_aL_r32(d, 164), 0, 960);
	memset((void *)(unsigned long)t33_aL_r32(d, 172), 0, 4096);
	memset((void *)(unsigned long)t33_aL_r32(d, 176), 0, 4096);
	memset((void *)(unsigned long)t33_aL_r32(d, 180), 0, 4096);
	memset((void *)(unsigned long)t33_aL_r32(d, 184), 0, 4096);
	memset((void *)(unsigned long)t33_aL_r32(d, 188), 0, 4096);
	o->mxu_begin(0);
	o->ct_detect(d, rb, flag);
	o->mxu_end();

	/* the three 15 entry rings of the detected colour point; weighted means over the newest entries */
	s2 = t33_aL_r16(par, 3310);
	if (have == 1) {
		for (k = 0; k < 14; k++) {
			t33_aL_sw(st, T33_AWBL_RA + k, t33_aL_sr(st, T33_AWBL_RA + k + 1));
			t33_aL_sw(st, T33_AWBL_RB + k, t33_aL_sr(st, T33_AWBL_RB + k + 1));
			t33_aL_sw(st, T33_AWBL_RC + k, t33_aL_sr(st, T33_AWBL_RC + k + 1));
		}
		t33_aL_sw(st, T33_AWBL_RA + 14, rb[0]);
		t33_aL_sw(st, T33_AWBL_RB + 14, rb[1]);
		t33_aL_sw(st, T33_AWBL_RC + 14, t33_aL_r32(par, 44));
		if (s2 >= 16)
			s2 = 15;
		else if (!s2)
			s2 = 1;
		ws = sa = sb = sc = 0;
		for (k = 16 - s2; k <= 15; k++) {
			ws += k;
			sa += k * t33_aL_sr(st, T33_AWBL_RA + k - 1);
			sb += k * t33_aL_sr(st, T33_AWBL_RB + k - 1);
			sc += k * t33_aL_sr(st, T33_AWBL_RC + k - 1);
		}
		rb[0] = (ws / 2 + sa) / ws;
		rb[1] = (ws / 2 + sb) / ws;
		t33_aL_w32(par, 44, (ws / 2 + sc) / ws);
	} else {
		for (k = 0; k < 15; k++) {
			t33_aL_sw(st, T33_AWBL_RA + k, rb[0]);
			t33_aL_sw(st, T33_AWBL_RB + k, rb[1]);
			t33_aL_sw(st, T33_AWBL_RC + k, t33_aL_r32(par, 44));
		}
	}
	if (have != 1) {
		st[0x94a8] = 1;
		st[0x94b0] = 0;
		s2 = 1;
	} else if (t33_aL_sr(st, 0x94e8 / 4) == t33_aL_sr(st, 0x94ec / 4) &&
		   t33_aL_sr(st, 0x9524 / 4) == t33_aL_sr(st, 0x9528 / 4) &&
		   !(t33_aL_r32(st, 0x10438) & 0x00ff00ffU) &&
		   t33_aL_r32(par, 16) == t33_aL_r32(par, 24) && t33_aL_r32(par, 20) == t33_aL_r32(par, 28)) {
		st[0x94b0] = 1;
		return;
	}

	/* the gains of the colour point against the stored reference */
	g1 = (u32)o->div(sh, one, rb[0]);
	g2 = (u32)o->div(sh, one, rb[1]);
	half = 1U << ((sh2 - 1) & 31);
	g1 = (g1 + half) >> (sh2 & 31);
	g2 = (g2 + half) >> (sh2 & 31);
	sum = t33_aL_absd(g1, v76) + t33_aL_absd(g2, v80);

	if (!p8) {
		if (!(th120 < sum) || flag[0] == 1) {
			t33_aL_w32(par, 8, 0);
			t33_aL_w32(par, 12, 0);
			return;
		}
		t33_aL_w32(par, 20, g2);
		t33_aL_w32(par, 8, 1);
		t33_aL_w32(par, 12, 1);
		t33_aL_w32(par, 16, g1);
		t33_aL_w32(par, 24, (u32)((int)(g1 - v76) / (int)s2) + v64);
		t33_aL_w32(par, 28, (u32)((int)(g2 - v80) / (int)s2) + v68);
		return;
	}
	if (th116 < sum && flag[0] != 1) {
		t33_aL_w32(par, 16, g1);
		t33_aL_w32(par, 20, g2);
		t33_aL_w32(par, 8, 1);
		t33_aL_w32(par, 12, 1);
		t33_aL_w32(par, 24, (u32)((int)(g1 - v64) / (int)s2) + v64);
		t33_aL_w32(par, 28, (u32)((int)(g2 - v68) / (int)s2) + v68);
		return;
	}
	if (p12 + 1 == s2 || s2 == 1) {
		t33_aL_w32(par, 8, 0);
		t33_aL_w32(par, 24, t33_aL_r32(par, 16));
		t33_aL_w32(par, 28, t33_aL_r32(par, 20));
		t33_aL_w32(par, 12, t33_aL_r32(par, 12) + 1);
		return;
	}
	t33_aL_w32(par, 8, 1);
	s2 -= p12;
	t33_aL_w32(par, 24, (u32)((int)(g1 - v64) / (int)s2) + v64);
	t33_aL_w32(par, 28, (u32)((int)(g2 - v68) / (int)s2) + v68);
	t33_aL_w32(par, 12, t33_aL_r32(par, 12) + 1);
}

#endif /* TX_ISP_T33_AWBL_H */
