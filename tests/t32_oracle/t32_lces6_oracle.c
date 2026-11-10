// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_lce_pdf_to_cdf_16bit_simd of driver/t32/tx_isp_t32_lce.h against the unmodified vendor
 * lce_pdf_to_cdf_16bit_simd (0x54984; Matt Davis' recovered C, PR #15 @ 2e2e06bc; its MXU2 statements are
 * checked against the MXU2 model by tests/t32_oracle/mxu2).  Compared: all 160 output bytes, the pdf stays
 * unchanged, guard bytes behind pdf and out.  Bins from empty to full scale, so the sum stays below 32768, crosses it
 * (clamp) and runs far past it (two bin steps, 32 bit sum).
 * Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

extern u64 lce_pdf_to_cdf_16bit_simd(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x3c6ef3u;
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
		u16 pdf[256], *vp;
		u8 oo[160], *vo;
		u32 i, scale;

		switch (rnd() % 5) {
		case 0: scale = 0xffff; break;
		case 1: scale = 40; break;
		case 2: scale = 600; break;
		case 3: scale = 2000; break;
		default: scale = 1 + rnd() % 0x10000;
		}
		for (i = 0; i < 256; i++)
			pdf[i] = (rnd() % 7) ? (u16)(rnd() % (scale + 1)) : 0;
		if (!(c % 11))
			for (i = 0; i < 256; i++)
				pdf[i] = 0;
		if (!(c % 13))
			for (i = 0; i < 256; i++)
				pdf[i] = 0xffff;
		memset(oo, 0x3c, sizeof(oo));
		t32_lce_pdf_to_cdf_16bit_simd(pdf, oo);
		t32o_reset();
		vp = t32o_alloc(512 + 16);
		vo = t32o_alloc(160 + 16);
		memset(vp, 0xa5, 528);
		memset(vo, 0xa5, 176);
		memcpy(vp, pdf, 512);
		t32o_call((t32o_fn)(void (*)(void))lce_pdf_to_cdf_16bit_simd, T32O_PTR(vp), T32O_PTR(vo), 0, 0, 0);
		compared++;
		if (memcmp(vp, pdf, 512) || ((u8 *)vp)[512] != 0xa5 || ((u8 *)vp)[527] != 0xa5 ||
		    memcmp(vo, oo, 160) || vo[160] != 0xa5 || vo[175] != 0xa5) {
			printf("MISMATCH scale %u\n", scale);
			fail = 1;
		}
	}
	printf("t32_lces6_oracle: %u compared, %u mismatches\n", compared, fail ? 1u : 0u);
	return fail ? 1 : 0;
}
