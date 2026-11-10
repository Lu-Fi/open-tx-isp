// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our lce_std_hist_transform and lce_self_light_correct_local_test
 * (driver/t32/tx_isp_t32_lcehist.h, group 3) against Matt Davis' mechanical MIPS-to-C recovery
 * (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: the 32 histogram words (in place) plus canaries; the 20 output bytes plus canaries and the
 * unchanged inputs.  Both are void (v0/v1 at return are leftovers, not compared).  Inputs: bins zero / small /
 * random, mul / shift giving sums below, at and above 0x8000 and negative signed sums; curves at, above and
 * below the base table, gain tables, cfg enable bytes 0 / non-zero.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lcehist.h"

u64 lce_std_hist_transform(u32, u32, u32, u32);
u64 lce_self_light_correct_local_test(u32, u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

static u32 rs = 0x0badc0deu;
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
	static const u8 base[20] = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x50,
				     0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0 };

	for (c = 0; c < n; c++) {
		u32 h[36], *vh, mul, sh;
		u8 in[20], out[28], cfg[16], up[20], dn[20], *vi, *vo, *vc, *vu, *vd;

		for (i = 0; i < 36; i++)
			switch (c % 4) {
			case 0: h[i] = 0; break;
			case 1: h[i] = rnd() % 2048; break;
			case 2: h[i] = rnd() % 64; break;
			default: h[i] = rnd(); break;
			}
		mul = c % 3 ? rnd() % 256 : rnd();
		sh = c % 5 ? rnd() % 12 : rnd();
		if (c % 13 == 0) {	/* sum exactly 0x7fff / 0x8000 / 0x8001 */
			u32 s = 0;

			mul = 1;
			sh = 0;
			for (i = 0; i < 32; i++)
				s += h[i] = rnd() % 512;
			h[0] += 0x7fff + c % 3 - s;
			if ((int32_t)h[0] < 0)
				h[0] = 0x7fff + c % 3;
		}
		if (c % 13 == 1) {	/* signed-negative sum 0x80000000..0x80007fff: wrapped deficit >= 2^31 */
			u32 s = 0;

			mul = 1;
			sh = 0;
			for (i = 1; i < 32; i++)
				s += h[i] = rnd() % 512;
			h[0] = 0x80000000u + rnd() % 0x8000 - s;
		}
		t32o_reset();
		vh = t32o_alloc(sizeof(h));
		memcpy(vh, h, sizeof(h));
		t32o_call(F(lce_std_hist_transform), rnd(), T32O_PTR(vh), mul, sh, 0);
		t32_lce_std_hist_transform(h, mul, sh);
		cmp("std_hist_transform", c, memcmp(vh, h, sizeof(h)) != 0);

		for (i = 0; i < 20; i++) {
			in[i] = c % 3 == 0 ? base[i] + (u8)(rnd() % 3) - 1 : (u8)rnd();
			up[i] = (u8)rnd();
			dn[i] = (u8)rnd();
		}
		for (i = 0; i < sizeof(cfg); i++)
			cfg[i] = (u8)rnd();
		if (c % 4 == 0)
			cfg[8] = 0;
		if (c % 4 == 1)
			cfg[12] = 0;
		for (i = 0; i < sizeof(out); i++)
			out[i] = (u8)rnd();
		vi = t32o_alloc(20);
		vo = t32o_alloc(sizeof(out));
		vc = t32o_alloc(sizeof(cfg));
		vu = t32o_alloc(20);
		vd = t32o_alloc(20);
		memcpy(vi, in, 20);
		memcpy(vo, out, sizeof(out));
		memcpy(vc, cfg, sizeof(cfg));
		memcpy(vu, up, 20);
		memcpy(vd, dn, 20);
		t32o_call(F(lce_self_light_correct_local_test), T32O_PTR(vi), T32O_PTR(vo), T32O_PTR(vc), T32O_PTR(vu),
			  T32O_PTR(vd));
		t32_lce_self_light_correct_local_test(in, out, cfg, up, dn);
		cmp("self_light_correct_local_test", c, memcmp(vo, out, sizeof(out)) != 0 || memcmp(vi, in, 20) ||
		    memcmp(vc, cfg, sizeof(cfg)) || memcmp(vu, up, 20) || memcmp(vd, dn, 20));
	}
	printf("t32_lcest_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
