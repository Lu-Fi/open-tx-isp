// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the five scalar LCE leaves of driver/t32/tx_isp_t32_lce41.h against the unmodified
 * vendor lce_pdf_to_cdf_16bit (0x53bb8), lce_self_light_correct_local_test (0x53d54),
 * lce_16bit_data_converge_16bit (0x53dd8), lce_std_hist_transform (0x531c4) and lce_hist_method_16bit
 * (0x53324) (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Compared: every output byte plus 32 canary bytes behind it, the inputs stay unchanged.  Inputs: random
 * full range values, small / large / saturating sums, both enable bytes, every sign of gain / lo / hi,
 * all centre bins 0..31 with zero ramp sums, under- and over-full histograms.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce41.h"

u64 lce_pdf_to_cdf_16bit(u32, u32, u32, u32);
u64 lce_self_light_correct_local_test(u32, u32, u32, u32, u32);
u64 lce_16bit_data_converge_16bit(u32, u32, u32, u32, u32);
u64 lce_std_hist_transform(u32, u32, u32, u32);
u64 lce_hist_method_16bit(u32, u32, u32, u32);

#define CAN 32

static u32 rs = 0x1ce41a5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void fill(u8 *a, u8 *b, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		a[i] = b[i] = (u8)rnd();
}

static unsigned int failed;
static void chk(const char *w, unsigned int c, const void *v, const void *o, u32 n)
{
	if (memcmp(v, o, n) && failed++ < 8)
		printf("MISMATCH %s case %u\n", w, c);
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	unsigned int nover = 0, nunder = 0, nz = 0;

	for (c = 0; c < n; c++) {
		u8 *v, o[512], ref[512];
		u32 m = rnd() % 4;

		/* pdf_to_cdf_16bit: pdf 64 B (+ canary), out 20 B */
		t32o_reset();
		v = t32o_alloc(512);
		fill(v, o, 512);
		for (i = 0; i < 32; i++) {
			u16 x = (u16)(m == 0 ? rnd() % 64 : m == 1 ? rnd() % 4096 : rnd());
			memcpy(v + 2 * i, &x, 2); memcpy(o + 2 * i, &x, 2);
		}
		t32o_call((t32o_fn)(void (*)(void))lce_pdf_to_cdf_16bit, T32O_PTR(v), T32O_PTR(v + 128), 0, 0, 0);
		t32_lce_pdf_to_cdf_16bit((const u16 *)o, o + 128);
		chk("pdf_to_cdf_16bit", c, v, o, 512);

		/* self_light_correct_local_test: in 20 B, out 20 B, en (+8 / +12), up, down */
		fill(v, o, 512);
		v[200 + 8] = o[200 + 8] = (u8)(rnd() & 1 ? 0 : rnd());
		v[200 + 12] = o[200 + 12] = (u8)(rnd() & 1 ? 0 : rnd());
		if (m == 0)
			for (i = 0; i < 20; i++)
				v[i] = o[i] = t32_lce_slc_ofs[i];
		t32o_call((t32o_fn)(void (*)(void))lce_self_light_correct_local_test, T32O_PTR(v), T32O_PTR(v + 64),
			  T32O_PTR(v + 200), T32O_PTR(v + 300), T32O_PTR(v + 400));
		t32_lce_self_light_correct_local_test(o, o + 64, o + 200, o + 300, o + 400);
		chk("self_light_correct_local_test", c, v, o, 512);

		/* 16bit_data_converge_16bit: target 20 B, cur 20 B */
		fill(v, o, 512);
		{
			u32 g = rnd(), l = m == 0 ? rnd() % 8 : rnd(), h = m == 1 ? rnd() % 16 : rnd();

			if (m == 2)
				for (i = 0; i < 20; i++)
					v[64 + i] = o[64 + i] = (u8)(v[i] + (int)(rnd() % 40) - 20);
			t32o_call((t32o_fn)(void (*)(void))lce_16bit_data_converge_16bit, g, l, h, T32O_PTR(v),
				  T32O_PTR(v + 64));
			t32_lce_16bit_data_converge_16bit(g, l, h, o, o + 64);
			chk("16bit_data_converge_16bit", c, v, o, 512);
		}

		/* std_hist_transform: 32 u32 bins */
		fill(v, o, 512);
		{
			u32 mult = m == 3 ? rnd() : rnd() % 4096, sh = rnd() % 3 ? 8 + rnd() % 8 : rnd();

			for (i = 0; i < 32; i++) {
				u32 x = m == 0 ? rnd() % 2048 : m == 1 ? rnd() % 65536 : rnd();
				memcpy(v + 4 * i, &x, 4); memcpy(o + 4 * i, &x, 4);
			}
			if (rnd() % 4 == 0) {	/* sum in 0x80000000..0x80008000: the gap is negative as s32 */
				u32 tsum = 0x80000000u + rnd() % 0x8001, x;

				mult = 1; sh = 0;
				for (i = 0; i < 31; i++) {
					memcpy(&x, v + 4 * i, 4);
					tsum -= x;
				}
				memcpy(v + 124, &tsum, 4); memcpy(o + 124, &tsum, 4);
			}
			t32o_call((t32o_fn)(void (*)(void))lce_std_hist_transform, rnd(), T32O_PTR(v), mult, sh, 0);
			t32_lce_std_hist_transform32((u32 *)o, mult, sh);
			chk("std_hist_transform", c, v, o, 512);
		}

		/* hist_method_16bit: hist 32 u16, out 32 u16, parameter block 13 words */
		fill(v, o, 512);
		{
			u32 w[13], k8 = rnd() % 3 ? rnd() % 256 : 128;

			for (i = 0; i < 13; i++)
				w[i] = rnd();
			w[1] = (rnd() & ~0xffu) | (rnd() % 32);
			if (m == 0) {	/* zero b ramp: sum(b) = 0 */
				w[5] &= ~0xffu; w[6] &= ~0xffu; w[11] &= ~0xffffu;
				nz++;
			}
			if (rnd() % 3 == 0)
				w[9] &= 0xff;
			if (rnd() & 1)
				w[12] = (rnd() & ~31u) | (rnd() % 4);
			w[7] = (w[7] & ~0xffu) | k8;
			for (i = 0; i < 32; i++) {
				u16 x = (u16)(m == 1 ? 1024 + rnd() % 2048 : m == 2 ? rnd() % 1500 : rnd());
				memcpy(v + 2 * i, &x, 2); memcpy(o + 2 * i, &x, 2);
			}
			if (rnd() % 8 == 0) {	/* sum of min(a, g) exactly 0x8000: k8 = 128, a ramp above every bin */
				u32 t = 0x8000;

				w[7] = (w[7] & ~0xffu) | 128;
				w[8] |= 0xffff; w[10] |= 0xffff;
				for (i = 0; i < 32; i++) {
					u16 x = (u16)(i < 31 ? rnd() % 1000 : t);

					t -= x;
					memcpy(v + 2 * i, &x, 2); memcpy(o + 2 * i, &x, 2);
				}
			}
			memcpy(v + 300, w, sizeof(w)); memcpy(o + 300, w, sizeof(w));
			memcpy(ref, o, 512);
			t32o_call((t32o_fn)(void (*)(void))lce_hist_method_16bit, T32O_PTR(v), T32O_PTR(v + 100),
				  T32O_PTR(v + 300), 0, 0);
			t32_lce_hist_method_16bit((const u16 *)o, (u16 *)(o + 100), (const struct t32_lce_hm16_par *)(o + 300));
			chk("hist_method_16bit", c, v, o, 512);
			{
				u32 s = 0;	/* which branch: sum of min(a, g) against 0x8000, from the port's view */
				for (i = 0; i < 32; i++)
					s += ((const u16 *)ref)[i] > 1024 ? 1 : 0;
				if (s > 16) nover++; else nunder++;
			}
		}
	}
	printf("t32_lce41l_oracle: %u x 5 leaves compared (%u zero b ramps, %u/%u high/low hist), %u mismatches\n",
	       n, nz, nover, nunder, failed);
	return failed ? 1 : 0;
}
