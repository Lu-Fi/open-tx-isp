/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 lens shading (LSC) from the IQ bin: the content of the stream-on
 * register list (S4).
 *
 * The vendor's stream-on list is the LSC block: 0x5010 = 1, the 13 control
 * registers 0x5040..0x5070, the mesh table through the port 0x32020/0x32024
 * (3456 words) and 0x2f4 = 0 (QEMU run r20).  Its flow (tisp_lsc_init,
 * tisp_lsc_calc_para_refresh, tisp_lsc_interp_by_ct/_by_tgain,
 * tisp_lsc_write_all_reg of the PRJ007 driver) reverse engineered:
 *
 *  - parameters: IQ bin + 0x98 (tparams) + 0x68c4, starting with the block
 *    id 0x0134da11 (also written to 0x5000);
 *  - geometry from the sensor size and the cell size p[66] (rows) / p[67]
 *    (columns), the optical centre offsets p32[16] / p32[20];
 *  - three mesh tables of 1152 cells, each a u16 colour temperature tag and
 *    three planes of 2 x 1152 u16 (stride 0x3602 from p + 0x2910); the
 *    colour temperature picks the table: breakpoints p16[0x18..0x1e], even
 *    zones use one table, odd zones blend two (stage 2; until then the
 *    nearer table is used);
 *  - a cell packs the 6 values (2 per plane, 12 bits) into 3 words;
 *  - gain dependent strength p[101 + i] interpolated over the log2 gain
 *    (Q16) as tisp_simple_intp_uint8 does.
 *
 * Checked against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle
 * (t32_lsc_oracle): the enable, the 13 control registers, the port select and the cell
 * count (t32_lsc_cell_count) are bit-exact for all 67 bins at three sizes and random
 * sections.  The mesh words are bit-exact too: table choice and blend (tags, hysteresis, Q12 weight,
 * 16-bit elements), the radial correction (byte 65 clear) and the ring format (byte 65 set).
 */
#ifndef TX_ISP_T32_LSC_H
#define TX_ISP_T32_LSC_H

#include "tx_isp_t32_rcfg.h"

#ifndef __KERNEL__
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_IQ_TPARAMS		0x98U		/* tparams in the IQ bin */
#define T32_LSC_PARAM		0x68c4U		/* LSC block in tparams */
#define T32_LSC_ID		0x0134da11U
#define T32_LSC_TABLES		0x2910U		/* first mesh table in the block */
#define T32_LSC_TABLE_STRIDE	0x3602U
#define T32_LSC_PLANE		0x1200U
#define T32_LSC_NTABLES		3
#define T32_LSC_CELLS		1152
#define T32_LSC_WORDS		(T32_LSC_CELLS * 3)	/* 3456 */
#define T32_LSC_BLOCK_BYTES	(T32_LSC_TABLES + T32_LSC_NTABLES * T32_LSC_TABLE_STRIDE)

/* registers */
#define T32_LSC_ID_REG		0x5000
#define T32_LSC_EN		0x5010
#define T32_LSC_CTRL0		0x5040	/* 13 consecutive registers to 0x5070 */
#define T32_LSC_PORT_SEL	0x32020
#define T32_LSC_PORT_DATA	0x32024
#define T32_LSC_LIST_TAIL	0x2f4	/* the vendor's list ends with 0x2f4 = 0 */

struct t32_lsc {
	u32 r2_center;		/* ox^2 + oy^2 */
	u32 cells;		/* rows * columns * 2 */
	u8 rem_y, rem_x;	/* padding of the first cell */
	u8 rows, cols;
	u8 zone;		/* far corner distance class 0..4 */
	u8 flags[6];		/* p[69..74] */
	u8 none;		/* no table applies: the vendor's buffer stays zero */
	u16 inv_main, inv_wdr;	/* 1024 / (p[115] - p[114]), 1024 / (p[64] - p[63]) */
	u8 strength, strength_wdr;
	unsigned int table_lo, table_hi, fraction;	/* CT choice (fraction Q12) */
	u32 regs[13];		/* 0x5040..0x5070 */
};

static inline u32 t32_le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static inline u32 t32_le16(const u8 *p) { return p[0] | p[1] << 8; }

