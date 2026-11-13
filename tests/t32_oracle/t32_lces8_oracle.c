// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_lce_self_light_correct_local_simd_test of driver/t32/tx_isp_t32_lce.h against the unmodified
 * vendor lce_self_light_correct_local_simd_test (0x54bd0; Matt Davis' recovered C, PR #15 @ 2e2e06bc; its MXU2
 * statements are checked against the MXU2 model by tests/t32_oracle/mxu2).  Compared: all 160 output bytes, input,
 * the gain tables and the a2 structure unchanged, guard bytes behind out.  The flag byte (a2 + 8) is zero, 1 or 0x80
 * (the vendor tests the whole byte).  Inputs around, below and above the column offsets, gains 0..255.
 * Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

extern u64 lce_self_light_correct_local_simd_test(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

static u32 rs = 0x6a09e6u;
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
		u8 in[160], oo[160], st[16], up[20], down[20], *vi, *vo, *vs, *vu, *vd;
		u32 i, mode = rnd() % 4;

		for (i = 0; i < 160; i++) {
			u32 j = i % 20;

			switch (rnd() % 4) {
			case 0: in[i] = (u8)rnd(); break;
			case 1: in[i] = t32_lce_slc_ofs[j]; break;
			case 2: in[i] = (u8)(t32_lce_slc_ofs[j] + (int)(rnd() % 9) - 4); break;
			default: in[i] = (u8)(t32_lce_slc_ofs[j] + (int)(rnd() % 81) - 40);
			}
		}
		for (i = 0; i < 20; i++) {
			up[i] = (rnd() % 3) ? (u8)(100 + rnd() % 60) : (u8)rnd();
			down[i] = (rnd() % 3) ? (u8)(100 + rnd() % 60) : (u8)rnd();
		}
		memset(st, 0x77, sizeof(st));
		st[8] = mode == 0 ? 0 : mode == 1 ? 1 : mode == 2 ? 0x80 : (u8)rnd();
		memset(oo, 0x3c, sizeof(oo));
		t32_lce_self_light_correct_local_simd_test(in, oo, st[8], up, down);
		t32o_reset();
		vi = t32o_alloc(160); vo = t32o_alloc(176); vs = t32o_alloc(16); vu = t32o_alloc(20); vd = t32o_alloc(20);
		memcpy(vi, in, 160); memset(vo, 0xa5, 176); memcpy(vs, st, 16); memcpy(vu, up, 20); memcpy(vd, down, 20);
		t32o_call((t32o_fn)(void (*)(void))lce_self_light_correct_local_simd_test, T32O_PTR(vi), T32O_PTR(vo),
			  T32O_PTR(vs), T32O_PTR(vu), T32O_PTR(vd));
		compared++;
		if (memcmp(vi, in, 160) || memcmp(vs, st, 16) || memcmp(vu, up, 20) || memcmp(vd, down, 20) ||
		    memcmp(vo, oo, 160) || vo[160] != 0xa5 || vo[175] != 0xa5) {
			printf("MISMATCH case %u flag %u\n", c, st[8]);
			fail = 1;
		}
	}
	printf("t32_lces8_oracle: %u compared, %u mismatches\n", compared, fail ? 1u : 0u);
	return fail ? 1 : 0;
}
