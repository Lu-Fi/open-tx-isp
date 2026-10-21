/* SPDX-License-Identifier: MIT */
#ifndef TX_ISP_T41_TMO_MAP_H
#define TX_ISP_T41_TMO_MAP_H

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
#include "tx_isp_t41_tmo.h"
#include "tx_isp_t41_tmo_kernels.h"

#define T41_TMO_COLS 25U
#define T41_TMO_ROWS 15U
#define T41_TMO_TILES (T41_TMO_COLS * T41_TMO_ROWS)
#define T41_TMO_BINS 10U
#define T41_TMO_SAMPLES (T41_TMO_TILES * T41_TMO_BINS)

/* ISA-scalar translation of the H20250310a local histogram filter. Input
 * is tile-major, output is bin-major. All Q shifts, lane-width truncations,
 * zero padding and temporal rounding are deliberate. No sensor identity or
 * captured output is involved. Callers own serialization / complete frames.
 * Modes 0/1 select spatial/range kernels from calibration; mode 2's separate
 * adaptive kernel selector is not yet recovered and is rejected explicitly.
 */
static inline int t41_tmo_map(const unsigned char *p, unsigned int bytes,
		unsigned int shift, const unsigned int *sums,
		const unsigned int *counts, const unsigned int *previous,
		int first_frame, unsigned int *out)
{
	unsigned int tile, bin, radius, mode;
	if (!p || bytes != T41_TMO_PARAM_BYTES || !sums || !counts ||
	    !previous || !out || out == sums || out == counts ||
	    shift > 9 || p[0x1742] > 4 || p[0x1739] > 1 || p[0x173c] > 1)
		return -1;
	radius = p[0x1742];
	mode = p[0x1739];
	if (mode == 1 && (p[0x173b] > 10 || p[0x173a] > 14))
		return -1;
	for (tile = 0; tile < T41_TMO_TILES; ++tile) {
		if (!mode && (p[0x230 + tile] > 10 || p[0x3a7 + tile] > 14))
			return -1;
	}
	for (tile = 0; tile < T41_TMO_SAMPLES; ++tile) {
		if (sums[tile] > 65535 || counts[tile] > 65535 ||
		    (!first_frame && previous[tile] > 4095))
			return -1;
	}
	for (tile = 0; tile < T41_TMO_TILES; ++tile) {
		unsigned int n[T41_TMO_BINS] = { 0 };
		unsigned int s[T41_TMO_BINS] = { 0 };
		unsigned int sk = mode ? p[0x173b] : p[0x230 + tile];
		unsigned int rk = mode ? p[0x173a] : p[0x3a7 + tile];
		unsigned int extra = radius == 4 ? 2 : 0;
		int row = tile / T41_TMO_COLS, col = tile % T41_TMO_COLS;
		int dy, dx;
		for (dy = -(int)radius; dy <= (int)radius; ++dy) {
			int y = row + dy;
			if (y < 0 || y >= (int)T41_TMO_ROWS)
				continue;
			for (dx = -(int)radius; dx <= (int)radius; ++dx) {
				int x = col + dx;
				unsigned int ax = dx < 0 ? -dx : dx;
				unsigned int ay = dy < 0 ? -dy : dy;
				unsigned int weight, src, swap;
				if (x < 0 || x >= (int)T41_TMO_COLS)
					continue;
				if (ax > ay) { swap = ax; ax = ay; ay = swap; }
				weight = t41_tmo_spatial_kernels[sk][ay * (ay + 1) / 2 + ax];
				if (!weight)
					continue;
				src = (y * T41_TMO_COLS + x) * T41_TMO_BINS;
				for (bin = 0; bin < T41_TMO_BINS; ++bin) {
					unsigned int count = counts[src + bin];
					unsigned int luma = ((sums[src + bin] << 9) +
						count * (819U * bin)) >> shift;
					n[bin] += weight * count;
					s[bin] += (weight * luma) >> extra;
				}
			}
		}
		for (bin = 0; bin < T41_TMO_BINS; ++bin) {
			n[bin] >>= 8;
			s[bin] >>= 10 - extra;
		}
		for (bin = 0; bin < T41_TMO_BINS; ++bin) {
			unsigned long long numerator = 0, denominator = 0;
			unsigned int value, divisor, index = bin * T41_TMO_TILES + tile;
			int k;
			for (k = -2; k <= 2; ++k) {
				int b = (int)bin + k;
				unsigned int w = t41_tmo_range_kernels[rk][k < 0 ? -k : k];
				if (b < 0 || b >= (int)T41_TMO_BINS)
					continue;
				numerator += (w * s[b]) >> 1;
				denominator += w * n[b];
			}
			divisor = (unsigned int)(denominator >> (shift + 5));
			if (!divisor)
				divisor = 1;
			value = (((unsigned int)(numerator >> 2) / divisor) >> 1) & 32767;
			if (p[0x173c]) {
				unsigned int floor = (bin * 4095 + 5) / 10;
				if (value < floor)
					value = floor;
			}
			if (value > 4095)
				value = 4095;
			if (!first_frame)
				value = (previous[index] * 15 + value) >> 4;
			out[index] = value;
		}
	}
	return 0;
}


