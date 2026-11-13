/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) LCE (local contrast), module list 16, IQ bin section 15
 * (0x3dc bytes): the list part of tisp_lce_init.
 *
 *  - tisp_lce_size_para_calc: the statistics grid from the sensor size
 *    (2 * ((h / 2) / 12) rows, 2 * ((w / 2) / 24) columns, the remainders,
 *    cell counts and their reciprocals); the vendor keeps its defaults for
 *    1920x1080, which this calculation gives as well.  Written into a copy
 *    of the section (bytes 6..9, 0x0a.., 0x1c, 0x2e4.., 0x2ea.., 0x2f2..).
 *  - tisp_lce_malloc_cfg: the curve buffer (0x2d00 bytes, 0xc0a4) and the
 *    statistics buffer (0x13420 bytes in four parts, 0xc090..0xc09c).
 *  - tisp_lce_curve_init_default + tisp_lce_write_all_reg: the default
 *    curve (20 bytes 0, 8 .. 0x38, 0x40, 0x50 .. 0xf0) in both halves of
 *    the curve buffer.
 *  - tisp_lce_bin_para_update + func_lce_para_update: register fields from
 *    the section; the nine 0xc06c curve points are the 11-point u16 tables
 *    at 0x1fc interpolated over the EV (sec[0x3d9] = 0, knots 0x2fc) or the
 *    BV (knots 0x328); at init both are 0.
 *
 * Not here: the LSF list 37 (written by the same init: tx_isp_t33_lsf.h),
 * the mode with sec[0x2fb] = sec[0xc8] = 1 (0xc0a4 then takes the LSF
 * buffer: the list is not built), the per-frame soft process (openimp #444).
 * QEMU trace of the vendor module (openimp #388): list 16 word-equal but
 * for the DMA addresses.
 */
#ifndef TX_ISP_T33_LCE_H
#define TX_ISP_T33_LCE_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_LCE		16
#define T33_SEC_LCE		15
#define T33_LCE_SEC_BYTES	0x3dcU
#define T33_LCE_CURVE_BYTES	0x2d00U		/* two sets of 0x1680 bytes */
#define T33_LCE_STAT_BYTES	0x13420U	/* four parts 0x4d08 apart */

static const struct t33_tcmd t33_lce_tmpl[] = {
	{ 0x0c010, 1, 0 }, { 0x0c040, 13, 0 }, { 0x0c080, 2, 0 }, { 0x0c090, 6, 0 }, { 0x0c0e0, 1, 0 },
	{ 0x0c06c, 5, 1 }, { 0x0c0d8, 5, 1 },
};

static inline void t33_lce_put16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/* tisp_lce_size_para_calc into the section copy s */
static inline void t33_lce_size(u8 *s, u32 h, u32 w)
{
	u32 rh = (h >> 1) % 12, rw = (w >> 1) % 24, rows = ((h >> 1) / 12) * 2, cols = ((w >> 1) / 24) * 2;
	u32 n0, n1, n2, n3 = rows * cols, nr[3], nc[3], p[9], one, sh, i, j;

	if (!rh && !rw) {
		n0 = n1 = n2 = n3;
	} else if (!rh) {
		n0 = n2 = (cols + 2) * rows;
		n1 = n3;
	} else if (!rw) {
		n0 = n1 = (rows + 2) * cols;
		n2 = n3;
	} else {
		n0 = (rows + 2) * (cols + 2);
		n1 = cols * (rows + 2);
		n2 = rows * (cols + 2);
	}
	/* cell counts (0x2ea..) and their reciprocals (0x2f2..) */
	t33_lce_put16(s + 0x2ea, n0);
	t33_lce_put16(s + 0x2ec, n1);
	t33_lce_put16(s + 0x2ee, n2);
	t33_lce_put16(s + 0x2f0, n3);
	t33_lce_put16(s + 0x2f2, ((n0 >> 1) + 0x2000000) / n0);
	t33_lce_put16(s + 0x2f4, ((n1 >> 1) + 0x2000000) / n1);
	t33_lce_put16(s + 0x2f6, ((n2 >> 1) + 0x2000000) / n2);
	t33_lce_put16(s + 0x2f8, ((n3 >> 1) + 0x2000000) / n3);
	/* the nine block products and their reciprocals (0x0a..), shift (0x1c) */
	nr[0] = rh ? rows + 2 : rows;
	nr[1] = rows + (rh != 0);
	nr[2] = rows;
	nc[0] = rw ? cols + 2 : cols;
	nc[1] = cols + (rw != 0);
	nc[2] = cols;
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			p[3 * i + j] = nc[j] * nr[i];
	for (sh = 3, one = 0x8000000; sh < 10 && p[8] <= 0x4000U >> (sh - 3); sh++)
		one >>= 1;
	for (i = 0; i < 9; i++)
		t33_lce_put16(s + 0x0a + 2 * i, p[i] ? ((p[i] >> 1) + one) / p[i] : 0);
	s[0x1c] = (u8)sh;
	s[6] = (u8)rows;
	s[7] = (u8)cols;
	s[8] = (u8)rh;
	s[9] = (u8)rw;
	s[0x2e4] = (u8)(w * h);
	s[0x2e5] = (u8)((w * h) >> 8);
	s[0x2e6] = (u8)((w * h) >> 16);
	s[0x2e7] = (u8)((w * h) >> 24);
	s[0x2e8] = (u8)(0x4000000 / ((w * h) >> 2));
	s[0x2e9] = 0xc;
}

/* tisp_ev_intp_uint16_t / tisp_bv_intp_uint16_t: 11 u16 points over 11 LE u32 knots (bv: falling) */
static inline u16 t33_intp_u16(u32 x, const u8 *e, const u8 *t, int bv)
{
	u32 k, lo, hi, d, kv;
	unsigned long long f;

	for (k = 0; k < 11; k++) {
		kv = t33_le32(e + 4 * k);
		if (bv ? kv < x : x < kv)
			break;
	}
	if (k == 11)
		return t32_tp_le16(t + 20);
	if (!k)
		return t32_tp_le16(t);
	hi = t33_le32(e + 4 * k);
	lo = t33_le32(e + 4 * k - 4);
	d = bv ? lo - hi : hi - lo;
	if (!d)
		return 2;
	f = (unsigned long long)(bv ? lo - x : x - lo) * 0x1000 + (d >> 1);
	return (u16)t33_sub_interp((u32)t32_tp_div64(f, (unsigned long long)d), t32_tp_le16(t + 2 * k - 2),
				   t32_tp_le16(t + 2 * k));
}

/* the default curve (tisp_lce_curve_init_default) in both halves of the curve buffer */
static inline void t33_lce_curve_default(u8 *buf)
{
	static const u8 c[20] = { 0, 8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x50,
				  0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0 };
	u32 i;

	for (i = 0; i < T33_LCE_CURVE_BYTES; i++)
		buf[i] = c[i % 20];
}

/*
 * Block init into list l (capacity cap words): s a writable copy of section
 * 15 (the size fields are written into it), w / h the sensor size, ev / bv
 * the exposure values (0 at init), curve / stat the physical addresses of
 * the curve buffer (filled by the caller with t33_lce_curve_default) and of
 * the statistics buffer.  Returns the list length, -1 for the LSF mode.
 */
static inline int t33_lce_init(u8 *s, u32 w, u32 h, u32 ev, u32 bv, u32 curve, u32 stat, u32 *l, u32 cap)
{
	u32 v[9], k, x;
	int len, isbv;

	if (!w || !h || (s[0x2fb] == 1 && s[0xc8] == 1))
		return -1;
	len = t33_rcfg_tmpl_init(l, cap, t33_lce_tmpl, ARRAY_SIZE(t33_lce_tmpl));
	if (len <= 0)
		return len;
	t33_lce_size(s, h, w);
	/* tisp_lce_malloc_cfg */
	T33_SW(l, 0x14, stat);
	T33_SW(l, 0x15, stat + 0x4d08);
	T33_SW(l, 0x16, stat + 0x9a10);
	T33_SW(l, 0x17, stat + 0xe718);
	/* tisp_lce_write_all_reg (default curve), then the curve buffer kept */
	T33_SW(l, 0x19, curve);
	T33_SW(l, 0xf, 0);
	T33_SW(l, 1, 1);
	T33_SW(l, 0x1b, 0);
	/* curve points: EV or BV interpolation of the nine tables */
	isbv = s[0x3d9] != 0;
	x = isbv ? bv : ev;
	for (k = 0; k < 9; k++)
		v[k] = t33_intp_u16(x, s + (isbv ? 0x328 : 0x2fc), s + 0x1fc + 0x16 * k, isbv);
	T33_SW(l, 3, (u32)s[6] << 16 | (s[7] & 0x7fU));
	T33_SW(l, 4, (s[8] & 0xfU) << 8 | (s[0x1c] & 0xfU) << 20 | (s[9] & 0x1fU));
	T33_SW(l, 5, (t32_tp_le16(s + 0x0c) & 0x1fffU) << 16 | (t32_tp_le16(s + 0x0a) & 0x1fffU));
	T33_SW(l, 6, t32_tp_le16(s + 0x0e) & 0x1fffU);
	T33_SW(l, 7, (t32_tp_le16(s + 0x12) & 0x1fffU) << 16 | (t32_tp_le16(s + 0x10) & 0x1fffU));
	T33_SW(l, 8, t32_tp_le16(s + 0x14) & 0x1fffU);
	T33_SW(l, 9, (t32_tp_le16(s + 0x18) & 0x1fffU) << 16 | (t32_tp_le16(s + 0x16) & 0x1fffU));
	T33_SW(l, 10, t32_tp_le16(s + 0x1a) & 0x1fffU);
	T33_SW(l, 0xb, (t32_tp_le16(s + 0x2c4) & 0x3ffU) << 16 | (t32_tp_le16(s + 0x2c2) & 0x3ffU));
	T33_SW(l, 0xc, (t32_tp_le16(s + 0x2c8) & 0x3ffU) << 16 | (t32_tp_le16(s + 0x2c6) & 0x3ffU));
	T33_SW(l, 0xd, (s[0x1f9] & 3U) << 8 | (s[0x1fa] & 1U) << 16 | (s[0x1f8] & 1U));
	for (k = 0; k < 4; k++)
		T33_SW(l, 0x1d + k, (v[2 * k + 1] & 0x1ffU) << 16 | (v[2 * k] & 0x1ffU));
	T33_SW(l, 0x21, v[8] & 0x1ffU);
	T33_SW(l, 0x11, (s[0x2fa] & 0xfU) << 16 | 3);
	T33_SW(l, 0x12, 0);
	T33_SW(l, 0x18, 0x1e0 | 0x10000 | (s[0xca] & 1U) << 20);
	for (k = 0; k < 5; k++)
		T33_SW(l, 0x23 + k, (u32)s[0x2cb + 4 * k] | (u32)s[0x2cc + 4 * k] << 8 |
				     (u32)s[0x2cd + 4 * k] << 16 | (u32)s[0x2ce + 4 * k] << 24);
	return len;
}

#endif /* TX_ISP_T33_LCE_H */
