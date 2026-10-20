/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T41_TMO_MXU3_H
#define TX_ISP_T41_TMO_MXU3_H

/*
 * Local TMO map with the spatial filter on MXU3 (tx_isp_t41_tmo_mxu3.S).
 *
 * Same arithmetic as the C reference t41_tmo_map(): the vector kernel
 * computes, 16 tiles of one tile row at a time, every (dy, dx) term with
 * 32-bit wrap-around (MULW keeps the low word, SRLIW/ADDW as in C). Inputs
 * are laid out as zero-padded planes per bin; per-lane weights cover both
 * kernel modes (mode 0 has one kernel per tile). Terms whose 16 weights are
 * all zero are dropped. The range filter, clamps and temporal blend stay
 * in C, unchanged.
 *
 * Kernel only: needs the XBurst2 MXU3 unit and the
 * kernel_mxuv3_begin_partial()/kernel_mxuv3_end() bracket.
 */
#include "tx_isp_t41_tmo_map.h"

#define T41_TMO_MXU3_LANES	16U
#define T41_TMO_MXU3_CHUNKS	(T41_TMO_ROWS * 2U)	/* x0 = 0 and 16 */
#define T41_TMO_MXU3_TERMS	((2U * T41_TMO_PAD + 1U) * (2U * T41_TMO_PAD + 1U))
#define T41_TMO_MXU3_PLANE	(T41_TMO_PH * T41_TMO_PW + 32U)	/* lanes past col 24 */

struct t41_tmo_mxu3_job {
	const unsigned int *c;
	const unsigned int *l;
	const int *offset;
	const unsigned int *weight;
	unsigned int terms;
	unsigned int extra;
	unsigned int *out;
};

void t41_tmo_mxu3_chunk(const struct t41_tmo_mxu3_job *job);
void kernel_mxuv3_begin_partial(unsigned int num_regs);
void kernel_mxuv3_end(void);

struct t41_tmo_mxu3_scratch {
	unsigned int weight[T41_TMO_MXU3_CHUNKS][T41_TMO_MXU3_TERMS][T41_TMO_MXU3_LANES]
		__attribute__((aligned(64)));
	unsigned int out[2 * T41_TMO_MXU3_LANES] __attribute__((aligned(64)));
	unsigned int c[T41_TMO_BINS][T41_TMO_MXU3_PLANE];
	unsigned int l[T41_TMO_BINS][T41_TMO_MXU3_PLANE];
	int offset[T41_TMO_MXU3_CHUNKS][T41_TMO_MXU3_TERMS];
	unsigned int terms[T41_TMO_MXU3_CHUNKS];
	unsigned int n[T41_TMO_BINS][T41_TMO_TILES];
	unsigned int s[T41_TMO_BINS][T41_TMO_TILES];
	/* weight/offset cache key: mode, radius, mode-1 kernel, per-tile kernels */
	unsigned char key[3 + T41_TMO_TILES];
	int key_valid;
};

/* Rebuild the per-chunk term lists when the kernel selection changed. */
static inline void t41_tmo_mxu3_terms(const unsigned char *p,
				      struct t41_tmo_mxu3_scratch *ws)
{
	unsigned char key[sizeof(ws->key)];
	unsigned int radius = p[0x1742], mode = p[0x1739], chunk;

	key[0] = mode;
	key[1] = radius;
	key[2] = p[0x173b];
	memcpy(key + 3, p + 0x230, T41_TMO_TILES);
	if (ws->key_valid && !memcmp(key, ws->key, sizeof(key)))
		return;
	for (chunk = 0; chunk < T41_TMO_MXU3_CHUNKS; ++chunk) {
		unsigned int row = chunk / 2, x0 = (chunk & 1) * T41_TMO_MXU3_LANES;
		unsigned int terms = 0;
		int dy, dx;

		for (dy = -(int)radius; dy <= (int)radius; ++dy) {
			for (dx = -(int)radius; dx <= (int)radius; ++dx) {
				unsigned int ax = dx < 0 ? -dx : dx;
				unsigned int ay = dy < 0 ? -dy : dy;
				unsigned int lane, any = 0;
				unsigned int *w = ws->weight[chunk][terms];

				for (lane = 0; lane < T41_TMO_MXU3_LANES; ++lane) {
					unsigned int col = x0 + lane, sk;

					w[lane] = 0;
					if (col >= T41_TMO_COLS)
						continue;
					sk = mode ? p[0x173b] : p[0x230 + row * T41_TMO_COLS + col];
					w[lane] = t41_tmo_spatial_weight(sk, ax, ay);
					any |= w[lane];
				}
				if (!any)
					continue;
				ws->offset[chunk][terms] =
					(dy * (int)T41_TMO_PW + dx) * (int)sizeof(unsigned int);
				++terms;
			}
		}
		ws->terms[chunk] = terms;
	}
	memcpy(ws->key, key, sizeof(key));
	ws->key_valid = 1;
}

