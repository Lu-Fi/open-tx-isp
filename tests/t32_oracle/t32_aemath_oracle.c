// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_aemath.h (fix_point_div_32 / mult2_32 /
 * mult3_32 / mult2_64 / mult3_64, tisp_math_exp2) against the vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Random operands
 * (full range and small), shifts 0..40 (exp2: in / out fraction bits
 * 0..40, sometimes 0..255), every case compared; the loop runs on the low
 * stack.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_aemath.h"

u64 fix_point_div_32(u32, u32, u32, u32), fix_point_mult2_32(u32, u32, u32, u32), fix_point_mult3_32(u32, u32, u32, u32);
u64 fix_point_mult2_64(u32, u32, u32, u32, u32, u32), fix_point_mult3_64(u32, u32, u32, u32, u32, u32, u32, u32);
u64 tisp_math_exp2(u32, u32, u32, u32);

static u32 rs = 0x243f6a88u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 opnd(void)
{
	switch (rnd() & 3) {
	case 0: return rnd();
	case 1: return rnd() & 0xffff;
	case 2: return rnd() & 0xfff;
	default: return rnd() >> (rnd() & 31);
	}
}

#define N 400000
static unsigned long fails, per[6];
static u64 loop(u32 a0, u32 a1, u32 a2, u32 a3)
{
	unsigned long n;

	(void)a0; (void)a1; (void)a2; (void)a3;
	for (n = 0; n < N && !fails; n++) {
		int f = (int)(n % 6);
		u32 sh = (rnd() & 7) ? rnd() % 24 : rnd() % 41, a = opnd(), b = opnd(), c = opnd();
		unsigned long long A = ((unsigned long long)opnd() << (rnd() & 31)) | opnd(), B = opnd(), C = opnd();
		unsigned long long v = 0, o = 0;

		switch (f) {
		case 0:
			if (!b)
				b = 1;	/* fix_point_div_32 by 0 is a vendor divide by zero */
			v = (u32)fix_point_div_32(sh, a, b, 0); o = t32_aem_div32(sh, a, b);
			break;
		case 1:
			v = (u32)fix_point_mult2_32(sh, a, b, 0); o = t32_aem_mult2_32(sh, a, b);
			break;
		case 2:
			v = (u32)fix_point_mult3_32(sh, a, b, c); o = t32_aem_mult3_32(sh, a, b, c);
			break;
		case 3:
			v = fix_point_mult2_64(sh, 0, (u32)A, (u32)(A >> 32), (u32)B, (u32)(B >> 32));
			o = t32_aem_mult2_64(sh, A, B);
			break;
		case 4:
			v = fix_point_mult3_64(sh, 0, (u32)A, (u32)(A >> 32), (u32)B, (u32)(B >> 32), (u32)C, (u32)(C >> 32));
			o = t32_aem_mult3_64(sh, A, B, C);
			break;
		default: {
			u32 iq = (rnd() & 7) ? rnd() % 41 : rnd() & 0xff, oq = (rnd() & 3) ? rnd() % 31 : rnd() & 0xff;

			a = (rnd() & 1) ? rnd() : rnd() & ((2U << (iq % 31)) - 1);
			v = (u32)tisp_math_exp2(a, iq, oq, 0); o = t32_aem_exp2(a, iq, oq);
			sh = iq; b = oq;
		}
		}
		if (v != o) {
			printf("t32_aemath_oracle: FAIL fn %d sh %u a %#x b %#x c %#x A %#llx: vendor %#llx ours %#llx\n",
			       f, sh, a, b, c, A, v, o);
			fails++;
		}
		per[f]++;
	}
	return 0;
}

int main(void)
{
	t32o_reset();
	t32o_call((t32o_fn)(void (*)(void))loop, 0, 0, 0, 0, 0);
	if (fails)
		return 1;
	printf("t32_aemath_oracle: PASS %d cases (div32 %lu, mult2_32 %lu, mult3_32 %lu, mult2_64 %lu, mult3_64 %lu, exp2 %lu)\n",
	       N, per[0], per[1], per[2], per[3], per[4], per[5]);
	return 0;
}
