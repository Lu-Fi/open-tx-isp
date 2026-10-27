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

/* table(s) for the colour temperature: zones split by the 4 breakpoints */
static inline void t32_lsc_ct(const u8 *p, struct t32_lsc *s, u32 ct)
{
	u32 b0 = t32_le16(p + 0x18), b1 = t32_le16(p + 0x1a);
	u32 b2 = t32_le16(p + 0x1c), b3 = t32_le16(p + 0x1e);
	u32 zone = ct <= b0 ? 0 : ct < b1 ? 1 : ct <= b2 ? 2 : ct < b3 ? 3 : 4;
	u32 first = zone == 1 ? b0 : b2, last = zone == 1 ? b1 : b3;

	s->table_lo = zone / 2;
	s->table_hi = s->table_lo + (zone & 1);
	s->fraction = 0;
	if ((zone & 1) && last > first)
		s->fraction = ((ct - first) << 12) / (last - first);
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

/* the mesh words of one cell from the chosen table (stage 1: no blend) */
static inline void t32_lsc_cell(const u8 *p, const struct t32_lsc *s, unsigned int i, u32 w[3])
{
	unsigned int t = s->fraction < 2048 ? s->table_lo : s->table_hi;
	const u8 *pl = p + T32_LSC_TABLES + t * T32_LSC_TABLE_STRIDE + 2;
	u32 v[6], k;

	for (k = 0; k < 6; k++)
		v[k] = t32_le16(pl + (k / 2) * T32_LSC_PLANE + i * 4 + (k & 1) * 2) & 4095;
	w[0] = v[0] | v[1] << 12 | v[2] << 24;
	w[1] = (v[2] >> 8) | v[3] << 4 | v[4] << 16 | v[5] << 28;
	w[2] = v[5] >> 4;
}

/*
 * The stream-on list as the vendor's: enable, the control registers, the
 * mesh through the port (always all 1152 cells), the tail register.
 */
static inline int t32_lsc_fill_list(const u8 *p, struct t32_lsc *s, struct t32_rcfg_list *l,
				    u32 *scratch)
{
	unsigned int i, k;

	t32_rcfg_add(l, T32_LSC_EN, 1);
	for (k = 0; k < 13; k++)
		t32_rcfg_add(l, T32_LSC_CTRL0 + 4 * k, s->regs[k]);
	t32_rcfg_add(l, T32_LSC_PORT_SEL, 0x101);
	for (i = 0; i < T32_LSC_CELLS; i++)
		t32_lsc_cell(p, s, i, scratch + i * 3);
	t32_rcfg_add_port(l, T32_LSC_PORT_DATA, scratch, T32_LSC_WORDS);
	t32_rcfg_add(l, T32_LSC_PORT_SEL, 0);
	t32_rcfg_add(l, T32_LSC_LIST_TAIL, 0);
	return l->overflow ? -1 : 0;
}

/* everything for a stream-on list at colour temperature ct and gain 0 */
static inline int t32_lsc_setup(const u8 *p, struct t32_lsc *s, u32 width, u32 height, u32 ct)
{
	if (t32_lsc_geometry(p, s, width, height))
		return -1;
	t32_lsc_gain(p, s, 0);
	t32_lsc_ct(p, s, ct);
	t32_lsc_registers(p, s, 0);
	return 0;
}

#endif /* TX_ISP_T32_LSC_H */
