/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AE / AWB statistics read-out.  Unlike the T32 (packed 22 /
 * 14 bit fields, AWB zones in groups of 4, tx_isp_t32_stats.h), the T33
 * engines write whole fields ("flat" records).  Layout as the vendor module
 * reads it:
 *
 *  - every buffer starts with a 16 bit tag in an 8 byte head and carries
 *    the tag again after its data; the vendor drops a buffer whose two tags
 *    differ (AE: tisp_ae_weight_mean counts it in stAeInfo + 0x4de0);
 *  - AE zones (tisp_ae_weight_mean): buffer 0x1000 bytes, index in bits
 *    4..5 of 0x1708c (tisp_normal_ae_interrupt_static); 15 zones a row
 *    whatever the columns used, 16 bytes a zone at +8: three luma sums
 *    (u32) and the numbers of quads below / above the levels (u16); tag
 *    again at +0xe18 (8 + 15 * 15 * 16);
 *  - AE histogram: buffer 0x800 bytes, index in bits 0..1 of 0x1708c
 *    (tisp_normal_ae_interrupt_hist), 256 u32 bins at +8, tag at +0x408;
 *  - AWB zones (tisp_t33_awb_long_alogrithm, tisp_t33_awb_sat_weight):
 *    buffer 0x7400 bytes, index in bits 0..1 of 0x1608c
 *    (tisp_awb_interrupt_static); 28 bytes a zone at +8: R, G, B sums of
 *    all quads and of the white ones (u32 each), then their numbers (u16
 *    each); a row padded to a multiple of 8 bytes (the vendor adds 4 bytes
 *    a row for an odd column count); tag after the last row.
 *
 * The ratios the vendor's AWB takes from a zone: white R << 10 / white G
 * and white B << 10 / white G (0 when the white G sum is 0).
 */
#ifndef TX_ISP_T33_STATS_H
#define TX_ISP_T33_STATS_H

/* u64 / u64: div64_u64 in the kernel (no __udivdi3 on 32-bit MIPS), plain C on the host */
#ifdef __KERNEL__
#include <linux/math64.h>
#define t33_st_div64(a, b)	div64_u64((a), (b))
#else
#define t33_st_div64(a, b)	((a) / (b))
#endif

#define T33_STAT_HEAD		8U
#define T33_AE_ZBUF_BYTES	0x1000U
#define T33_AE_HBUF_BYTES	0x800U
#define T33_AE_GRID		15U		/* zones a row of the AE buffer */
#define T33_AE_ZONE_BYTES	16U
#define T33_AE_ZTAG		(T33_STAT_HEAD + T33_AE_GRID * T33_AE_GRID * T33_AE_ZONE_BYTES)	/* 0xe18 */
#define T33_AE_HTAG		(T33_STAT_HEAD + 256U * 4U)	/* 0x408 */
#define T33_AE_IDX_REG		0x1708cU
#define T33_AWB_IDX_REG		0x1608cU
#define T33_AWB_ZONE_BYTES	28U
#define T33_AWB_GRID_MAX	32U		/* 0x7400 / (32 * 28) rows of 32 */

static inline u32 t33_st_le16(const u8 *p)
{
	return p[0] | (u32)p[1] << 8;
}

static inline u32 t33_st_le32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* AE buffer indices from 0x1708c: zone buffer (bits 4..5), histogram buffer (bits 0..1) */
static inline u32 t33_ae_zbuf_idx(u32 reg)
{
	return (reg & 0x3f) >> 4;
}

static inline u32 t33_ae_hbuf_idx(u32 reg)
{
	return reg & 3;
}

/* AWB buffer index from 0x1608c */
static inline u32 t33_awb_buf_idx(u32 reg)
{
	return reg & 3;
}

/* bytes of one AWB row of cols zones */
static inline u32 t33_awb_row_bytes(u32 cols)
{
	return (cols * T33_AWB_ZONE_BYTES + 7) & ~7U;
}

/* offset of the AWB buffer's closing tag */
static inline u32 t33_awb_tag_off(u32 rows, u32 cols)
{
	return T33_STAT_HEAD + rows * t33_awb_row_bytes(cols);
}

struct t33_awb_zone {
	u32 r, g, b;		/* all quads */
	u32 wr, wg, wb;		/* white quads */
	u32 n, wn;		/* numbers of quads */
};

