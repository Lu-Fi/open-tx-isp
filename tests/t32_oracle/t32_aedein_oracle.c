// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_ae_deinit (tx_isp_t32_librest.h) against the vendor
 * function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case a random
 * channel; the four per channel tables (e15d8 / e15c8 / e1598 / e15a8) of all
 * four channels hold NULL or a block, every word inside the blocks is NULL or
 * random.  Both sides run on the same blocks (restored in between).  Compared:
 * every kfree call in order with its pointer, all four tables (4 channels,
 * the other three channels are decoys) and every byte of every block.
 * The vendor return value is undefined (no return value) and not compared.
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_librest.h"

extern u32 t32_data_e15d8[4], t32_data_e15c8[4], t32_data_e1598[4], t32_data_e15a8[4];
extern u64 tisp_ae_deinit(u32, u32, u32, u32);

static u32 rs = 0x6b43a9b5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 32
struct hlog { unsigned int n; u32 e[LOGN]; };
static struct hlog vl, ol, *cur;
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (cur->n < LOGN) cur->e[cur->n] = a0;
	cur->n++;
	return 0;
}
static void o_kfree(void *p) { private_kfree(T32O_PTR(p), 0, 0, 0); }

#define BLK 32
static u32 rptr(void) { return (rnd() & 1) ? rnd() | 1 : 0; }

int main(void)
{
	u8 *st[4], *cx[4], init[2][4][BLK], res[2][4][BLK];
	u32 tab[16], tv[16], to[16], c, i;
	unsigned long n, N = 20000, nfree = 0;

	for (c = 0; c < 4; c++) { st[c] = t32o_alloc(BLK); cx[c] = t32o_alloc(BLK); }
	for (n = 0; n < N; n++) {
		struct t32_aed a;
		u32 ch = rnd() & 3;

		for (c = 0; c < 4; c++) {
			for (i = 0; i < BLK; i += 4) {
				u32 w = (i == 4 && (rnd() & 3)) ? rptr() : rnd();
				memcpy(&init[0][c][i], &w, 4);
				w = (i == 4 && (rnd() & 3)) ? rptr() : rnd();
				memcpy(&init[1][c][i], &w, 4);
			}
			tab[c] = rptr();
			tab[4 + c] = rptr();
			tab[8 + c] = (rnd() & 3) ? T32O_PTR(st[c]) : 0;
			tab[12 + c] = (rnd() & 3) ? T32O_PTR(cx[c]) : 0;
		}

		/* ours */
		for (c = 0; c < 4; c++) { memcpy(st[c], init[0][c], BLK); memcpy(cx[c], init[1][c], BLK); }
		memcpy(to, tab, sizeof(to));
		t32o_reset();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		a.kfree = o_kfree; a.state_p = to; a.state_q = to + 4; a.stats = to + 8; a.ctx = to + 12;
		t32_ae_deinit(&a, ch);
		for (c = 0; c < 4; c++) memcpy(res[1][c], st[c], BLK);
		u8 ocx[4][BLK];
		for (c = 0; c < 4; c++) memcpy(ocx[c], cx[c], BLK);

		/* vendor, same blocks */
		for (c = 0; c < 4; c++) { memcpy(st[c], init[0][c], BLK); memcpy(cx[c], init[1][c], BLK); }
		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_e15d8, tab, 16);
		memcpy(t32_data_e15c8, tab + 4, 16);
		memcpy(t32_data_e1598, tab + 8, 16);
		memcpy(t32_data_e15a8, tab + 12, 16);
		t32o_call((t32o_fn)(void (*)(void))tisp_ae_deinit, ch, rnd(), rnd(), rnd(), 0);
		memcpy(tv, t32_data_e15d8, 16);
		memcpy(tv + 4, t32_data_e15c8, 16);
		memcpy(tv + 8, t32_data_e1598, 16);
		memcpy(tv + 12, t32_data_e15a8, 16);
		for (c = 0; c < 4; c++) memcpy(res[0][c], st[c], BLK);

		nfree += vl.n;
		int bad = vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(tv, to, sizeof(tv)) ||
			memcmp(res[0], res[1], sizeof(res[0]));
		for (c = 0; c < 4 && !bad; c++)
			bad = memcmp(cx[c], ocx[c], BLK) != 0;
		if (bad) {
			printf("t32_aedein_oracle: MISMATCH case %lu ch %u: kfree %u/%u\n", n, ch, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++) printf("  v %08x o %08x\n", vl.e[i], ol.e[i]);
			for (i = 0; i < 16; i++) if (tv[i] != to[i]) printf("  tab %u v %08x o %08x\n", i, tv[i], to[i]);
			return 1;
		}
	}
	printf("t32_aedein_oracle: %lu cases, %lu kfree calls, 0 mismatches\n", N, nfree);
	return 0;
}
