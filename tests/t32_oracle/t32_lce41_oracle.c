// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, end to end: t32_lce_soft_t41 of driver/t32/tx_isp_t32_lce41.h against the unmodified vendor
 * Tisp_lce_soft_t41 (0x563a0) with all its leaves as vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Inputs as the t32_lcet41 harness: random parameter blocks and working buffers whose tile bins give totals on both
 * sides of 0x8000, every quadrant split, selected tile inside / outside the grid, judge overrides, both correction
 * switches, mode 4 and others.  Compared: every byte of the working buffer (0x26400 B) and the parameter block
 * (unchanged).  Thresholds are kept 0..31 (see the header).  Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce41.h"

#define WSZ 0x26400U
#define PSZ 400U

u8 oc_d6c66 __asm__("t32_data_d6c66") __attribute__((weak)) = 20;	/* rodata data.c:25798 */
u8 oc_d6c67 __asm__("t32_data_d6c67") __attribute__((weak)) = 20;
/* lce_hist_method copies this 128 B table into a buffer it overwrites before reading (as in t32_lcet41_common.inc) */
u8 oc_bf8a0[128] __asm__("t32_data_bf8a0") __attribute__((weak));
extern u64 Tisp_lce_soft_t41(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x41a5c3eu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, nsel, nm4;

static int one(const u8 *par, const u8 *w, u32 mode)
{
	u8 *wo = malloc(WSZ), *po = malloc(PSZ), *wr, *pr;
	int bad = 0;
	unsigned int i;

	memcpy(wo, w, WSZ);
	memcpy(po, par, PSZ);
	t32o_reset();
	wr = t32o_alloc(WSZ); pr = t32o_alloc(PSZ);
	memcpy(wr, w, WSZ);
	memcpy(pr, par, PSZ);
	t32o_call((t32o_fn)(void (*)(void))Tisp_lce_soft_t41, rnd(), T32O_PTR(pr), T32O_PTR(wr), mode, 0);
	t32_lce_soft_t41(po, wo, mode);
	compared++;
	if (memcmp(pr, par, PSZ) || memcmp(po, par, PSZ))
		bad = printf("MISMATCH par changed\n");
	for (i = 0; i < WSZ && !bad; i++)
		if (wr[i] != wo[i])
			bad = printf("MISMATCH w+0x%x vendor %02x port %02x (mode %u)\n", i, wr[i], wo[i], mode);
	free(wo); free(po);
	return bad;
}

static int gen_and_run(void)
{
	static u8 par[PSZ], w[WSZ];
	unsigned int i, sc = 1u << (rnd() % 13);
	u32 mode;

	for (i = 0; i < PSZ; i++)
		par[i] = (u8)rnd();
	for (i = 0; i < WSZ; i++)
		w[i] = (u8)rnd();
	for (i = 0; i < 0x9000; i += 2) {
		u32 v = rnd() % (1 + sc);

		w[i] = (u8)v; w[i + 1] = (u8)(v >> 8);
	}
	{
		u64 sb = 1;
		u32 mult, sh = rnd() % 20, tgt = (rnd() % 4) ? 0x7000 + rnd() % 0x6000 : rnd() % 0x20000;

		for (i = 0; i < 0x9000; i += 2)
			sb += w[i] | w[i + 1] << 8;
		mult = (u32)(((u64)tgt << sh) / sb);
		if (!mult || !(rnd() % 8))
			mult = 1 + rnd() % 300;
		memcpy(par + 4, &mult, 4);
		memcpy(par + 8, &sh, 4);
		if (!(rnd() % 8)) {	/* the global sum is not cleared: a big carried histogram */
			u32 x = rnd();
			memcpy(w + T32_LCE_T41_A + 277 + 4 * (rnd() % 32), &x, 4);
		}
	}
	par[284] = (rnd() % 3) ? 1 : (u8)rnd();
	par[285] = (u8)(rnd() % 32);
	par[294] = (rnd() % 3) ? 1 : (u8)rnd();
	for (i = 0; i < 4; i++) {
		u32 gain = (rnd() % 3) ? 256 + rnd() % 600 : rnd() % 2048;

		memcpy(par + 68 + 4 * i, &gain, 4);
	}
	par[110] = (u8)(rnd() % 18);
	par[111] = (u8)(rnd() % 32);
	if (!(rnd() % 4))
		par[110] = (u8)rnd();
	else
		nsel++;
	par[12] = (u8)(rnd() % 20);
	par[13] = (u8)(rnd() % 34);
	par[229] = (rnd() % 3) ? 1 : (u8)rnd();
	par[230] = (u8)(rnd() % 32);
	par[234] = (rnd() % 2) ? (u8)rnd() : (u8)(rnd() % 8);
	par[235] = (u8)(rnd() % 16);
	par[236] = (u8)(rnd() % 8);
	par[237] = (u8)(rnd() % 16);
	par[239] = (rnd() % 3) ? (u8)(1 + rnd() % 255) : 0;
	if (!(rnd() % 6))
		par[236] = par[237] = 0;
	par[238] = (rnd() % 3) ? 128 + (u8)(rnd() % 100) : (u8)rnd();
	if (rnd() % 3)
		par[112] = (u8)(rnd() % 129);
	for (i = 0; i < 6; i++) {	/* enable bytes of the two corrections: zero or not, independently */
		static const u16 en[6] = { 296, 297, 298, 241, 242, 243 };

		if (rnd() % 3 == 0)
			par[en[i]] = 0;
	}
	mode = (rnd() % 3) ? 4 : rnd() % 6;
	if (mode == 4)
		nm4++;
	return one(par, w, mode);
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 300;
	int fail = 0;

	for (c = 0; c < n && !fail; c++)
		fail = gen_and_run();
	printf("t32_lce41_oracle: %u compared (%u with a selected tile, %u mode 4), %u mismatches\n", compared, nsel,
	       nm4, fail ? 1u : 0u);
	return fail;
}
