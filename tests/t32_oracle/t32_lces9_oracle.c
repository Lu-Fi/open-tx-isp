// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_lce_hist_method_16bit_simd of driver/t32/tx_isp_t32_lce.h against the unmodified vendor
 * lce_hist_method_16bit_simd (0x54438; Matt Davis' recovered C, PR #15 @ 2e2e06bc; its MXU2 statements are
 * checked against the MXU2 model by tests/t32_oracle/mxu2; memset / memcpy callees run as the host libc).
 * Compared: all 32 output u16, hist and the parameter block unchanged, guard bytes behind out.  Parameters
 * cover both branches (sum above and below 0x8000), centre bins 0..31 (bin 32 and above overflow the vendor's
 * stack arrays and are not ported), gains around 128 and over the full byte range, parameter words with garbage
 * in the unused upper bits, an empty b ramp (division by zero case) and full-range histograms.
 * Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

extern u64 lce_hist_method_16bit_simd(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x4f1bbcu;
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
		struct t32_lce_hm16_par par;
		u16 hist[32], oo[32], *vh, *vo;
		struct t32_lce_hm16_par *vp;
		u32 i, junk = (c & 1) ? rnd() & 0xffff0000u : 0, junk8 = (c & 1) ? rnd() & 0xff00u : 0;

		memset(&par, 0x5a, sizeof(par));
		par.n = (rnd() % 8) ? rnd() % 32 : 31 * (rnd() & 1);
		par.n |= junk8;
		par.k8 = (rnd() % 3) ? 88 + rnd() % 80 : rnd() & 0xff;
		par.k8 |= junk;
		par.c1 = (rnd() % 4) ? rnd() % 2500 : rnd() % 65536;
		par.a = (rnd() % 4) ? rnd() % 4000 : rnd() % 65536;
		par.e1 = (rnd() % 4) ? 1000 + rnd() % 2500 : rnd() % 65536;
		par.c3 = (rnd() % 4) ? rnd() % 40 : rnd() & 0xff;
		par.b = (rnd() % 4) ? rnd() % 100 : rnd() & 0xff;
		par.e2 = (rnd() % 4) ? rnd() % 400 : rnd() % 65536;
		par.sh = (rnd() % 3) ? rnd() % 6 : rnd() & 0xff;
		if (!(c % 17)) { par.c3 = 0; par.b = 0; par.e2 = 0; par.k8 = (c & 1) ? junk | 20 : 20; par.c1 = 100; }
		if (!(c % 19)) par.b |= junk;
		par.c1 |= (c % 17) ? junk : 0; par.a |= junk; par.e1 |= junk; par.e2 |= junk; par.c3 |= junk8;
		for (i = 0; i < 32; i++) {
			switch (rnd() % 6) {
			case 0: hist[i] = (u16)rnd(); break;
			case 1: hist[i] = 0; break;
			default: hist[i] = (u16)(700 + rnd() % 700);
			}
		}
		memset(oo, 0x3c, sizeof(oo));
		t32_lce_hist_method_16bit_simd(hist, oo, &par);
		t32o_reset();
		vh = t32o_alloc(64);
		vo = t32o_alloc(80);
		vp = t32o_alloc(64);
		memcpy(vh, hist, 64);
		memset(vo, 0xa5, 80);
		memcpy(vp, &par, sizeof(par));
		t32o_call((t32o_fn)(void (*)(void))lce_hist_method_16bit_simd, T32O_PTR(vh), T32O_PTR(vo), T32O_PTR(vp), 0, 0);
		compared++;
		if (memcmp(vh, hist, 64) || memcmp(vp, &par, sizeof(par)) || memcmp(vo, oo, 64) ||
		    ((u8 *)vo)[64] != 0xa5 || ((u8 *)vo)[79] != 0xa5) {
			printf("MISMATCH case %u n %u\n", c, par.n & 0xff);
			for (i = 0; i < 32; i++)
				if (oo[i] != vo[i]) { printf("bin %u port %u vendor %u\n", i, oo[i], vo[i]); break; }
			fail = 1;
		}
	}
	/* BEYOND VENDOR (#281): n 32..255 (vendor: stack overflow) must give the n = 31 result; ours only */
	for (c = 0; c < 2000 && !fail; c++) {
		struct t32_lce_hm16_par par, p31;
		u16 hist[32], o1[32], o2[32];
		u32 i;

		for (i = 0; i < 32; i++)
			hist[i] = (u16)(rnd() % 3 ? 700 + rnd() % 700 : rnd());
		memset(&par, 0, sizeof(par));
		par.n = (rnd() & 0xff00u) | (32 + rnd() % 224);
		par.k8 = 88 + rnd() % 80;
		par.c1 = rnd() % 2500; par.a = rnd() % 4000; par.e1 = 1000 + rnd() % 2500;
		par.c3 = rnd() % 40; par.b = rnd() % 100; par.e2 = rnd() % 400; par.sh = rnd() % 6;
		p31 = par;
		p31.n = (par.n & ~0xffu) | 31;
		t32_lce_hist_method_16bit_simd(hist, o1, &par);
		t32_lce_hist_method_16bit_simd(hist, o2, &p31);
		if (memcmp(o1, o2, sizeof(o1))) {
			printf("MISMATCH n clamp (beyond vendor) case %u n %u\n", c, par.n & 0xff);
			fail = 1;
		}
	}
	printf("t32_lces9_oracle: %u compared, 2000 n >= 32 clamp checks (beyond vendor), %u mismatches\n", compared,
	       fail ? 1u : 0u);
	return fail ? 1 : 0;
}
