// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_lce_hist_filter_and_judge_16bit_simd of driver/t32/tx_isp_t32_lce.h against the unmodified
 * vendor lce_hist_filter_and_judge_16bit_simd (0x54318; Matt Davis' recovered C, PR #15 @ 2e2e06bc; its MXU2
 * statements are checked against the MXU2 model by tests/t32_oracle/mxu2).  Compared: all 8 output u16, hist and
 * idx unchanged, guard bytes behind out.  Rows of 16 hist rows, empty, single bin, sparse and dense; idx repeats.
 * Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

extern u64 lce_hist_filter_and_judge_16bit_simd(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

static u32 rs = 0x1d2c3bu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000, compared = 0;
	int fail = 0;

	for (c = 0; c < n && !fail; c++) {
		u16 hist[16 * 32], idx[8], oo[8], *vh, *vi, *vo;
		u32 i, r;

		for (r = 0; r < 16; r++) {
			u32 kind = rnd() % 5, first = rnd() % 32;

			for (i = 0; i < 32; i++) {
				switch (kind) {
				case 0: hist[r * 32 + i] = 0; break;
				case 1: hist[r * 32 + i] = i == first ? (rnd() % 3 ? (u16)(1 + rnd() % 65535) : 1) : 0; break;
				case 2: hist[r * 32 + i] = i >= first ? (u16)rnd() : 0; break;
				case 3: hist[r * 32 + i] = (rnd() % 6) ? 0 : (u16)rnd(); break;
				default: hist[r * 32 + i] = i >= first ? 0xffff : 0;
				}
			}
		}
		for (i = 0; i < 8; i++)
			idx[i] = (u16)(rnd() % 16);
		memset(oo, 0x3c, sizeof(oo));
		t32_lce_hist_filter_and_judge_16bit_simd(hist, idx, oo);
		t32o_reset();
		vh = t32o_alloc(sizeof(hist));
		vi = t32o_alloc(16);
		vo = t32o_alloc(32);
		memcpy(vh, hist, sizeof(hist));
		memcpy(vi, idx, 16);
		memset(vo, 0xa5, 32);
		t32o_call((t32o_fn)(void (*)(void))lce_hist_filter_and_judge_16bit_simd, T32O_PTR(vh), 0, 0, T32O_PTR(vo),
			  T32O_PTR(vi));
		compared++;
		if (memcmp(vh, hist, sizeof(hist)) || memcmp(vi, idx, 16) || memcmp(vo, oo, 16) ||
		    ((u8 *)vo)[16] != 0xa5 || ((u8 *)vo)[31] != 0xa5) {
			printf("MISMATCH case %u\n", c);
			fail = 1;
		}
	}
	printf("t32_lces7_oracle: %u compared, %u mismatches\n", compared, fail ? 1u : 0u);
	return fail ? 1 : 0;
}
