/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 defog grid weights: defog_count_weight35abc (0x5aa34), the helper behind
 * t32_defog_ops.count_weight of tx_isp_t32_defog.h.
 *
 * Arguments as the vendor's caller passes them: ch, a = ctx height, b = ctx
 * width, gain = section byte +429, thr24 = section +80 (24 u16, rewritten),
 * thr31 = section +128 (31 u16, rewritten), out[5] = section +430/+462/+494/
 * +526/+558 (32 bytes each, the weight curves).
 *
 * Flow: the grid has bx = (a + 5) / 10 by by = (b + 9) / 18 cells; the two
 * vendor tables (rodata bfa30 / bfa60) are scaled by the area factor
 * g = ((a * b + 1012) / 2025 * gain + 64) >> 7 into thr24 and a 63 entry
 * distance table (thr31 = its odd entries); the tails of thr24 / thr31 are
 * clipped to 8191 / 16383 and made strictly decreasing downwards (index 0
 * excluded) where they are not.  The per channel scratch (13 planes of n =
 * bx * by bytes, t32_data_e20e0[ch] with capacity t32_data_e211c[ch]) only
 * grows and is cleared over its whole capacity.  For every cell 8 squared
 * distances to points of a 3 x 3 neighbour lattice are looked up in the
 * distance table (63 - first index whose entry is above the distance; no
 * store when none is), 5 index planes are derived from them and five
 * defog_wei_interpcot calls build the weight curves.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK): when the
 * vmalloc fails the vendor prints and then memsets NULL (a crash); the port
 * returns -1 there instead of crashing (no output written).  All grid
 * arithmetic is 32 bit with wrap, the distance compare is unsigned.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_defogcw_oracle).
 */
#ifndef TX_ISP_T32_DEFOGCW_H
#define TX_ISP_T32_DEFOGCW_H

#include "tx_isp_t32_defogw.h"

typedef int32_t s32_t32cw;

/* rodata t32_data_bfa30 (data.c:2735..2746) */
static const u16 t32_defog_cw_t24[24] = {
	255, 521, 799, 1090, 1395, 1717, 2057, 2417, 2799, 3207, 3643, 4114,
	4624, 5180, 5792, 6471, 7237, 8111, 9133, 10359, 11896, 13953, 17075, 23792
};

/* rodata t32_data_bfa60 (data.c:2747..2778), 63 entries */
static const u16 t32_defog_cw_t63[63] = {
	133, 269, 407, 547, 689, 834, 982, 1132, 1285, 1440, 1599, 1761, 1926, 2094, 2266, 2442,
	2621, 2804, 2991, 3183, 3379, 3580, 3785, 3996, 4213, 4435, 4663, 4898, 5140, 5389, 5645, 5910,
	6183, 6465, 6758, 7061, 7375, 7702, 8042, 8397, 8767, 9155, 9562, 9989, 10440, 10916, 11421, 11959,
	12534, 13152, 13819, 14544, 15338, 16216, 17197, 18130, 19595, 21114, 22974, 25371, 28750, 34526, 40368
};

struct t32_defog_cw_ops {
	void *(*vmalloc)(u32 size);
	void (*vfree)(void *p);
};

/* clip the last entry to max, then walk down (to index 1) while an entry is not below its upper neighbour */
static inline void t32_defog_cw_tail(u16 *t, u32 last, u32 max)
{
	u32 j;

	if (!(t[last] > max))
		return;
	t[last] = (u16)max;
	for (j = last - 1; j > 0; j--) {
		if (t[j] < t[j + 1])
			break;
		t[j] = (u16)(t[j + 1] - 1);
	}
}

/* rounded squared distance (>> 6) looked up in the distance table: store 63 - first index above it */
static inline void t32_defog_cw_look(const u16 *dist, u32 dx2, u32 dy2, u8 *p)
{
	u32 d = (u32)((s32_t32cw)(dx2 + dy2 + 32) >> 6), k;

	for (k = 0; k < 63; k++) {
		if (d < dist[k]) {
			*p = (u8)(63 - k);
			return;
		}
	}
}

