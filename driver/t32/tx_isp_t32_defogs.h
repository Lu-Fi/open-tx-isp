/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 defog per frame software part: Tiziano_defog_soft (0x5da70), the helper behind
 * t32_defog_ops.soft of tx_isp_t32_defog.h.  It works on the 3948 byte parameter / LUT
 * block (struct t32_defog_par, vendor byte offsets below) of a 10 x 18 grid.
 *
 * Per cell: the three channel statistics (u32, +0 / +720 / +1440) are scaled by the
 * quadrant gain (+2160..+2172, quadrant by rows < +2455 / columns < +2456) as a 64 bit
 * product shifted right by 31 - shift (+2457) plus a rounding bit taken from the gain
 * (bit 30 - shift of the gain, vendor), clipped to 255 above.  The luma
 * (38 G + 19 R + 7 B) >> 6 replaces the previous luma (+2184) and its change goes to a
 * 16 bit difference grid.  Unless +2415 is set: the strength curve (17 points at +2364,
 * scaled into +2417..+2418) maps every channel (+2416 = 0) or the luma (+2416 != 0)
 * into the grids +2458 / +2638 / +2818, the transmission curve (tbl_a -> tbl_b, 5
 * points, linear between them) maps the luma into the grid +2998; both curves are
 * cached per value (+3434 / +3690) with the cache flags +3178 (bit 0 / +16 counters).
 * Then optional (+2420 == 1) a local activity mask from 5 x 5 windows of the
 * difference grid (thresholds +2421..+2423), max and 3 x 3 filtered, attenuates the
 * transmission grid; optional (+2424 == 1) the 5 x 5 filter (+2390) smooths the
 * strength grids (with +2416 the first one is copied to the other two).
 *
 * BEYOND VENDOR (#281): channel values are clipped to 0 below (vendor: only above, a
 * negative value, product low word >= 2^31 or a negative gain, indexes the curve, cache
 * and flag tables before their start); values 0..255 are bit-exact.
 * Vendor behaviour kept (not fixed, a change needs the user's OK): the rounding bit
 * comes from the gain, not from the product; the window "variance" of the activity mask is the squared deviation of the window's last
 * cell only; the cache flags are byte counters (+1 / +16) that can carry into each
 * other; the signed divides by a zero threshold (+2421 = 0, +2422 * 4 = +2423 * 64)
 * give the MIPS model's -1 / 1.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_defogs_oracle).
 */
#ifndef TX_ISP_T32_DEFOGS_H
#define TX_ISP_T32_DEFOGS_H

#include "tx_isp_t32_defog.h"

#ifndef __KERNEL__
typedef int32_t s32;	/* identical redefinitions are fine (C11) */
typedef int16_t s16;
typedef int64_t s64;
#endif

#define T32_DFS_ROWS		10
#define T32_DFS_COLS		18
#define T32_DFS_STAT		0	/* 3 x 180 u32 channel statistics */
#define T32_DFS_GAIN		2160	/* 4 u32 quadrant gains */
#define T32_DFS_LUMA		2184	/* previous luma grid */
#define T32_DFS_CURVE		2364	/* 17 point strength curve */
#define T32_DFS_K9		2381
#define T32_DFS_K25		2390
#define T32_DFS_NOSTR		2415
#define T32_DFS_LUMAMODE	2416
#define T32_DFS_STR_LO		2417
#define T32_DFS_STR_HI		2418
#define T32_DFS_MASK		2420
#define T32_DFS_MK_K		2421
#define T32_DFS_MK_LO		2422
#define T32_DFS_MK_HI		2423
#define T32_DFS_SMOOTH		2424
#define T32_DFS_QROW		2455
#define T32_DFS_QCOL		2456
#define T32_DFS_SHIFT		2457
#define T32_DFS_STR		2458	/* 3 strength grids of 180 B */
#define T32_DFS_TRANS		2998
#define T32_DFS_FLAG		3178
#define T32_DFS_STRC		3434
#define T32_DFS_TRC		3690

static inline s32 t32_dfs_sdiv(s32 n, s32 d)
{
	if (d == 0)
		return n < 0 ? 1 : -1;
	if (n == (s32)0x80000000 && d == -1)
		return n;
	return n / d;
}

static inline s32 t32_dfs_rd32(const u8 *p)
{
	u32 v;

	memcpy(&v, p, 4);
	return (s32)v;
}

/* strength curve at v (cached): 17 points, 16 steps of 16, scaled into lo .. hi */
static inline u8 t32_dfs_strength(u8 *b, s32 v)
{
	const u8 *c = b + T32_DFS_CURVE + (v >> 4);
	s32 x;
	u32 y;

	if (b[T32_DFS_FLAG + v] & 1)
		return b[T32_DFS_STRC + v];
	x = c[0] + (((c[1] - c[0]) * (v & 15) + 8) >> 4);
	x = x * (b[T32_DFS_STR_HI] - b[T32_DFS_STR_LO]) + 127;
	y = (u32)x + ((u32)x << 8) + ((u32)b[T32_DFS_STR_LO] << 16);
	b[T32_DFS_STRC + v] = (u8)(y >> 16);
	b[T32_DFS_FLAG + v]++;
	return (u8)(y >> 16);
}

/* transmission curve at the luma y (cached): tbl_a thresholds -> tbl_b values, linear, rounded */
static inline u8 t32_dfs_trans(u8 *b, const u8 *ta, const u8 *tb, s32 y)
{
	u32 i;
	u8 r;

	if (b[T32_DFS_FLAG + y] & 0x10)
		return b[T32_DFS_TRC + y];
	if (y < ta[0]) {
		r = tb[0];
	} else {
		for (i = 0; i < 4 && !(y < ta[i + 1]); i++)
			;
		if (i == 4) {
			r = tb[4];
		} else {
			s32 da = ta[i + 1] - ta[i];

			r = (u8)(t32_dfs_sdiv((y - ta[i]) * (tb[i + 1] - tb[i]) + (da >> 1), da) + tb[i]);
		}
	}
	b[T32_DFS_TRC + y] = r;
	b[T32_DFS_FLAG + y] += 16;
	return r;
}

/* local activity mask from the 5 x 5 window (clipped to the grid) of the luma differences */
static inline void t32_dfs_mask(const u8 *b, const s16 *d, u8 *m)
{
	s32 k = b[T32_DFS_MK_K] << 4, lo = b[T32_DFS_MK_LO] << 2, hi = b[T32_DFS_MK_HI] << 6;
	int r, c, i, j;

	for (r = 0; r < T32_DFS_ROWS; r++) {
		int r0 = r < 3 ? 0 : r - 2, r1 = r < 7 ? r + 2 : 9;

		for (c = 0; c < T32_DFS_COLS; c++) {
			int c0 = c < 3 ? 0 : c - 2, c1 = c < 15 ? c + 2 : 17;
			s32 n = (c1 - c0 + 1) * (r1 - r0 + 1), sum = 0, act = 0, mean, sq = 0, v;

			for (i = r0; i <= r1; i++)
				for (j = c0; j <= c1; j++) {
					s32 x = d[i * T32_DFS_COLS + j];

					sum += x;
					act += x < 1 ? -x : x;
				}
			mean = t32_dfs_sdiv(sum, n);
			act = t32_dfs_sdiv((s32)((u32)act << 6), n);
			for (i = r0; i <= r1; i++)
				for (j = c0; j <= c1; j++)
					sq = (d[i * T32_DFS_COLS + j] - mean) * (d[i * T32_DFS_COLS + j] - mean);
			v = t32_dfs_sdiv((t32_dfs_sdiv(sq, n) + k) * act, k);
			if (hi < v)
				m[r * T32_DFS_COLS + c] = 0xff;
			else if (v < lo)
				m[r * T32_DFS_COLS + c] = 0;
			else
				m[r * T32_DFS_COLS + c] = (u8)t32_dfs_sdiv((s32)((u32)(v - lo) << 8), hi - lo);
		}
	}
}

static inline void t32_defog_soft(struct t32_defog_par *par)
{
	u8 *b = par->b, m[T32_DEFOG_GRID_N];
	s16 d[T32_DEFOG_GRID_N];
	u32 sh = b[T32_DFS_SHIFT];
	int r, c, k;

	memset(d, 0, sizeof(d));
	memset(m, 0, sizeof(m));
	for (r = 0; r < T32_DFS_ROWS; r++) {
		for (c = 0; c < T32_DFS_COLS; c++) {
			int i = r * T32_DFS_COLS + c;
			int q = (r < b[T32_DFS_QROW] ? 0 : 2) + (c < b[T32_DFS_QCOL] ? 0 : 1);
			s32 g = t32_dfs_rd32(b + T32_DFS_GAIN + 4 * q), ch[3], y;
			u32 rnd = (u32)(g >> ((30 - sh) & 31)) & 1;

			for (k = 0; k < 3; k++) {
				s64 p = (s64)(u32)t32_dfs_rd32(b + T32_DFS_STAT + 720 * k + 4 * i) * g;
				s32 v = (s32)((u32)(p >> ((31 - sh) & 63)) + rnd);

				ch[k] = v < 0 ? 0 : v < 256 ? v : 255;	/* BEYOND VENDOR (#281): vendor no lower clip */
			}
			y = (38 * ch[1] + 19 * ch[0] + 7 * ch[2]) >> 6;
			if (!(y < 256))
				y = 255;
			d[i] = (s16)(y - b[T32_DFS_LUMA + i]);
			b[T32_DFS_LUMA + i] = (u8)y;
			if (b[T32_DFS_NOSTR])
				continue;
			if (b[T32_DFS_LUMAMODE]) {
				u8 s = t32_dfs_strength(b, y);

				for (k = 0; k < 3; k++)
					b[T32_DFS_STR + 180 * k + i] = s;
			} else {
				for (k = 0; k < 3; k++)
					b[T32_DFS_STR + 180 * k + i] = t32_dfs_strength(b, ch[k]);
			}
			b[T32_DFS_TRANS + i] = t32_dfs_trans(b, par->tbl_a, par->tbl_b, y);
		}
	}
	if (b[T32_DFS_MASK] == 1) {
		t32_dfs_mask(b, d, m);
		t32_defog_max_filter(m, m);
		t32_defog_img_filter9(m, m, b + T32_DFS_K9);
		for (k = 0; k < (int)T32_DEFOG_GRID_N; k++)
			if (m[k])
				b[T32_DFS_TRANS + k] = (u8)((s32)((256 - m[k]) * b[T32_DFS_TRANS + k]) >> 8);
	}
	if (b[T32_DFS_SMOOTH] != 1)
		return;
	t32_defog_img_filter25(b + T32_DFS_STR, b + T32_DFS_STR, b + T32_DFS_K25);
	if (b[T32_DFS_LUMAMODE]) {
		memcpy(b + T32_DFS_STR + 180, b + T32_DFS_STR, 180);
		memcpy(b + T32_DFS_STR + 360, b + T32_DFS_STR, 180);
	} else {
		t32_defog_img_filter25(b + T32_DFS_STR + 180, b + T32_DFS_STR + 180, b + T32_DFS_K25);
		t32_defog_img_filter25(b + T32_DFS_STR + 360, b + T32_DFS_STR + 360, b + T32_DFS_K25);
	}
}

#endif /* TX_ISP_T32_DEFOGS_H */