/* the LSC block in an IQ bin of bytes length, or NULL */
static inline const u8 *t32_lsc_block(const u8 *bin, u32 bytes)
{
	const u8 *p;

	if (!bin || bytes < T32_IQ_TPARAMS + T32_LSC_PARAM + T32_LSC_BLOCK_BYTES)
		return NULL;
	p = bin + T32_IQ_TPARAMS + T32_LSC_PARAM;
	return t32_le32(p) == T32_LSC_ID ? p : NULL;
}

/* tisp_simple_intp_uint8: t[i] towards t[i + 1] by frac / 65536, rounded */
static inline u8 t32_intp_u8(u32 i, u32 frac, const u8 *t)
{
	u32 a, b, d, v;

	if (i >= 10)
		return t[10];
	a = t[i];
	b = t[i + 1];
	if (a == b)
		return a;
	d = (a < b ? b - a : a - b) * frac;
	v = (d >> 16) + ((d >> 15) & 1);
	return (u8)(a < b ? a + v : a - v);
}

/* tisp_lsc_calc_para_refresh; -1 when the grid exceeds 1152 cells */
static inline int t32_lsc_geometry(const u8 *p, struct t32_lsc *s, u32 width, u32 height)
{
	int oy = (int)t32_le32(p + 16), ox = (int)t32_le32(p + 20);
	u32 cy = p[66], cx = p[67], a, ry, rx;
	int fy = (int)height + oy, fx = (int)width + ox;
	long long far;

	if (!cy || !cx)
		return -1;
	if (fy < -oy)
		fy = -oy;
	if (fx < -ox)
		fx = -ox;
	far = (long long)fx * fx + (long long)fy * fy;
	a = (height >> 1) + (cy >> 1);
	ry = a % cy;
	s->rem_y = ry ? (u8)(cy - ry) : 0;
	s->rows = (u8)(a / cy + (s->rem_y != 0));
	a = (width >> 1) + (cx >> 1);
	rx = a % cx;
	s->rem_x = rx ? (u8)(cx - rx) : 0;
	s->cols = (u8)(a / cx + (s->rem_x != 0));
	s->r2_center = (u32)(ox * ox + oy * oy);
	s->cells = (u32)s->rows * s->cols * 2;
	if (s->cells > T32_LSC_CELLS)
		return -1;
	s->zone = far > 0x300000 ? 4 : far > 0x180000 ? 3 : far > 0xc0000 ? 2 : far > 0x60000 ? 1 : 0;
	s->inv_main = p[114] + 1 < p[115] ? 1024 / (p[115] - p[114]) : 1023;
	s->inv_wdr = p[63] + 1 < p[64] ? 1024 / (p[64] - p[63]) : 1023;
	return 0;
}

/* tisp_lsc_interp_by_tgain(_s): strength for the total gain (log2, Q16) */
static inline void t32_lsc_gain(const u8 *p, struct t32_lsc *s, u32 gain)
{
	if (t32_le32(p + 4)) {
		s->strength = p[112];
		s->strength_wdr = p[61];
	} else {
		s->strength = t32_intp_u8(gain >> 16, gain & 0xffff, p + 101);
		s->strength_wdr = t32_intp_u8(gain >> 16, gain & 0xffff, p + 50);
	}
}

/*
 * table(s) for the colour temperature (tisp_lsc_interp_by_ct, forced update, section byte
 * p[116] = number of tables, at most three): each mesh table starts with the colour
 * temperature it was measured at (u16 tag).  The hysteresis h is the u16 at p[48], lowered
 * to a quarter of the smallest tag distance when that is smaller.  Then
 *  - ct <= tag[0] + h: the first table; ct >= tag[last] - h: the last table;
 *  - otherwise the tag pairs are walked: ct within h of the upper tag of a pair: that
 *    table; between tag_lo + h and tag_hi - h: the pair is blended with the weight
 *    (ct - (tag_lo + h)) / ((tag_hi - h) - (tag_lo + h)) in Q12 (a later pair replaces an
 *    earlier one); if no pair applies the vendor leaves its buffer untouched (zero).
 */