static inline int t32_defog_count_weight35abc(u32 ch, u32 a, u32 b, u32 gain, u16 *thr24, u16 *thr31,
					      u8 *const out[5], u8 **vbuf, u32 *vsize,
					      const struct t32_defog_cw_ops *ops)
{
	u16 dist[63];
	u32 bx = (a + 5) / 10, by = (b + 9) / 18, n = bx * by, g, i, j, stride, row;
	u32 x0, x1, x2, y0, y1, y2, xx[3], yy[3];
	u8 *p[13];

	ch &= 0xff;
	g = ((a * b + 1012) / 2025 * gain + 64) >> 7;
	for (i = 0; i < 24; i++)
		thr24[i] = (u16)((t32_defog_cw_t24[i] * g + 8192) >> 14);
	for (i = 0; i < 63; i++)
		dist[i] = (u16)((t32_defog_cw_t63[i] * g + 4096) >> 13);
	for (i = 0; i < 31; i++)
		thr31[i] = dist[2 * i + 1];
	t32_defog_cw_tail(thr24, 23, 8191);
	t32_defog_cw_tail(thr31, 30, 16383);

	if (vsize[ch] < 13 * n) {
		vsize[ch] = 13 * n;
		ops->vfree(vbuf[ch]);
		vbuf[ch] = ops->vmalloc(vsize[ch]);
		if (!vbuf[ch])
			return -1;	/* vendor: isp_printf, then memset(NULL) */
	}
	memset(vbuf[ch], 0, vsize[ch]);
	for (i = 0; i < 13; i++)
		p[i] = vbuf[ch] + i * n;

	stride = (s32_t32cw)by < 0 ? 0 : by;
	row = 0;
	for (i = 0; (s32_t32cw)i < (s32_t32cw)bx; i++, row += stride) {
		x0 = 5 * bx - 1 - 2 * i;
		x1 = x0 - 2 * bx;
		x2 = x1 - 2 * bx;
		xx[0] = x0 * x0; xx[1] = x1 * x1; xx[2] = x2 * x2;
		for (j = 0; (s32_t32cw)j < (s32_t32cw)by; j++) {
			y0 = 5 * by - 1 - 2 * j;
			y1 = y0 - 2 * by;
			y2 = y1 - 2 * by;
			yy[0] = y0 * y0; yy[1] = y1 * y1; yy[2] = y2 * y2;
			t32_defog_cw_look(dist, xx[0], yy[0], p[0] + row + j);
			t32_defog_cw_look(dist, xx[0], yy[1], p[1] + row + j);
			t32_defog_cw_look(dist, xx[0], yy[2], p[2] + row + j);
			t32_defog_cw_look(dist, xx[1], yy[0], p[3] + row + j);
			t32_defog_cw_look(dist, xx[1], yy[1], p[4] + row + j);
			t32_defog_cw_look(dist, xx[1], yy[2], p[5] + row + j);
			t32_defog_cw_look(dist, xx[2], yy[0], p[6] + row + j);
			t32_defog_cw_look(dist, xx[2], yy[1], p[7] + row + j);
		}
	}
	for (i = 0; i < n; i++) {
		p[8][i] = p[4][i] >> 1;
		p[9][i] = (u8)((p[4][i] + p[5][i]) >> 2);
		p[10][i] = (u8)((p[4][i] + p[7][i]) >> 2);
		p[11][i] = p[5][i] >> 1;
		p[12][i] = p[7][i] >> 1;
	}
	t32_defog_wei_interpcot(n, p[8], p[0], out[0]);
	t32_defog_wei_interpcot(n, p[9], p[1], out[1]);
	t32_defog_wei_interpcot(n, p[10], p[3], out[2]);
	t32_defog_wei_interpcot(n, p[11], p[2], out[3]);
	t32_defog_wei_interpcot(n, p[12], p[6], out[4]);
	return 0;
}

#endif /* TX_ISP_T32_DEFOGCW_H */
