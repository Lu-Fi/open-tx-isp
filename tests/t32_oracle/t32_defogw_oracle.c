// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: defog_wei_interpcot of driver/t32/tx_isp_t32_defogw.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc).
 * Compared: the 32 output bytes plus 32 canary bytes behind them.
 * Inputs: 0..600 samples, bin indexes 0..31 (some bins empty, some full),
 * values 0..255 (means above 31, rounding ties).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_defogw.h"

u64 defog_wei_interpcot(u32, u32, u32, u32);

static u32 rs = 0xdef09e1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, k, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;

	for (c = 0; c < n; c++) {
		u32 cnt = (rnd() & 1) ? rnd() % 40 : rnd() % 601, span = 1 + rnd() % 32, vmax = (rnd() & 1) ? 32 + rnd() % 8 : 256;
		u8 *idx, *val, *ov, oo[64];

		t32o_reset();
		idx = t32o_alloc(640); val = t32o_alloc(640); ov = t32o_alloc(64);
		for (k = 0; k < cnt; k++) {
			idx[k] = (u8)(rnd() % span);
			val[k] = (u8)(rnd() % vmax);
		}
		for (k = 0; k < 64; k++)
			ov[k] = oo[k] = (u8)rnd();
		t32o_call((t32o_fn)(void (*)(void))defog_wei_interpcot, cnt, T32O_PTR(idx), T32O_PTR(val), T32O_PTR(ov), 0);
		t32_defog_wei_interpcot(cnt, idx, val, oo);
		if (memcmp(ov, oo, 64) && failed++ < 8)
			printf("MISMATCH n %u span %u\n", cnt, span);
	}
	printf("t32_defogw_oracle: %u compared, %u mismatches\n", n, failed);
	return failed ? 1 : 0;
}
