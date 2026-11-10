// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_lce_std_hist_transform_simd of driver/t32/tx_isp_t32_lce.h against the unmodified vendor
 * tisp_lce_std_hist_transform_simd (0x5401c; Matt Davis' recovered C, PR #15 @ 2e2e06bc; its MXU2 statements are
 * checked against the MXU2 model by tests/t32_oracle/mxu2).  Compared: all 32 bins, the excess word (pre-set to a
 * marker, so "not written" is compared too), guard bytes behind the histogram.  Multipliers / shifts cover the
 * normal range (sum near 0x8000, below, above) and the full 32 bit range (signed overflow of the product).
 * Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

extern u64 tisp_lce_std_hist_transform_simd(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

static u32 rs = 0x7be3a5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000, compared = 0;
	int fail = 0;

	for (c = 0; c < n && !fail; c++) {
		u16 h[32], oh[32], over_o = 0xbeef, *vh, *vo;
		u32 mult, shift, i;

		for (i = 0; i < 32; i++)
			h[i] = (rnd() % 4) ? (u16)(rnd() % 3000) : (u16)rnd();
		switch (rnd() % 4) {
		case 0: mult = rnd(); shift = rnd(); break;
		case 1: mult = rnd() % 64; shift = rnd() % 8; break;
		case 2: mult = 1 + rnd() % 1000; shift = 8 + rnd() % 8; break;
		default: mult = 0x8000 + rnd() % 0x20000; shift = 10 + rnd() % 10;
		}
		if (!(c % 3)) {	/* sum at or near 0x8000 with multiplier 1: exact, below (remainder loop) and above */
			int delta = (c % 9) ? (int)(rnd() % 81) - 40 : 0;
			u32 tot = 0;

			for (i = 0; i < 31; i++) {
				h[i] = (u16)(900 + rnd() % 250);
				tot += h[i];
			}
			h[31] = (u16)(0x8000 + delta - tot);
			mult = 1; shift = 0;
		}
		if (c % 7 == 1) {	/* signed sum near INT_MIN: 0x8000 - sum is >= 2^31 */
			for (i = 1; i < 32; i++)
				h[i] = 0;
			h[0] = 1; mult = 0x80000000u + rnd() % 0x8000; shift = 0;
		}
		memcpy(oh, h, 64);
		t32_lce_std_hist_transform_simd(oh, mult, shift, &over_o);
		t32o_reset();
		vh = t32o_alloc(80); vo = t32o_alloc(4);
		memset(vh, 0xa5, 80);
		memcpy(vh, h, 64);
		*vo = 0xbeef;
		t32o_call((t32o_fn)(void (*)(void))tisp_lce_std_hist_transform_simd, 0, T32O_PTR(vh), mult, shift, T32O_PTR(vo));
		compared++;
		if (memcmp(vh, oh, 64) || *vo != over_o || ((u8 *)vh)[64] != 0xa5 || ((u8 *)vh)[79] != 0xa5) {
			printf("MISMATCH mult %u shift %u over v%x o%x\n", mult, shift, *vo, over_o);
			fail = 1;
		}
	}
	printf("t32_lces3_oracle: %u compared, %u mismatches\n", compared, fail ? 1u : 0u);
	return fail ? 1 : 0;
}