/*
 * Same result as t41_tmo_map(), bit for bit, with fewer operations per
 * frame (no SIMD; stock tisp_tmo_fpga uses MXU for the same filter).
 *
 * - The spatial weight depends only on (|dx|, |dy|). Values at +-dx are
 *   first added into horizontal pair sums, so a tile needs (2r+1)*(r+1)
 *   products instead of (2r+1)^2. In 32-bit unsigned arithmetic
 *   w*a + w*b == w*(a+b) also holds after wrap-around, as the reference
 *   sums wrap too.
 * - Luma is computed once per cell, not once per tile/neighbour pair, and
 *   zero padding replaces the bounds checks.
 * - Radius 4 shifts every luma product (extra = 2) before summing. While
 *   no product w*l overflows, (w*l)>>2 == w*(l>>2) + (w*(l&3))>>2, so the
 *   pair sums carry l>>2 and the two low-bit pairs pick a precomputed
 *   correction. If a product could overflow (only for shift < 5), the
 *   reference loop runs instead.
 */
#define T41_TMO_PAD 4U
#define T41_TMO_PW (T41_TMO_COLS + 2U * T41_TMO_PAD)
#define T41_TMO_PH (T41_TMO_ROWS + 2U * T41_TMO_PAD)
#define T41_TMO_KERNELS 11U

/* Spatial weight for offsets (ax, ay); the kernel table is indexed by the
 * sorted pair. */
static inline unsigned int t41_tmo_spatial_weight(unsigned int sk,
		unsigned int ax, unsigned int ay)
{
	unsigned int lo = ax < ay ? ax : ay, hi = ax < ay ? ay : ax;

	return t41_tmo_spatial_kernels[sk][hi * (hi + 1) / 2 + lo];
}

struct t41_tmo_map_scratch {
	unsigned int c[T41_TMO_PH][T41_TMO_PW][T41_TMO_BINS];
	unsigned int l[T41_TMO_PH][T41_TMO_PW][T41_TMO_BINS];
	unsigned char q[T41_TMO_PH][T41_TMO_PW][T41_TMO_BINS];
	unsigned int w[T41_TMO_KERNELS][T41_TMO_PAD + 1][T41_TMO_PAD + 1];
	unsigned short fq[T41_TMO_KERNELS][T41_TMO_PAD + 1][T41_TMO_PAD + 1][16];
};

