// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_ae_ai_interrupt_static (tx_isp_t32_blkrest.h) against
 * the vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random channel, register read results (per read, in order), AE tuning
 * words +7448.. (equal to the first 3 statistics words in half the cases,
 * one of them differing otherwise) and parameter block.  Compared: return
 * value, every register read (address, order), the tisp_ae_roi_point call
 * with its 5 arguments, every byte of the tuning and parameter blocks.
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
extern u64 tisp_ae_ai_interrupt_static(u32, u32, u32, u32);

static u32 rs = 0x51ed2701u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 16
struct hlog { unsigned int n; u32 e[LOGN][6]; };
static struct hlog vl, ol, *cur;
static u32 rdv[LOGN];
static u32 rd(u32 reg)
{
	u32 v = cur->n < LOGN ? rdv[cur->n] : 0;

	if (cur->n < LOGN) { cur->e[cur->n][0] = 1; cur->e[cur->n][1] = reg; }
	cur->n++;
	return v;
}
u64 tisp_ae_roi_point(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 tisp_ae_roi_point(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	if (cur->n < LOGN) {
		u32 *e = cur->e[cur->n];

		e[0] = 2; e[1] = a0; e[2] = a1; e[3] = a2; e[4] = a3; e[5] = a4;
	}
	cur->n++;
	return 0;
}
static void o_roi(u32 ch, u32 r0, u32 r1, u32 r2, u32 r3) { tisp_ae_roi_point(ch, r0, r1, r2, r3); }
static const struct t32_aeirq_ops ops = { rd, o_roi };

#define TUNB 7480
#define PARB 1520
static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	u8 *tun[2], *par[2], *ctx[2];
	u32 ctxw[2][4], parw[2][4], i, s;
	unsigned long n, N = 20000, chg = 0;

	for (s = 0; s < 2; s++) { tun[s] = t32o_alloc(TUNB); par[s] = t32o_alloc(PARB); ctx[s] = t32o_alloc(8); }
	for (n = 0; n < N; n++) {
		struct t32_aeirq a;
		u32 ch = rnd() & 3, rv, ro;

		for (i = 0; i < LOGN; i++) rdv[i] = rnd();
		fill(tun[0], TUNB); fill(par[0], PARB);
		if (rnd() & 1) {
			memcpy(tun[0] + 7448, rdv, 12);
			if (rnd() & 1) tun[0][7448 + 4 * (rnd() % 3)] ^= 1 << (rnd() & 7);
		}
		memcpy(tun[1], tun[0], TUNB); memcpy(par[1], par[0], PARB);
		for (s = 0; s < 2; s++) {
			memcpy(ctx[s], (u32[2]){ rnd(), T32O_PTR(tun[s]) }, 8);
			for (i = 0; i < 4; i++) { ctxw[s][i] = T32O_PTR(ctx[s]); parw[s][i] = T32O_PTR(par[s]); }
		}
		memcpy(ctx[1], ctx[0], 4);

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		a.ops = &ops; a.par = parw[1]; a.ctx = ctxw[1];
		ro = (u32)t32_ae_ai_interrupt_static(&a, ch);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		t32o_rd_hook = rd;
		memcpy(t32_data_e15a8, ctxw[0], 16);
		memcpy(t32_data_e1588, parw[0], 16);
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_ai_interrupt_static, ch, rnd(), rnd(), rnd(), 0);
		t32o_rd_hook = NULL;

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(tun[0], tun[1], TUNB) ||
		    memcmp(par[0], par[1], PARB)) {
			printf("t32_aeirq_oracle: FAIL case %lu ch %u: ret %#x/%#x calls %u/%u\n", n, ch, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x / %u %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], ol.e[i][0], ol.e[i][1], ol.e[i][2]);
			return 1;
		}
		chg += ol.n > 8;
	}
	printf("t32_aeirq_oracle: PASS %lu cases (%lu with new statistics)\n", N, chg);
	return 0;
}