static inline void t32_lsc_ct(const u8 *p, struct t32_lsc *s, u32 ct)
{
	u32 tag[T32_LSC_NTABLES + 1], k, n = p[116], h = t32_le16(p + 48), md = 0x10000;

	if (n < 1)
		n = 1;
	if (n > T32_LSC_NTABLES)
		n = T32_LSC_NTABLES;
	for (k = 0; k < n; k++)
		tag[k] = t32_le16(p + T32_LSC_TABLES + k * T32_LSC_TABLE_STRIDE);
	/* the vendor's distance loop runs one step past the last table; the tag there is read as 0 */
	tag[n] = n < T32_LSC_NTABLES ? t32_le16(p + T32_LSC_TABLES + n * T32_LSC_TABLE_STRIDE) : 0;
	for (k = 1; k <= n; k++) {
		u32 d = tag[k] - tag[k - 1];

		if (d < md)
			md = d;
		if (md < (h << 2))
			h = md >> 2;
	}
	s->none = 0;
	s->fraction = 0;
	s->table_lo = s->table_hi = 0;
	if (!(tag[0] + h < ct)) {
		/* first table */
	} else if (!(ct < tag[n - 1] - h)) {
		s->table_lo = s->table_hi = n - 1;
	} else {
		s->none = 1;
		for (k = 1; k < n; k++) {
			u32 hi = tag[k] - h, lo = tag[k - 1] + h;

			if (ct < hi) {
				if (!(lo < ct))
					continue;
				s->none = 0;
				s->table_lo = k - 1;
				s->table_hi = k;
				s->fraction = ((ct - lo) << 12) / (hi - lo);
			} else if (!(tag[k] + h < ct)) {
				s->none = 0;
				s->table_lo = s->table_hi = k;
				s->fraction = 0;
				break;
			}
		}
	}
	s->flags[0] = p[69];
	s->flags[1] = p[70];
	s->flags[2] = p[71];
	s->flags[3] = p[72];
	s->flags[4] = p[73];
	s->flags[5] = p[74];
}

/* tisp_lsc_write_all_reg, registers 0x5040..0x5070 (linear mode) */
static inline void t32_lsc_registers(const u8 *p, struct t32_lsc *s, int wdr)
{
	u32 *r = s->regs;

	r[0] = (p[66] & 0x7fU) << 16 | (s->rem_x & 0x7fU) << 8 | (p[67] & 0x7fU) |
	       (u32)(s->rem_y & 0x7f) << 24;
	r[1] = (s->flags[3] & 3U) | (u32)s->cols << 16 | (s->flags[4] & 3U) << 4 |
	       (s->flags[5] & 3U) << 8;
	r[2] = (u32)p[115] << 8 | (u32)p[113] << 31 | p[114] | (s->inv_main & 0x3ffU) << 16;
	r[3] = 1;
	r[4] = wdr ? (u32)s->strength_wdr << 16 | s->strength : (u32)s->strength << 16 | s->strength;
	r[5] = t32_le16(p + 20) | t32_le32(p + 16) << 16;
	r[6] = s->r2_center & 0x7fffff;
	r[7] = (s->flags[1] & 3U) << 4 | (s->flags[2] & 3U) << 8 | (s->flags[0] & 3U) |
	       (s->zone & 7U) << 16;
	r[8] = (p[68] & 1U) << 16 | (p[65] & 1U);
	r[9] = t32_le32(p + 8) & 0x7fffff;
	r[10] = 0;
	r[11] = 0x04000400;
	r[12] = wdr ? (u32)p[64] << 8 | (u32)p[62] << 31 | p[63] | (s->inv_wdr & 0x3ffU) << 16 : r[2];
}

/*
 * Number of mesh cells the vendor's tisp_lsc_write_all_reg loads: with the section's
 * mode byte p[65] set, a fixed 1152 for the IR Bayer patterns (index >= 4) and 576
 * otherwise; with it clear, the grid of tisp_lsc_calc_para_refresh (rows * cols * 2).
 */
static inline u32 t32_lsc_cell_count(const u8 *p, const struct t32_lsc *s, u32 bayer)
{
	if (p[65])
		return (bayer & 0x1f) >= 4 ? 1152 : 576;
	return s->cells;
}

/* round(x / 2^n) as tisp_round_int64 does: arithmetic shift, the bit below added */
static inline long long t32_lsc_round(long long x, unsigned int n)
{
	return (x >> n) + ((x >> (n - 1)) & 1);
}

