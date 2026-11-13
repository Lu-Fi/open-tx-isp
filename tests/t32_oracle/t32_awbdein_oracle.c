// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_awb_deinit (tx_isp_t32_blkrest.h) against the vendor
 * function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case a random
 * channel; the three per channel tables (e16a4 / e16b4 / e1694) of all four
 * channels hold NULL or a block, every buffer pointer inside the blocks is
 * NULL or random.  Compared: return value, every kfree / vfree call in order
 * with its pointer, all three tables (4 channels) and every byte of both
 * blocks of every channel.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

extern u8 t32_data_e1694[0x100];
extern u64 tisp_awb_deinit(u32, u32, u32, u32);

static u32 rs = 0x2545f491u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 32
struct hlog { unsigned int n; u32 e[LOGN][2]; };
static struct hlog vl, ol, *cur;
static void hrec(u32 k, u32 a)
{
	if (cur->n < LOGN) { cur->e[cur->n][0] = k; cur->e[cur->n][1] = a; }
	cur->n++;
}
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(1, a0); return 0; }
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(2, a0); return 0; }
static void o_kfree(void *p) { private_kfree(T32O_PTR(p), 0, 0, 0); }
static void o_vfree(void *p) { private_vfree(T32O_PTR(p), 0, 0, 0); }
static const struct t32_awbd_ops ops = { o_kfree, o_vfree };

#define WKB 48
#define STB 16
static u32 rptr(void) { return (rnd() & 1) ? rnd() | 1 : 0; }

int main(void)
{
	u8 *wk[2][4], *st[2][4];
	u32 tab[2][12], c, s, i;
	unsigned long n, N = 20000, freed = 0;

	for (c = 0; c < 4; c++)
		for (s = 0; s < 2; s++) { wk[s][c] = t32o_alloc(WKB); st[s][c] = t32o_alloc(STB); }
	for (n = 0; n < N; n++) {
		struct t32_awbd a;
		u32 ch = rnd() & 3, rv, ro;

		for (c = 0; c < 4; c++) {
			for (i = 0; i < WKB; i += 4) { u32 w = rptr(); memcpy(wk[0][c] + i, &w, 4); }
			for (i = 0; i < STB; i += 4) { u32 w = rptr(); memcpy(st[0][c] + i, &w, 4); }
			memcpy(wk[1][c], wk[0][c], WKB); memcpy(st[1][c], st[0][c], STB);
			for (s = 0; s < 2; s++) {
				tab[s][c] = rptr();
				tab[s][4 + c] = (rnd() & 3) ? T32O_PTR(wk[s][c]) : 0;
				tab[s][8 + c] = (rnd() & 3) ? T32O_PTR(st[s][c]) : 0;
			}
			tab[1][c] = tab[0][c];
			if (!tab[0][4 + c]) tab[1][4 + c] = 0;
			if (!tab[1][4 + c]) tab[0][4 + c] = 0;
			if (!tab[0][8 + c]) tab[1][8 + c] = 0;
			if (!tab[1][8 + c]) tab[0][8 + c] = 0;
		}

		t32o_reset();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		a.ops = &ops; a.zone = tab[1]; a.work = tab[1] + 4; a.state = tab[1] + 8;
		ro = (u32)t32_awb_deinit(&a, ch);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_e1694 + 0x10, tab[0], 16);
		memcpy(t32_data_e1694 + 0x20, tab[0] + 4, 16);
		memcpy(t32_data_e1694, tab[0] + 8, 16);
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_awb_deinit, ch, rnd(), rnd(), rnd(), 0);
		memcpy(tab[0], t32_data_e1694 + 0x10, 16);
		memcpy(tab[0] + 4, t32_data_e1694 + 0x20, 16);
		memcpy(tab[0] + 8, t32_data_e1694, 16);

		/* vendor pointers are its own blocks: map them to ours for the table compare */
		for (c = 0; c < 4; c++) {
			if (tab[0][4 + c]) tab[0][4 + c] = T32O_PTR(wk[1][c]);
			if (tab[0][8 + c]) tab[0][8 + c] = T32O_PTR(st[1][c]);
		}
		for (i = 0; i < vl.n && i < LOGN; i++)
			for (c = 0; c < 4; c++) {
				if (vl.e[i][1] == T32O_PTR(wk[0][c])) vl.e[i][1] = T32O_PTR(wk[1][c]);
				if (vl.e[i][1] == T32O_PTR(st[0][c])) vl.e[i][1] = T32O_PTR(st[1][c]);
			}
		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(tab[0], tab[1], sizeof(tab[0]))) {
			printf("t32_awbdein_oracle: FAIL case %lu ch %u: ret %#x/%#x frees %u/%u\n", n, ch, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x / %u %#x\n", i, vl.e[i][0], vl.e[i][1], ol.e[i][0], ol.e[i][1]);
			return 1;
		}
		for (c = 0; c < 4; c++)
			if (memcmp(wk[0][c], wk[1][c], WKB) || memcmp(st[0][c], st[1][c], STB)) {
				printf("t32_awbdein_oracle: FAIL case %lu ch %u: block of channel %u differs\n", n, ch, c);
				return 1;
			}
		freed += ol.n;
	}
	printf("t32_awbdein_oracle: PASS %lu cases (%lu frees)\n", N, freed);
	return 0;
}