/* AWB zone (row, col) of a buffer of cols columns */
static inline void t33_awb_zone_get(const u8 *buf, u32 cols, u32 row, u32 col, struct t33_awb_zone *z)
{
	const u8 *p = buf + T33_STAT_HEAD + row * t33_awb_row_bytes(cols) + col * T33_AWB_ZONE_BYTES;

	z->r = t33_st_le32(p);
	z->g = t33_st_le32(p + 4);
	z->b = t33_st_le32(p + 8);
	z->wr = t33_st_le32(p + 12);
	z->wg = t33_st_le32(p + 16);
	z->wb = t33_st_le32(p + 20);
	z->n = t33_st_le16(p + 24);
	z->wn = t33_st_le16(p + 26);
}

/* summary of one statistics frame */
struct t33_awb_sum {
	u32 tag;		/* the buffer's tag */
	u32 zones, white;	/* zones read, zones with white quads */
	u32 rg, bg;		/* white R << 10 / G and B << 10 / G over all zones */
	u32 wr, wg, wb;		/* white sums over all zones, >> 8 */
};

/*
 * Read an AWB buffer of rows x cols zones; 0 and *s filled, -1 when the
 * grid does not fit or the tags differ (the vendor drops such a buffer).
 */
static inline int t33_awb_read(const u8 *buf, u32 len, u32 rows, u32 cols, struct t33_awb_sum *s)
{
	u32 r, c, toff = t33_awb_tag_off(rows, cols);
	unsigned long long wr = 0, wg = 0, wb = 0;
	struct t33_awb_zone z;

	if (!rows || !cols || rows > T33_AWB_GRID_MAX || cols > T33_AWB_GRID_MAX || toff + 2 > len)
		return -1;
	s->tag = t33_st_le16(buf);
	if (t33_st_le16(buf + toff) != s->tag)
		return -1;
	s->zones = rows * cols;
	s->white = 0;
	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++) {
			t33_awb_zone_get(buf, cols, r, c, &z);
			if (z.wn)
				s->white++;
			wr += z.wr;
			wg += z.wg;
			wb += z.wb;
		}
	s->wr = (u32)(wr >> 8);
	s->wg = (u32)(wg >> 8);
	s->wb = (u32)(wb >> 8);
	/* ratios on the >> 8 sums */
	s->rg = s->wg ? (u32)t33_st_div64((unsigned long long)s->wr << 10, s->wg) : 0;
	s->bg = s->wg ? (u32)t33_st_div64((unsigned long long)s->wb << 10, s->wg) : 0;
	return 0;
}

struct t33_ae_sum {
	u32 tag;
	u32 zones;		/* zones read */
	u32 luma;		/* sum of the three luma sums over all zones, >> 8 */
	u32 lo, hi;		/* quads below / above the levels, all zones */
};

/* Read an AE zone buffer of rows x cols (at most 15 x 15); -1: grid or tag mismatch */
static inline int t33_ae_read(const u8 *buf, u32 rows, u32 cols, struct t33_ae_sum *s)
{
	unsigned long long l = 0;
	u32 r, c;

	if (!rows || !cols || rows > T33_AE_GRID || cols > T33_AE_GRID)
		return -1;
	s->tag = t33_st_le16(buf);
	if (t33_st_le16(buf + T33_AE_ZTAG) != s->tag)
		return -1;
	s->zones = rows * cols;
	s->lo = s->hi = 0;
	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++) {
			const u8 *p = buf + T33_STAT_HEAD + (r * T33_AE_GRID + c) * T33_AE_ZONE_BYTES;

			l += (unsigned long long)t33_st_le32(p) + t33_st_le32(p + 4) + t33_st_le32(p + 8);
			s->lo += t33_st_le16(p + 12);
			s->hi += t33_st_le16(p + 14);
		}
	s->luma = (u32)(l >> 8);
	return 0;
}

/* Read an AE histogram buffer: 256 bins; mean bin * 16 in *mean16; -1: tag mismatch */
static inline int t33_ae_hist_read(const u8 *buf, u32 *tag, u32 *mean16)
{
	unsigned long long n = 0, m = 0;
	u32 i, v;

	*tag = t33_st_le16(buf);
	if (t33_st_le16(buf + T33_AE_HTAG) != *tag)
		return -1;
	for (i = 0; i < 256; i++) {
		v = t33_st_le32(buf + T33_STAT_HEAD + 4 * i);
		n += v;
		m += (unsigned long long)v * i;
	}
	*mean16 = n ? (u32)t33_st_div64(m * 16, n) : 0;
	return 0;
}

#endif /* TX_ISP_T33_STATS_H */