/*
 * The vendor's radial correction (tisp_lsc_interp_by_ct, section byte 65 clear, after the
 * blend): the plane is a (2 rows) x (2 cols) grid of 16-bit elements, rows / cols being
 * the grid of tisp_lsc_calc_para_refresh; an element at the quadrant distance (dx, dy) in
 * half cells (1, 3, 5 ...) from the centre gets the gain of its normalised squared radius
 * r = (dx^2 + dy^2) * 2^24 / ((2 cols + 1)^2 + (2 rows + 1)^2): the eight thresholds are the
 * squares of the u16 at p[32 + 2 k], the gains the bytes p[77 + k] (plane 0), p[85 + k]
 * (plane 1) and p[93 + k] (plane 2), interpolated linearly between two levels.  A gain
 * (plus its bit 7) below 256 pulls the element towards the black level 2048 >> p[72 + plane]:
 * level + round((element - level) * gain / 256).
 */
static inline u32 t32_lsc_radial(const u8 *p, const struct t32_lsc *s, unsigned int plane, unsigned int e, u32 v)
{
	unsigned int rows = s->rows, cols = s->cols, x, y;
	int thr[9], r, k, lvl = 0, g, level;
	long long dx, dy;
	u32 gain, d;

	if (!rows || !cols)
		return v;
	x = e % (2 * cols);
	y = e / (2 * cols);
	if (y >= 2 * rows)
		return v;
	dy = y < rows ? 2 * (rows - 1 - y) + 1 : 2 * (y - rows) + 1;
	dx = x < cols ? 2 * (cols - 1 - x) + 1 : 2 * (x - cols) + 1;
	for (k = 0; k < 8; k++) {
		int t = (int)t32_le16(p + 32 + 2 * k);

		thr[k] = t * t;
	}
	thr[8] = 0x1000000;
	r = (int)((dx * dx + dy * dy) * (0x1000000 / ((int)((2 * cols + 1) * (2 * cols + 1) + (2 * rows + 1) * (2 * rows + 1)))));
	while (lvl < 9 && !(r < thr[lvl]))
		lvl++;
	if (lvl == 0) {
		g = p[77 + 8 * plane];
	} else if (lvl >= 8) {
		g = p[84 + 8 * plane];
	} else if (thr[lvl] - thr[lvl - 1] <= 0) {
		g = p[77 + 8 * plane + lvl];
	} else {
		int span = thr[lvl] - thr[lvl - 1], pos = r - thr[lvl - 1];

		g = (p[77 + 8 * plane + lvl - 1] * (span - pos) + p[77 + 8 * plane + lvl] * pos) / span;
	}
	gain = (u32)g + (((u32)g >> 7) & 1);
	if (gain >= 256)
		return v;
	level = 2048 >> p[72 + plane];
	d = (int)v > level ? v - level : 0;
	return (u32)level + (u32)t32_lsc_round((long long)d * gain, 8) > 4095 ? 4095 :
	       (u32)level + (u32)t32_lsc_round((long long)d * gain, 8);
}

/* element e of plane `plane` straight from the chosen table(s) (copy or tisp_lsc_mesh_itp blend) */
static inline u32 t32_lsc_raw_elem(const u8 *p, const struct t32_lsc *s, unsigned int plane, unsigned int e)
{
	const u8 *pa = p + T32_LSC_TABLES + s->table_lo * T32_LSC_TABLE_STRIDE + 2 + plane * T32_LSC_PLANE;
	const u8 *pb = p + T32_LSC_TABLES + s->table_hi * T32_LSC_TABLE_STRIDE + 2 + plane * T32_LSC_PLANE;
	u32 v;

	if (s->none) {
		v = 0;
	} else if (s->table_lo == s->table_hi) {
		v = t32_le16(pa + 2 * e);
	} else {
		u32 a = t32_le16(pa + 2 * e), b = t32_le16(pb + 2 * e);

		v = (a + (((b - a) * s->fraction) >> 12)) & 0xffff;
	}
	return v;
}

/*
 * Section byte 65 set: the mesh is the ring format of the first payload (three tables of
 * 3464 bytes: a u16 tag and three planes of 577 u16 elements); the chosen table(s) are
 * blended element by element (tisp_lsc_ring_itp).
 */
