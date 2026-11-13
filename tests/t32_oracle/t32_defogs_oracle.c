// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_defog_soft of driver/t32/tx_isp_t32_defogs.h against the unmodified vendor
 * Tiziano_defog_soft (0x5da70) with tisp_defog_max_filter / _img_filter9 / _img_filter25 as vendor code
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Per case two calls on the same 3948 byte block (the curve caches and their flag counters carry over).
 * Inputs: per cell statistics sized so that the scaled channel values are 0..~400 (clip above 255) for every
 * shift 0..31 and quadrant gains (zero gains too), random previous luma, curves, kernels, cache contents and
 * flag bytes (mostly zero), every mode byte 0 / 1 / other, sorted and unsorted transmission thresholds.
 * Kept out (vendor UB, see the header): negative channel values (negative gain, product low word >= 2^31).
 * Compared: every byte of the block and of both 5 byte tables (unchanged).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_defogs.h"

u64 Tiziano_defog_soft(u32, u32, u32, u32);

#define PN T32_DEFOG_PAR_BYTES

static u32 rs = 0xdef50f7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, nmask, nsmooth, nluma, nclip;

static u8 mb(void)	/* mode byte: mostly 0 / 1 */
{
	u32 x = rnd() % 8;

	return (u8)(x < 3 ? 0 : x < 7 ? 1 : rnd());
}