static inline int t41_tmo_map_mxu3(const unsigned char *p, unsigned int bytes,
		unsigned int shift, const unsigned int *sums,
		const unsigned int *counts, const unsigned int *previous,
		int first_frame, unsigned int *out,
		struct t41_tmo_mxu3_scratch *ws)
{
	unsigned int tile, bin, radius, mode, extra, x, y, chunk;
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
	t41_tmo_mxu3_terms(p, ws);
	memset(ws->c, 0, sizeof(ws->c));
	memset(ws->l, 0, sizeof(ws->l));
	for (y = 0; y < T41_TMO_ROWS; ++y) {
		for (x = 0; x < T41_TMO_COLS; ++x) {
			unsigned int src = (y * T41_TMO_COLS + x) * T41_TMO_BINS;
			unsigned int dst = (y + T41_TMO_PAD) * T41_TMO_PW + x + T41_TMO_PAD;

			for (bin = 0; bin < T41_TMO_BINS; ++bin) {
				unsigned int count = counts[src + bin];

				ws->c[bin][dst] = count;
				ws->l[bin][dst] = ((sums[src + bin] << 9) +
						   count * (819U * bin)) >> shift;
			}
		}
	}
	kernel_mxuv3_begin_partial(32);
	for (bin = 0; bin < T41_TMO_BINS; ++bin) {
		for (chunk = 0; chunk < T41_TMO_MXU3_CHUNKS; ++chunk) {
			unsigned int row = chunk / 2, x0 = (chunk & 1) * T41_TMO_MXU3_LANES;
			unsigned int base = (row + T41_TMO_PAD) * T41_TMO_PW + x0 + T41_TMO_PAD;
			struct t41_tmo_mxu3_job job = {
				.c = ws->c[bin] + base,
				.l = ws->l[bin] + base,
				.offset = ws->offset[chunk],
				.weight = &ws->weight[chunk][0][0],
				.terms = ws->terms[chunk],
				.extra = extra,
				.out = ws->out,
			};
			unsigned int lane;

			t41_tmo_mxu3_chunk(&job);
			for (lane = 0; lane < T41_TMO_MXU3_LANES; ++lane) {
				unsigned int col = x0 + lane;

				if (col >= T41_TMO_COLS)
					break;
				tile = row * T41_TMO_COLS + col;
				ws->n[bin][tile] = ws->out[lane] >> 8;
				ws->s[bin][tile] = ws->out[T41_TMO_MXU3_LANES + lane] >> (10 - extra);
			}
		}
	}
	kernel_mxuv3_end();
	for (tile = 0; tile < T41_TMO_TILES; ++tile) {
		unsigned int rk = mode ? p[0x173a] : p[0x3a7 + tile];

		for (bin = 0; bin < T41_TMO_BINS; ++bin) {
			unsigned long long numerator = 0, denominator = 0;
			unsigned int value, divisor, index = bin * T41_TMO_TILES + tile;
			int kk;
			for (kk = -2; kk <= 2; ++kk) {
				int b = (int)bin + kk;
				unsigned int w = t41_tmo_range_kernels[rk][kk < 0 ? -kk : kk];
				if (b < 0 || b >= (int)T41_TMO_BINS)
					continue;
				numerator += (w * ws->s[b][tile]) >> 1;
				denominator += w * ws->n[b][tile];
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