#define T32_LSC_RING_TABLES	120U		/* first payload, behind the 120-byte section */
#define T32_LSC_RING_STRIDE	3464U
#define T32_LSC_RING_PLANE	1154U
#define T32_LSC_RING_ELEMS	577U

static inline u32 t32_lsc_ring_elem(const u8 *p, const struct t32_lsc *s, unsigned int plane, unsigned int e)
{
	const u8 *pa = p + T32_LSC_RING_TABLES + s->table_lo * T32_LSC_RING_STRIDE + 2 + plane * T32_LSC_RING_PLANE;
	const u8 *pb = p + T32_LSC_RING_TABLES + s->table_hi * T32_LSC_RING_STRIDE + 2 + plane * T32_LSC_RING_PLANE;
	u32 a, b;

	if (s->none || e >= T32_LSC_RING_ELEMS)
		return 0;
	a = t32_le16(pa + 2 * e);
	b = t32_le16(pb + 2 * e);
	return s->table_lo == s->table_hi ? a : (a + (((b - a) * s->fraction) >> 12)) & 0xffff;
}

/*
 * The vendor keeps the planes of the mesh in a work buffer between updates.  Each update
 * (tisp_lsc_interp_by_ct) with a new colour temperature copies or blends the chosen tables
 * into it, or leaves it as it is when no table applies, and for byte 65 clear then applies
 * the radial correction in place.
 */
struct t32_lsc_work {
	u16 pl[3][2304];
	u32 old_ct;
};

/*
 * tisp_lsc_interp_by_ct.  A non-forced call returns 0 without touching anything unless the
 * colour temperature moved by more than the section's threshold u32 p[12].  Otherwise the
 * table choice (t32_lsc_ct) is made: a copy fills the planes completely (2304 elements, or
 * 577 in the ring format), a blend only the first 2 * (grid cells) elements (the rest is never read, so all are done), no applicable
 * table keeps the old planes.  Returns 1 when the planes were updated.
 */
static inline int t32_lsc_update(const u8 *p, struct t32_lsc *s, struct t32_lsc_work *wk, u32 ct, int force)
{
	unsigned int plane, e, n;
	u32 diff = ct > wk->old_ct ? ct - wk->old_ct : wk->old_ct - ct;

	if (!force && !(t32_le32(p + 12) < diff))
		return 0;
	wk->old_ct = ct;
	t32_lsc_ct(p, s, ct);
	if (!s->none) {
		n = p[65] ? T32_LSC_RING_ELEMS : 2304U;
		for (plane = 0; plane < 3; plane++)
			for (e = 0; e < n; e++)
				wk->pl[plane][e] = (u16)(p[65] ? t32_lsc_ring_elem(p, s, plane, e) :
							 t32_lsc_raw_elem(p, s, plane, e));
	}
	if (!p[65])
		for (plane = 0; plane < 3; plane++)
			for (e = 0; e < 4 * (unsigned int)s->rows * s->cols && e < 2304; e++)
				wk->pl[plane][e] = (u16)t32_lsc_radial(p, s, plane, e, wk->pl[plane][e]);
	return 1;
}

/*
 * the mesh words of one cell from the work planes.  Byte 65 clear: its 6 values (two elements of
 * each plane, 12 bits packed).  Byte 65 set: the vendor's two blocks of 576 cells; cells 0..575
 * pack one element of each plane and its neighbour, cells 576.. get the top byte of plane 1 and
 * the packed plane 1 neighbours only (as the vendor's loop stores them).
 */