int main(void)
{
	unsigned int c, i, k, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 4000, failed = 0;
	static struct t32_defog_par po;

	for (c = 0; c < n && !failed; c++) {
		u8 *pv, *ta, *tb, tav[5], tbv[5];
		u32 sh = rnd() % 32, g[4];

		t32o_reset();
		pv = t32o_alloc(PN + 64);
		ta = t32o_alloc(16); tb = t32o_alloc(16);
		for (i = 0; i < PN + 64; i++)
			pv[i] = (u8)rnd();
		for (k = 0; k < 4; k++) {
			g[k] = rnd() % 9 == 0 ? 0 : (rnd() & 0x00ffffffu) >> (rnd() % 16);
			memcpy(pv + T32_DFS_GAIN + 4 * k, &g[k], 4);
		}
		pv[T32_DFS_SHIFT] = (u8)sh;
		pv[T32_DFS_QROW] = (u8)(rnd() % 12);
		pv[T32_DFS_QCOL] = (u8)(rnd() % 20);
		for (i = 0; i < 540; i++) {	/* statistics: value t = X * g >> (31 - sh) around 0..400 */
			unsigned int cell = i % 180, q = (cell / 18 < pv[T32_DFS_QROW] ? 0 : 2) + (cell % 18 < pv[T32_DFS_QCOL] ? 0 : 1);
			u64 t = rnd() % 420, x = g[q] ? (t << (31 - sh)) / g[q] : rnd();
			u32 xv;

			if (rnd() % 4 == 0)
				x += rnd() % 64;
			if (x > 0xffffffffu)
				x = 0xffffffffu;
			while (x && (s32)(u32)(((s64)x * (s32)g[q]) >> (31 - sh)) + 1 <= 0)
				x >>= 4;
			xv = (u32)x;
			memcpy(pv + 4 * i, &xv, 4);
			if (t > 255)
				nclip++;
		}
		for (i = 0; i < 256; i++)
			pv[T32_DFS_FLAG + i] = (u8)(rnd() % 4 ? 0 : rnd());
		pv[T32_DFS_NOSTR] = (u8)(rnd() % 4 ? 0 : mb());
		pv[T32_DFS_LUMAMODE] = mb();
		pv[T32_DFS_MASK] = mb();
		pv[T32_DFS_SMOOTH] = mb();
		if (rnd() % 6 == 0) pv[T32_DFS_MK_K] = 0;
		if (rnd() % 6 == 0) pv[T32_DFS_MK_HI] = 0;
		if (rnd() % 6 == 0) pv[T32_DFS_MK_LO] = (u8)(pv[T32_DFS_MK_HI] << 4);
		for (k = 0; k < 5; k++) {
			ta[k] = (u8)rnd();
			tb[k] = (u8)rnd();
		}
		if (rnd() % 4) {	/* ascending thresholds (the normal case), sometimes with equal neighbours */
			u8 a = (u8)(rnd() % 40);

			for (k = 0; k < 5; k++) {
				ta[k] = a;
				a = (u8)(a + (rnd() % 5 ? 1 + rnd() % 60 : 0));
				if (a < ta[k]) a = 255;
			}
		}
		memcpy(tav, ta, 5); memcpy(tbv, tb, 5);
		{
			u32 pa = T32O_PTR(ta), pb = T32O_PTR(tb);

			memcpy(pv + 2176, &pa, 4); memcpy(pv + 2180, &pb, 4);
		}
		memcpy(po.b, pv, PN);
		po.tbl_a = tav; po.tbl_b = tbv;
		nmask += pv[T32_DFS_MASK] == 1; nsmooth += pv[T32_DFS_SMOOTH] == 1; nluma += pv[T32_DFS_LUMAMODE] != 0;
		for (k = 0; k < 2 && !failed; k++) {
			if (k) {	/* second frame: new statistics, same caches */
				for (i = 0; i < 540 * 4; i += 4) {
					u32 x;
					memcpy(&x, pv + i, 4);
					x = x / 2 + (x / 4) * (rnd() % 3);
					memcpy(pv + i, &x, 4); memcpy(po.b + i, &x, 4);
				}
			}
			t32o_call((t32o_fn)(void (*)(void))Tiziano_defog_soft, T32O_PTR(pv), 0, 0, 0, 0);
			t32_defog_soft(&po);
			compared++;
			for (i = 0; i < PN; i++)
				if (pv[i] != po.b[i]) {
					printf("MISMATCH case %u call %u +%u vendor %02x ours %02x (luma %u mask %u smooth %u nostr %u)\n",
					       c, k, i, pv[i], po.b[i], pv[T32_DFS_LUMAMODE], pv[T32_DFS_MASK], pv[T32_DFS_SMOOTH],
					       pv[T32_DFS_NOSTR]);
					failed++;
					break;
				}
			if (!failed && (memcmp(ta, tav, 5) || memcmp(tb, tbv, 5)))
				failed = printf("MISMATCH tables changed\n");
		}
	}
	/*
	 * BEYOND VENDOR (#281): negative channel values (vendor: index before the curve / cache / flag tables)
	 * are clipped to 0.  Ours only: every gain negative and every statistic nonzero gives channel 0 in
	 * every cell, so every strength byte is the curve at 0 and every transmission byte the curve at 0.
	 */
	for (c = 0; c < 500 && !failed; c++) {
		static struct t32_defog_par pn, pr;
		u8 tav[5], tbv[5], s0, t0;

		for (i = 0; i < PN; i++)
			pn.b[i] = (u8)rnd();
		for (k = 0; k < 4; k++) {
			u32 gk = 0x80000000u | rnd();

			memcpy(pn.b + T32_DFS_GAIN + 4 * k, &gk, 4);
		}
		for (i = 0; i < 540; i++) {
			u32 x = 1 + rnd() % 0x7ffffffeu;	/* shift 31: product >> 31 in -2^31 .. -1 */

			memcpy(pn.b + 4 * i, &x, 4);
		}
		pn.b[T32_DFS_SHIFT] = 0;
		pn.b[T32_DFS_NOSTR] = 0;
		pn.b[T32_DFS_MASK] = 0;
		pn.b[T32_DFS_SMOOTH] = 0;
		pn.b[T32_DFS_LUMAMODE] = (u8)(rnd() & 1);
		for (k = 0; k < 5; k++)
			tav[k] = (u8)rnd(), tbv[k] = (u8)rnd();
		pn.tbl_a = tav; pn.tbl_b = tbv;
		pr = pn;
		s0 = t32_dfs_strength(pr.b, 0);
		t0 = t32_dfs_trans(pr.b, tav, tbv, 0);
		t32_defog_soft(&pn);
		for (i = 0; i < 180 && !failed; i++)
			if (pn.b[T32_DFS_STR + i] != s0 || pn.b[T32_DFS_STR + 180 + i] != s0 ||
			    pn.b[T32_DFS_STR + 360 + i] != s0 || pn.b[T32_DFS_TRANS + i] != t0 || pn.b[T32_DFS_LUMA + i])
				failed = 1, printf("MISMATCH negative channel clip (beyond vendor) case %u cell %u\n", c, i);
	}
	printf("t32_defogs_oracle: %u calls compared (mask %u, smooth %u, luma mode %u cases, %u clipped cells),"
	       " 500 negative-channel blocks (beyond vendor, ours only), %u mismatches\n",
	       compared, nmask, nsmooth, nluma, nclip, failed);
	return failed ? 1 : 0;
}
