// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_lce_thres_search_simd of driver/t32/tx_isp_t32_lce.h against the unmodified vendor
 * tisp_lce_thres_search_simd_v4 (0x4d988; Matt Davis' recovered C, PR #15 @ 2e2e06bc; its MXU2 statements are
 * checked against the MXU2 model by tests/t32_oracle/mxu2).  Compared: the three 16 byte output arrays, the
 * 1024 byte histogram block and the threshold block stay unchanged, and guard bytes behind each output.
 * Histograms are random, sparse, huge (saturating sums) and cumulative-like; thresholds random incl. 0 and 0xffff.
 * Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

extern u64 tisp_lce_thres_search_simd_v4(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

static u32 rs = 0x3d9a71u;
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
		u16 h[16][32], thr[8], *vh, *vt;
		u8 o[3][16], *vo[3];
		unsigned int k, i, mode = rnd() % 4;

		for (k = 0; k < 16; k++)
			for (i = 0; i < 32; i++) {
				switch (mode) {
				case 0: h[k][i] = (u16)rnd(); break;
				case 1: h[k][i] = (rnd() % 4) ? 0 : (u16)(rnd() % 400); break;
				case 2: h[k][i] = (u16)(rnd() % 3000); break;
				default: h[k][i] = (rnd() % 8) ? (u16)(rnd() % 20) : (u16)(0xf000 | rnd());
				}
			}
		for (i = 0; i < 8; i++)
			thr[i] = (u16)rnd();
		for (i = 0; i < 3; i++)
			switch (rnd() % 6) {
			case 0: thr[i] = 0; break;
			case 1: thr[i] = 0xffff; break;
			case 2: thr[i] = (u16)(rnd() % 100); break;
			case 3: thr[i] = (u16)(rnd() % 20000); break;
			default: break;
			}
		t32_lce_thres_search_simd(h, thr, o[0], o[1], o[2]);
		t32o_reset();
		vh = t32o_alloc(1024); vt = t32o_alloc(16);
		for (i = 0; i < 3; i++) {
			vo[i] = t32o_alloc(32);
			memset(vo[i], 0xa5, 32);
		}
		memcpy(vh, h, 1024); memcpy(vt, thr, 16);
		t32o_call((t32o_fn)(void (*)(void))tisp_lce_thres_search_simd_v4, T32O_PTR(vh), T32O_PTR(vt),
			  T32O_PTR(vo[0]), T32O_PTR(vo[1]), T32O_PTR(vo[2]));
		compared++;
		if (memcmp(vh, h, 1024) || memcmp(vt, thr, 16)) {
			printf("MISMATCH vendor changed its input\n");
			fail = 1;
		}
		for (i = 0; i < 3 && !fail; i++) {
			if (memcmp(vo[i], o[i], 16) || vo[i][16] != 0xa5 || vo[i][31] != 0xa5) {
				printf("MISMATCH out%u mode %u thr %u %u %u\n", i, mode, thr[0], thr[1], thr[2]);
				for (k = 0; k < 16; k++)
					printf("%u o%u v%u\n", k, o[i][k], vo[i][k]);
				fail = 1;
			}
		}
	}
	printf("t32_lces2_oracle: %u compared, %u mismatches\n", compared, fail ? 1u : 0u);
	return fail ? 1 : 0;
}
