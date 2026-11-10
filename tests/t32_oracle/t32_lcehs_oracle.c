// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our LCE histogram helpers (driver/t32/tx_isp_t32_lcehist.h) against lce_compress_data_part_1,
 * lce_pdf_to_cdf_16bit, lce_head_tail_search and lce_head_tail_search_16bit from Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value (low word), every output byte incl. 16 canary bytes after it, the inputs
 * (unchanged).  Inputs: zero, small, mixed and full-range bins (sums below and above the 32767 clamp and the
 * thresholds), lo / hi bytes 0..255 with high garbage, a head search that never trips.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lcehist.h"

u64 lce_compress_data_part_1(u32, u32, u32, u32);
u64 lce_pdf_to_cdf_16bit(u32, u32, u32, u32);
u64 lce_head_tail_search(u32, u32, u32, u32);
u64 lce_head_tail_search_16bit(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define OUTB	96

static u32 rs = 0x1f123bb5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

static u32 bin(unsigned int c)
{
	switch (c % 5) {
	case 0: return 0;
	case 1: return rnd() % 64;
	case 2: return rnd() % 2048;
	case 3: return rnd() & 0xffff;
	default: return rnd();
	}
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;

	for (c = 0; c < n; c++) {
		u32 h32[32], *vin, lo = rnd(), hi = rnd(), r, o;
		unsigned short h16[32];
		u8 *vout, mine[OUTB], *vin16;
		unsigned int k = c % 7 == 0 ? 0 : c, kk;

		for (i = 0; i < 32; i++) {
			kk = k % 9 == 8 ? (rnd() % 5) : k;
			h32[i] = bin(kk);
			h16[i] = (unsigned short)bin(kk);
		}
		if (c % 3 == 0) {
			lo &= 0xff;
			hi &= 0xff;
		}
		if (c % 11 == 0)
			lo = 0xff;	/* threshold 16320: never reached by small bins */
		t32o_reset();
		vin = t32o_alloc(32 * 4);
		vin16 = t32o_alloc(32 * 2);
		vout = t32o_alloc(OUTB);
		memcpy(vin, h32, sizeof(h32));
		memcpy(vin16, h16, sizeof(h16));

#define RUN(name, call, ours) do {						\
		for (i = 0; i < OUTB; i++)					\
			vout[i] = mine[i] = (u8)(c + i * 7);			\
		r = (u32)(call);						\
		o = (ours);							\
		compared++;							\
		if ((r != o || memcmp(vout, mine, OUTB) || memcmp(vin, h32, sizeof(h32)) ||	\
		     memcmp(vin16, h16, sizeof(h16))) && failed++ < 10)		\
			printf("MISMATCH %s case %u: ret %x/%x\n", name, c, r, o);	\
	} while (0)

		RUN("compress_data_part_1", t32o_call(F(lce_compress_data_part_1), T32O_PTR(vin), T32O_PTR(vout), 0, 0, 0),
		    t32_lce_compress_data_part_1(h32, (unsigned short *)mine));
		RUN("pdf_to_cdf_16bit", t32o_call(F(lce_pdf_to_cdf_16bit), T32O_PTR(vin16), T32O_PTR(vout), 0, 0, 0),
		    t32_lce_pdf_to_cdf_16bit(h16, mine));
		RUN("head_tail_search", t32o_call(F(lce_head_tail_search), T32O_PTR(vin), lo, hi, T32O_PTR(vout), 0),
		    t32_lce_head_tail_search(h32, lo, hi, (u32 *)mine));
		RUN("head_tail_search_16bit", t32o_call(F(lce_head_tail_search_16bit), T32O_PTR(vin16), lo, hi,
						     T32O_PTR(vout), 0),
		    t32_lce_head_tail_search_16bit(h16, lo, hi, (u32 *)mine));
	}
	printf("t32_lcehs_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