static inline void t32_lsc_cell(const u8 *p, const struct t32_lsc_work *wk, unsigned int i, u32 w[3])
{
	u32 v[6], k;

	if (p[65]) {
		unsigned int n = i < 576 ? i : i - 576;
		u32 p0 = wk->pl[0][n], p0n = wk->pl[0][n + 1];
		u32 p1 = wk->pl[1][n], p1n = wk->pl[1][n + 1];
		u32 p2 = wk->pl[2][n], p2n = wk->pl[2][n + 1];

		if (i < 576) {
			w[0] = (p0 & 4095) | (p0n & 4095) << 12 | p1 << 24;
			w[1] = ((p1 >> 8) & 15) | ((p1n << 4) & 0xffff) | (p2 & 4095) << 16 | p2n << 28;
			w[2] = (p2n >> 4) & 0xff;
		} else {
			w[0] = p1 << 24;
			w[1] = ((p1 >> 8) & 15) | ((p1n << 4) & 0xffff);
			w[2] = 0;
		}
		return;
	}
	for (k = 0; k < 6; k++)
		v[k] = wk->pl[k / 2][i * 2 + (k & 1)] & 4095;
	w[0] = v[0] | v[1] << 12 | v[2] << 24;
	w[1] = (v[2] >> 8) | v[3] << 4 | v[4] << 16 | v[5] << 28;
	w[2] = v[5] >> 4;
}

/*
 * The stream-on list as the vendor's: enable, the control registers, the
 * mesh through the port (t32_lsc_cell_count cells), the tail register.
 */
static inline int t32_lsc_fill_list(const u8 *p, struct t32_lsc *s, const struct t32_lsc_work *wk,
				    struct t32_rcfg_list *l, u32 *scratch, u32 bayer)
{
	unsigned int i, k, cells = t32_lsc_cell_count(p, s, bayer);

	t32_rcfg_add(l, T32_LSC_EN, 1);
	for (k = 0; k < 13; k++)
		t32_rcfg_add(l, T32_LSC_CTRL0 + 4 * k, s->regs[k]);
	t32_rcfg_add(l, T32_LSC_PORT_SEL, 0x101);
	for (i = 0; i < cells; i++)
		t32_lsc_cell(p, wk, i, scratch + i * 3);
	t32_rcfg_add_port(l, T32_LSC_PORT_DATA, scratch, cells * 3);
	t32_rcfg_add(l, T32_LSC_PORT_SEL, 0);
	t32_rcfg_add(l, T32_LSC_LIST_TAIL, 0);
	return l->overflow ? -1 : 0;
}

/* everything for a stream-on list at colour temperature ct and gain 0 */
static inline int t32_lsc_setup(const u8 *p, struct t32_lsc *s, struct t32_lsc_work *wk, u32 width, u32 height, u32 ct)
{
	if (t32_lsc_geometry(p, s, width, height))
		return -1;
	t32_lsc_gain(p, s, 0);
	memset(wk, 0, sizeof(*wk));
	t32_lsc_update(p, s, wk, ct, 1);
	t32_lsc_registers(p, s, 0);
	return 0;
}

/*
 * Per-frame paths: tisp_lsc_dn_params_refresh, tisp_lsc_wdr_en, tisp_lsc_wake_up
 * (Matt Davis' recovered C).  The output of the vendor's LSC block is a shadow image
 * (tisp_single_lsc_reg_write / _reg_lut_write: index -> value): 2 = enable, 4..16 = the
 * 13 control registers, 18 = port select, 20 + w = mesh word w.  Checked by
 * tests/t32_oracle (t32_lscd_oracle).
 */
#define T32_LSC_TUNING_OFF 26820U	/* section 7 in the tuning block */
#define T32_LSC_IMG_EN		2
#define T32_LSC_IMG_REG0	4
#define T32_LSC_IMG_SEL		18
#define T32_LSC_IMG_MESH	20

/* the current values the vendor keeps next to the section (offsets 8, 20, 24, 28) */
struct t32_lsc_cur {
	u32 rsvd0[2];
	u32 ct;			/* colour temperature */
	u32 rsvd1[2];
	u32 tgain;		/* total gain, log2 Q16 */
	u32 tgain_s;		/* short exposure total gain (wdr strength) */
	u32 wdr;		/* tisp_lsc_wdr_en */
};

struct t32_lsc_dwr {
	u32 reg, val;
};

struct t32_lsc_ctx {
	const u8 *sec;			/* section 7 (contiguous with its payloads) */
	struct t32_lsc_cur *cur;
	struct t32_lsc s;
	struct t32_lsc_work wk;
	u32 *img;			/* shadow image */
	u32 width, height, bayer;
	u8 direct;			/* vendor flag t32_data_d03a0 */
	struct t32_lsc_dwr *dlog;	/* direct register writes */
	unsigned int ndlog;
	void (*cfg_trig)(struct t32_lsc_ctx *ctx, u8 ch);	/* tisp_single_lsc_rcfg_cfg_trig */
};

