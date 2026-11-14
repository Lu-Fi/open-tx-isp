/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) lens shading (LSC), module list 6, IQ bin section 7 (0x98
 * bytes) and its payload (sec[0x94] tables of 0x3602 bytes: u16 colour
 * temperature, three planes of 0x900 u16): tisp_lsc_init with
 * tisp_lsc_lut_valid_check (mesh from the sensor size and the cell size
 * sec[0x58] x sec[0x59]), tisp_lsc_interp_by_tgain (strength),
 * tisp_lsc_interp_by_ct (tisp_look_up_ct_list, blend of two tables,
 * tisp_lsc_lut_update_by_strength, tisp_lsc_lut_update_by_distance, the
 * 9-byte packing of two cells into the DMA table), func_lsc_para_update
 * (tisp_lsc_bin2user) and the tisp_lsc_write_reg(0) slot stores.
 *
 * The mesh table is not in the list: the block reads it by DMA from the
 * address in 0x5078 (slot 16; the vendor's kmalloc'ed table, rows padded
 * to 8 bytes, stride 0x5074).  0x507c (slot 17) is never stored by the
 * vendor: its list buffer is one word short (count 0x12 for 0x13 words),
 * the engine reads slab slack there (0x400 in the QEMU trace); ours is 0.
 * QEMU trace of the vendor module (openimp #388): list 6 word-equal but
 * for these two words.
 */
#ifndef TX_ISP_T33_LSC_H
#define TX_ISP_T33_LSC_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_LSC		6
#define T33_SEC_LSC		7
#define T33_LSC_SEC_BYTES	0x98U
#define T33_LSC_TABLE_BYTES	0x3602U
#define T33_LSC_PLANE		0x900U		/* u16 per plane */
#define T33_LSC_LUT_BYTES	0x6000U		/* the vendor's DMA table */
#define T33_LSC_WORDS		0x31U

static const struct t33_tcmd t33_lsc_tmpl[] = {
	{ 0x05010, 1, 0 }, { 0x05040, 10, 0 }, { 0x05070, 4, 0 },
};

/* mesh state (the vendor's 0x34-byte lsc info) and the six planes */
struct t33_lsc {
	u32 cells, stride;
	u8 rem_h, rem_w, rows, cols, str_ct, str, str_last, shift, valid;
	u16 ct[8];
	u16 pl[6][T33_LSC_PLANE];	/* A B C: CT blend, D E F: after strength / distance */
};

/* tisp_lsc_lut_valid_check (h, w: sensor height, width; mh, mw: cell size) */
static inline void t33_lsc_mesh(struct t33_lsc *m, int h, int w, int mh, int mw)
{
	int a = (h >> 1) + (mh >> 1), b = (w >> 1) + (mw >> 1), r;

	r = a % mh;
	m->rem_h = r > 0 ? (u8)(mh - r) : 0;
	r = b % mw;
	m->rem_w = r > 0 ? (u8)(mw - r) : 0;
	m->cols = (u8)(((m->rem_w != 0) + b / mw) * 2);
	m->rows = (u8)(((m->rem_h != 0) + a / mh) * 2);
	m->cells = (u32)m->rows * m->cols;
	m->valid = m->cols < 0x5d;
	m->stride = (((u32)m->cols * 0x24 + 0x3f) & ~0x3fU) >> 3;
	if (!m->cols)
		m->cols = 1;
	if (m->cells > 0x900) {
		m->cells = 0x900;
		m->rows = (u8)(0x900 / m->cols);
	}
}

/* tisp_look_up_ct_list */
static inline u32 t33_lsc_ct_part(u32 ct, u32 n, const u16 *c, const u8 *hy)
{
#define HY(i)	t32_tp_le16(hy + 2 * (i))
	u32 i;

	if (!(c[0] + HY(0) < ct))
		return 0;
	if ((int)(c[n - 1] - HY(n - 1)) <= (int)ct)
		return (n - 1) * 2;
	for (i = 0; i + 1 < n; i++) {
		if ((int)(c[i] - HY(i)) <= (int)ct && ct <= c[i] + HY(i))
			return i * 2;
		if (c[i] + HY(i) <= ct && (int)ct <= (int)(c[i + 1] - HY(i + 1)))
			return i * 2 + 1;
	}
	return 0;
#undef HY
}

/*
 * Integer square root of 4 x, halved and rounded (tisp_sqrt_round -> tisp_sqrt).  The vendor's tisp_sqrt starts
 * its bit at 1 << 30 (not 1 << 62), so it is exact only for 4 x < 2^32 and saturates near 0xffff above; kept
 * (binary oracle tests/t33_binoracle leaf, openimp #99; the LSC callers stay below 2^30).
 */
static inline u32 t33_sqrt_round(unsigned long long x)
{
	unsigned long long r = 0, bit = 1ULL << 30;

	x <<= 2;
	while (bit > x)
		bit >>= 2;
	while (bit) {
		if (x >= r + bit) {
			x -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return (u32)((r >> 1) + (r & 1));
}

/* tisp_lsc_lut_update_by_strength: A B C -> D E F */
static inline void t33_lsc_strength(struct t33_lsc *m)
{
	int s = (m->str == 0xff) + m->str;
	u32 i, a, b, c, lo, k, q;

	if (s == 0x100) {
		for (i = 0; i < m->cells; i++) {
			m->pl[3][i] = m->pl[0][i];
			m->pl[4][i] = m->pl[1][i];
			m->pl[5][i] = m->pl[2][i];
		}
		return;
	}
	for (i = 0; i < m->cells; i++) {
		a = m->pl[0][i];
		b = m->pl[1][i];
		c = m->pl[2][i];
		lo = a < b ? a : b;
		if (c <= lo)
			lo = c;
		if (lo < (0x800U >> (m->shift & 0x1f)))
			break;	/* the vendor stops here */
		k = (u32)s * lo + ((u32)(0x100 - s) << ((0xb - m->shift) & 0x1f));
		q = a * k / lo;
		m->pl[3][i] = (u16)((q >> 8) + ((q & 0xff) >> 7));
		q = b * k / lo;
		m->pl[4][i] = (u16)((q >> 8) + ((q & 0xff) >> 7));
		q = c * k / lo;
		m->pl[5][i] = (u16)((q >> 8) + ((q & 0xff) >> 7));
	}
}

/* tisp_lsc_lut_update_by_distance on D E F (in place) */
static inline void t33_lsc_distance(struct t33_lsc *m, const u8 *sec)
{
	int hr = m->rows / 2, hc = m->cols / 2, mh = sec[0x58], mw = sec[0x59];
	int base = 0x800 >> (m->shift & 0x1f), x, y, k, di, dj, cy, cx;
	const u8 *t[3] = { sec + 0x6d, sec + 0x75, sec + 0x7d };
	u32 diag, d, g[3], q, j;
	u16 *p;

#define DS(i)	t32_tp_le16(sec + 0x18 + 2 * (i))
	diag = t33_sqrt_round((unsigned long long)(long long)(hr * mh * hr * mh + hc * mw * hc * mw));
	for (k = 0; k < 7; k++)
		if (DS(k + 1) <= DS(k))
			return;
	if (!diag)
		return;
	for (y = 0, cy = mh >> 1; y < hr; y++, cy += mh) {
		k = 0;
		for (x = 0, cx = mw >> 1; x < hc; x++, cx += mw) {
			d = (t33_sqrt_round((unsigned long long)(u32)(cx * cx + cy * cy)) << 12) / diag;
			for (; k != 8; k++)
				if (d < DS(k))
					break;
			for (j = 0; j < 3; j++) {
				if (k == 8) {
					g[j] = t[j][7];
				} else if (!k) {
					g[j] = t[j][0];
				} else {
					int dd = (int)(d - DS(k - 1)), dk = (int)(DS(k) - DS(k - 1));

					g[j] = (u32)(((int)t[j][k - 1] * (dk - dd) + (int)t[j][k] * dd) / dk);
				}
				g[j] += (g[j] >> 7) & 1;
			}
			for (di = 0; di < 2; di++)
				for (dj = 0; dj < 2; dj++) {
					int r = di ? hr + y : hr - y - 1, c = dj ? hc + x : hc - x - 1;
					unsigned int idx = (unsigned int)(c + r * m->cols);

					for (j = 0; j < 3; j++) {
						p = &m->pl[3 + j][idx];
						q = ((u32)*p - (u32)base) * g[j];
						*p = (u16)(base + (q >> 8) + ((q & 0xff) >> 7));
					}
				}
		}
	}
#undef DS
}

/*
 * tisp_lsc_interp_by_ct (init: force) and the packing into the DMA table
 * lut (T33_LSC_LUT_BYTES; cells 2 per 9 bytes, rows padded to 8 bytes).
 */
static inline void t33_lsc_ct(struct t33_lsc *m, const u8 *sec, const u8 *tab, u32 ct, u8 *lut)
{
	u32 n = sec[0x94], part, t, i, f = 0, off = 0, r, c, lo, hi;
	const u8 *a, *b;
	unsigned int k;

	ct &= 0xffff;
	part = t33_lsc_ct_part(ct, n, m->ct, sec + 0x28);
	t = part / 2;
	a = tab + t * T33_LSC_TABLE_BYTES + 2;
	if (!(part & 1)) {
		for (k = 0; k < 3; k++)
			for (i = 0; i < m->cells; i++)
				m->pl[k][i] = (u16)t32_tp_le16(a + 0x1200 * k + 2 * i);
	} else {
		lo = m->ct[t] + t32_tp_le16(sec + 0x28 + 2 * t);
		hi = m->ct[t + 1] - t32_tp_le16(sec + 0x28 + 2 * t + 2);
		if (ct >= lo && (int)ct <= (int)hi && hi != lo)
			f = (u32)(((int)(ct - lo) * 0x1000 + (int)(hi - lo) / 2) / (int)(hi - lo)) & 0xffff;
		b = a + T33_LSC_TABLE_BYTES;
		for (k = 0; k < 3; k++)
			for (i = 0; i < m->cells; i++) {
				u32 x = t32_tp_le16(a + 0x1200 * k + 2 * i), y = t32_tp_le16(b + 0x1200 * k + 2 * i);

				m->pl[k][i] = (u16)((((y - x) * f) >> 12) + x);
			}
	}
	t33_lsc_strength(m);
	m->str_last = m->str;
	if (!sec[0x57])
		t33_lsc_distance(m, sec);
	for (r = 0; r < m->rows; r++) {
		for (c = 0; c < m->cols; c += 2) {
			unsigned int i0 = c + m->cols * r, i1 = i0 + 1;
			u16 *d = m->pl[3], *e = m->pl[4], *g = m->pl[5];

			if (off + 9 > T33_LSC_LUT_BYTES)
				return;
			lut[off++] = (u8)d[i0];
			lut[off++] = (u8)((d[i0] >> 8 & 0xf) | e[i0] << 4);
			lut[off++] = (u8)(e[i0] >> 4);
			lut[off++] = (u8)g[i0];
			lut[off++] = (u8)((g[i0] >> 8 & 0xf) | d[i1] << 4);
			lut[off++] = (u8)(d[i1] >> 4);
			lut[off++] = (u8)e[i1];
			lut[off++] = (u8)((e[i1] >> 8 & 0xf) | g[i1] << 4);
			lut[off++] = (u8)(g[i1] >> 4);
		}
		while ((off & 7) && off < T33_LSC_LUT_BYTES)
			lut[off++] = 0;
	}
}

/* tisp_lsc_bin2user + func_lsc_para_update into v (the vendor's 0x31 words) */
static inline void t33_lsc_para(u32 *v, const u8 *sec, const struct t33_lsc *m)
{
	v[9] = sec[0x91];
	v[10] = sec[0x54];
	v[0xf] = sec[0x5a];
	v[0x10] = t33_le32(sec + 8);
	v[0x11] = sec[0x59];
	v[0x12] = sec[0x58];
	v[0x13] = sec[0x5b];
	v[0x14] = sec[0x5c];
	v[0x15] = sec[0x5d];
	v[0x16] = t33_le32(sec + 0x10);
	v[0x17] = t33_le32(sec + 0x14);
	v[7] = v[8] = m->str_ct;
	v[0xb] = sec[0x92];
	v[0xc] = sec[0x93];
	v[0xd] = sec[0x55];
	v[0xe] = sec[0x56];

	v[0x22] = v[0xf];
	v[0x18] = v[7];
	v[0x23] = v[0x10];
	v[0x19] = v[8];
	v[0x26] = v[0x11];
	v[0x1a] = v[9];
	v[0x27] = v[0x12];
	v[0x1b] = v[10];
	v[0x2a] = v[0x13];
	v[0x2b] = v[0x14];
	v[0x1c] = v[0xb];
	v[0x1d] = v[0xc];
	v[0x2c] = v[0x15];
	v[0x1e] = v[0xd];
	v[0x1f] = v[0xe];
	v[0x2e] = v[0x16];
	v[0x2f] = v[0x17];
	v[0x25] = m->rem_h;
	v[0x24] = m->rem_w;
	v[0x29] = m->rows;
	v[0x28] = m->cols;
	v[0x30] = m->stride;
	v[0x20] = (int)v[0xb] + 1 < (int)v[0xc] ? 0x400 / (v[0xc] - v[0xb]) : 0x3ff;
	v[0x21] = (int)v[0xd] + 1 < (int)v[0xe] ? 0x400 / (v[0xe] - v[0xd]) : 0x3ff;
	v[0x2d] = v[0x17] * v[0x17] + v[0x16] * v[0x16];
}

/* tisp_lsc_write_reg(0) + the two init stores: list slots of t33_lsc_tmpl */
static inline void t33_lsc_store(const u32 *v, u32 *l, u32 lut_phys)
{
	u32 c0 = (v[0x1c] & 0xff) | v[0x1a] << 31 | (v[0x1d] & 0xff) << 8 | (v[0x20] & 0x3ff) << 16;
	u32 c1 = (v[0x1e] & 0xff) | v[0x1b] << 31 | (v[0x1f] & 0xff) << 8 | (v[0x21] & 0x3ff) << 16;

	T33_SW(l, 5, c0);
	T33_SW(l, 7, (v[0x19] & 0xff) << 16 | (v[0x18] & 0xff));
	T33_SW(l, 0xe, c1);
	T33_SW(l, 4, (v[0x2b] & 3) << 4 | (v[0x2c] & 3) << 8 | (v[0x2a] & 3));
	T33_SW(l, 3, (v[0x24] & 0x7f) << 8 | (v[0x27] & 0x7f) << 16 | (v[0x26] & 0x7f) | (v[0x25] & 0x7f) << 24);
	T33_SW(l, 8, (v[0x2f] & 0xffff) | v[0x2e] << 16);
	T33_SW(l, 9, v[0x2d] & 0x7fffff);
	T33_SW(l, 0xb, (v[0x22] & 1) << 16);
	T33_SW(l, 0xc, v[0x23] & 0x7fffff);
	T33_SW(l, 0xf, v[0x30] & 0x1ff);
	T33_SW(l, 0x10, lut_phys);
	T33_SW(l, 1, 1);
	T33_SW(l, 6, 1);	/* tisp_lsc_init: 0x504c = 1, 0x5010 = 1 */
	T33_SW(l, 1, 1);
}

/*
 * Block init into list l (capacity cap words): sec section 7, tab its
 * payload (tabbytes), h / w the sensor size, gain 16.16, ct in K; m the
 * mesh state, lut the DMA table (T33_LSC_LUT_BYTES) at lut_phys.  Returns
 * the list length, -1 when the payload or the mesh does not fit.
 */
static inline int t33_lsc_init(const u8 *sec, const u8 *tab, u32 tabbytes, u32 h, u32 w, u32 gain, u32 ct,
			       u32 *v, struct t33_lsc *m, u8 *lut, u32 lut_phys, u32 *l, u32 cap)
{
	u32 n = sec[0x94], mode = t33_le32(sec + 4), i;
	int len;

	if (!n || n > 8 || tabbytes < n * T33_LSC_TABLE_BYTES || !sec[0x58] || !sec[0x59])
		return -1;
	len = t33_rcfg_tmpl_init(l, cap, t33_lsc_tmpl, ARRAY_SIZE(t33_lsc_tmpl));
	for (i = 0; i < T33_LSC_WORDS; i++)
		v[i] = 0;
	t33_lsc_mesh(m, (int)h, (int)w, sec[0x58], sec[0x59]);
	if (!m->valid) {	/* the vendor falls back to 80 x 80 cells */
		t33_lsc_mesh(m, (int)h, (int)w, 0x50, 0x50);
		return -1;	/* and edits its section; not done here */
	}
	for (i = 0; i < n; i++)
		m->ct[i] = (u16)t32_tp_le16(tab + i * T33_LSC_TABLE_BYTES);
	/* tisp_lsc_interp_by_tgain */
	m->str_last = 0xff;
	if (mode == 0) {
		m->str_ct = t32_tp_intp_u8(gain >> 16, gain & 0xffff, sec + 0x85);
		m->str = 0xff;
	} else if (mode == 1) {
		m->str_ct = sec[0x90];
		m->str = 0xff;
	} else if (mode == 2) {
		m->str_ct = 0xff;
		m->str = t32_tp_intp_u8(gain >> 16, gain & 0xffff, sec + 0x48);
	} else {
		m->str_ct = 0xff;
		m->str = sec[0x90];
	}
	m->shift = sec[0x5b];
	for (i = 0; i < T33_LSC_LUT_BYTES; i++)
		lut[i] = 0;
	t33_lsc_ct(m, sec, tab, ct, lut);
	t33_lsc_para(v, sec, m);
	if (len > 0)
		t33_lsc_store(v, l, lut_phys);
	return len;
}

#endif /* TX_ISP_T33_LSC_H */
