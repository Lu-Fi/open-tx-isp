// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our lce_compress_data and lce_16bit_data_converge_16bit (driver/t32/tx_isp_t32_lcehist.h,
 * group 2) against Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc; lce_compress_data
 * tail-jumps to the lce_compress_data_part_1 reference in mode 1).
 *
 * Compared bit-exact: lce_compress_data return value (low word) and its output incl. canaries; for the
 * converge, the 20 target bytes updated in place plus canaries and the unchanged current curve (its v0/v1 at
 * return are leftovers of a void function, not compared).  Inputs: mode 0 / 1 / 2 / random, full-range and
 * small bins; gain, lo, hi as random signed bytes with high garbage, small positive values, targets near the
 * current points (inside and just outside +-lo).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lcehist.h"

u64 lce_compress_data(u32, u32, u32, u32);
u64 lce_16bit_data_converge_16bit(u32, u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define OUTB	160

static u32 rs = 0x7a3c9e11u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

static void cmp(const char *what, unsigned int c, unsigned int bad)
{
	compared++;
	if (bad && failed++ < 10)
		printf("MISMATCH %s case %u\n", what, c);
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;

	for (c = 0; c < n; c++) {
		u32 src[32], mode = c % 4 == 3 ? rnd() : c % 4, r, o, g, lo, hi;
		u8 *vin, *vout, mine[OUTB], cur[20], tgt[20 + 8], *vcur, *vtgt;

		for (i = 0; i < 32; i++)
			src[i] = c % 3 ? rnd() : rnd() % 4096;
		t32o_reset();
		vin = t32o_alloc(sizeof(src));
		vout = t32o_alloc(OUTB);
		memcpy(vin, src, sizeof(src));
		for (i = 0; i < OUTB; i++)
			vout[i] = mine[i] = (u8)(c * 3 + i);
		r = (u32)t32o_call(F(lce_compress_data), mode, T32O_PTR(vin), T32O_PTR(vout), 0, 0);
		o = t32_lce_compress_data(mode, src, mine);
		cmp("compress_data", c, r != o || memcmp(vout, mine, OUTB) != 0 || memcmp(vin, src, sizeof(src)) != 0);

		g = rnd();
		lo = rnd();
		hi = rnd();
		if (c % 3 == 0) {
			g = rnd() % 128;
			lo = rnd() % 16;
			hi = lo + rnd() % 64;
		}
		if (c % 5 == 0)
			g |= rnd() & 0xffffff00u;
		for (i = 0; i < 20; i++) {
			cur[i] = (u8)rnd();
			tgt[i] = c % 2 ? (u8)rnd() : (u8)(cur[i] + (int)(rnd() % 41) - 20);
		}
		for (i = 20; i < sizeof(tgt); i++)
			tgt[i] = (u8)rnd();
		vcur = t32o_alloc(20);
		vtgt = t32o_alloc(sizeof(tgt));
		memcpy(vcur, cur, 20);
		memcpy(vtgt, tgt, sizeof(tgt));
		t32o_call(F(lce_16bit_data_converge_16bit), g, lo, hi, T32O_PTR(vcur), T32O_PTR(vtgt));
		t32_lce_converge_16bit(g, lo, hi, cur, tgt);
		cmp("converge_16bit", c, memcmp(vtgt, tgt, sizeof(tgt)) != 0 || memcmp(vcur, cur, 20) != 0);
	}
	printf("t32_lcecv_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