static inline int t41_tmo_map_fast(const unsigned char *p, unsigned int bytes,
		unsigned int shift, const unsigned int *sums,
		const unsigned int *counts, const unsigned int *previous,
		int first_frame, unsigned int *out,
		struct t41_tmo_map_scratch *ws)
{
	unsigned int tile, bin, radius, mode, extra, a, ay, x, y, k;
	if (!p || bytes != T41_TMO_PARAM_BYTES || !sums || !counts ||
	    !previous || !out || out == sums || out == counts || !ws ||
	    shift > 9 || p[0x1742] > 4 || p[0x1739] > 1 || p[0x173c] > 1)
		return -1;
	radius = p[0x1742];
	mode = p[0x1739];
	extra = radius == 4 ? 2 : 0;
	if (mode == 1 && (p[0x173b] > 10 || p[0x173a] > 14))
		return -1;
	for (tile = 0; tile < T41_TMO_TILES; ++tile) {
		if (!mode && (p[0x230 + tile] > 10 || p[0x3a7 + tile] > 14))
			return -1;
	}
	for (tile = 0; tile < T41_TMO_SAMPLES; ++tile) {
		if (sums[tile] > 65535 || counts[tile] > 65535 ||
		    (!first_frame && previous[tile] > 4095))
			return -1;
	}
	/* Largest luma is ((65535 << 9) + 65535 * 7371) >> shift; times 255
	 * it stays below 2^32 for shift >= 5. */
	if (extra && shift < 5)
		return t41_tmo_map(p, bytes, shift, sums, counts, previous,
				   first_frame, out);
	for (k = 0; k < T41_TMO_KERNELS; ++k) {
		for (a = 0; a <= T41_TMO_PAD; ++a) {
			for (ay = 0; ay <= T41_TMO_PAD; ++ay) {
				unsigned int lo = a < ay ? a : ay, hi = a < ay ? ay : a;
				unsigned int w = t41_tmo_spatial_kernels[k][hi * (hi + 1) / 2 + lo];
				unsigned int r1, r2;

				ws->w[k][a][ay] = w;
				if (extra)
					for (r1 = 0; r1 < 4; ++r1)
						for (r2 = 0; r2 < 4; ++r2)
							ws->fq[k][a][ay][r1 * 4 + r2] =
								((w * r1) >> 2) + ((w * r2) >> 2);
			}
		}
	}
	memset(ws->c, 0, sizeof(ws->c));
	memset(ws->l, 0, sizeof(ws->l));
	memset(ws->q, 0, sizeof(ws->q));
	for (y = 0; y < T41_TMO_ROWS; ++y) {
		for (x = 0; x < T41_TMO_COLS; ++x) {
			unsigned int src = (y * T41_TMO_COLS + x) * T41_TMO_BINS;
			unsigned int *cd = ws->c[y + T41_TMO_PAD][x + T41_TMO_PAD];
			unsigned int *ld = ws->l[y + T41_TMO_PAD][x + T41_TMO_PAD];
			unsigned char *qd = ws->q[y + T41_TMO_PAD][x + T41_TMO_PAD];

			for (bin = 0; bin < T41_TMO_BINS; ++bin) {
				unsigned int count = counts[src + bin];
				unsigned int luma = ((sums[src + bin] << 9) +
						     count * (819U * bin)) >> shift;

				cd[bin] = count;
				if (extra) {
					ld[bin] = luma >> 2;
					qd[bin] = luma & 3;
				} else {
					ld[bin] = luma;
				}
			}
		}
	}
	for (tile = 0; tile < T41_TMO_TILES; ++tile) {
		unsigned int rk = mode ? p[0x173a] : p[0x3a7 + tile];
		unsigned int sk = mode ? p[0x173b] : p[0x230 + tile];
		unsigned int row = tile / T41_TMO_COLS, col = tile % T41_TMO_COLS;
		unsigned int n[T41_TMO_BINS] = { 0 }, sm[T41_TMO_BINS] = { 0 };
		int dy;

		for (dy = -(int)radius; dy <= (int)radius; ++dy) {
			unsigned int py = row + T41_TMO_PAD + dy;

			ay = dy < 0 ? -dy : dy;
			for (a = 0; a <= radius; ++a) {
				unsigned int w = ws->w[sk][a][ay];
				unsigned int pl = col + T41_TMO_PAD - a, pr = col + T41_TMO_PAD + a;
				const unsigned int *cl = ws->c[py][pl], *cr = ws->c[py][pr];
				const unsigned int *ll = ws->l[py][pl], *lr = ws->l[py][pr];

				if (!w)
					continue;
				if (!a) {
					for (bin = 0; bin < T41_TMO_BINS; ++bin) {
						n[bin] += w * cl[bin];
						sm[bin] += w * ll[bin];
					}
				} else {
					for (bin = 0; bin < T41_TMO_BINS; ++bin) {
						n[bin] += w * (cl[bin] + cr[bin]);
						sm[bin] += w * (ll[bin] + lr[bin]);
					}
				}
				if (extra) {
					const unsigned short *fq = ws->fq[sk][a][ay];
					const unsigned char *ql = ws->q[py][pl], *qr = ws->q[py][pr];

					if (!a)
						for (bin = 0; bin < T41_TMO_BINS; ++bin)
							sm[bin] += fq[ql[bin] * 4];
					else
						for (bin = 0; bin < T41_TMO_BINS; ++bin)
							sm[bin] += fq[ql[bin] * 4 + qr[bin]];
				}
			}
		}
		for (bin = 0; bin < T41_TMO_BINS; ++bin) {
			n[bin] >>= 8;
			sm[bin] >>= 10 - extra;
		}
		for (bin = 0; bin < T41_TMO_BINS; ++bin) {
			unsigned long long numerator = 0, denominator = 0;
			unsigned int value, divisor, index = bin * T41_TMO_TILES + tile;
			int kk;
			for (kk = -2; kk <= 2; ++kk) {
				int b = (int)bin + kk;
				unsigned int w = t41_tmo_range_kernels[rk][kk < 0 ? -kk : kk];
				if (b < 0 || b >= (int)T41_TMO_BINS)
					continue;
				numerator += (w * sm[b]) >> 1;
				denominator += w * n[b];
			}
			divisor = (unsigned int)(denominator >> (shift + 5));
			if (!divisor)
				divisor = 1;
			value = (((unsigned int)(numerator >> 2) / divisor) >> 1) & 32767;
			if (p[0x173c]) {
				unsigned int floor = (bin * 4095 + 5) / 10;
				if (value < floor)
					value = floor;
			}
			if (value > 4095)
				value = 4095;
			if (!first_frame)
				value = (previous[index] * 15 + value) >> 4;
			out[index] = value;
		}
	}
	return 0;
}

#endif