static inline void t32_lsc_img_write(struct t32_lsc_ctx *ctx, u8 ch, u32 idx, u32 val)
{
	(void)ch;
	ctx->img[idx] = val;
}

static inline void t32_lsc_reg_write(struct t32_lsc_ctx *ctx, u8 ch, u32 reg, u32 val)
{
	(void)ch;
	ctx->dlog[ctx->ndlog].reg = reg;
	ctx->dlog[ctx->ndlog].val = val;
	ctx->ndlog++;
}

/* tisp_lsc_interp_by_tgain_s: the wdr strength from its own gain */
static inline void t32_lsc_gain_s(const u8 *p, struct t32_lsc *s, u32 gain)
{
	struct t32_lsc t = *s;

	t32_lsc_gain(p, &t, gain);
	s->strength_wdr = t.strength_wdr;
}

/* tisp_lsc_write_all_reg: the constants 2, 7, 14, 15 are not part of it */
static inline void t32_lsc_write_all_reg(struct t32_lsc_ctx *ctx, u8 ch)
{
	unsigned int i, j, k, cells = t32_lsc_cell_count(ctx->sec, &ctx->s, ctx->bayer);
	u32 w[3];

	/* wdr mode: 0x5050 = wdr strength << 16 | strength, 0x5070 the wdr range */
	t32_lsc_registers(ctx->sec, &ctx->s, ctx->cur->wdr != 0);
	for (k = 0; k < 13; k++)
		if (k != 3 && k != 10 && k != 11)
			t32_lsc_img_write(ctx, ch, 4 + k, ctx->s.regs[k]);
	t32_lsc_img_write(ctx, ch, 18, 0x101);
	for (i = 0; i < cells; i++) {
		t32_lsc_cell(ctx->sec, &ctx->wk, i, w);
		for (j = 0; j < 3; j++)
			t32_lsc_img_write(ctx, ch, 20 + 3 * i + j, w[j]);
	}
}

/* the common tail of dn_params_refresh and wdr_en */
static inline int t32_lsc_refresh(struct t32_lsc_ctx *ctx, u8 ch)
{
	const u8 *p = ctx->sec;

	if (t32_lsc_geometry(p, &ctx->s, ctx->width, ctx->height) < 0)
		t32_lsc_geometry(p, &ctx->s, ctx->width, ctx->height);	/* the vendor retries once */
	t32_lsc_update(p, &ctx->s, &ctx->wk, ctx->cur->ct, 1);
	t32_lsc_gain(p, &ctx->s, ctx->cur->tgain);
	t32_lsc_gain_s(p, &ctx->s, ctx->cur->tgain_s);
	t32_lsc_write_all_reg(ctx, ch);
	return 0;
}

static inline int t32_lsc_dn_params_refresh(struct t32_lsc_ctx *ctx, u8 ch, u8 *tuning_base)
{
	ctx->sec = tuning_base + T32_LSC_TUNING_OFF;
	return t32_lsc_refresh(ctx, ch);
}

static inline int t32_lsc_wdr_en(struct t32_lsc_ctx *ctx, u8 ch, u32 en)
{
	ctx->cur->wdr = en;
	return t32_lsc_refresh(ctx, ch);
}

static inline int t32_lsc_wake_up(struct t32_lsc_ctx *ctx, u8 ch)
{
	if (ctx->direct == 1) {
		t32_lsc_reg_write(ctx, ch, 2763, 0);
		t32_lsc_reg_write(ctx, ch, 2764, 0x04000400);
		t32_lsc_reg_write(ctx, ch, 2756, 1);
		t32_lsc_reg_write(ctx, ch, 2751, 1);
	} else {
		t32_lsc_img_write(ctx, ch, 14, 0);
		t32_lsc_img_write(ctx, ch, 15, 0x04000400);
		t32_lsc_img_write(ctx, ch, 7, 1);
		t32_lsc_img_write(ctx, ch, 2, 1);
	}
	t32_lsc_write_all_reg(ctx, ch);
	ctx->cfg_trig(ctx, ch);
	return 0;
}

#endif /* TX_ISP_T32_LSC_H */
