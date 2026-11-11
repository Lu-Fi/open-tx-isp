// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_ae_algo_init (tx_isp_t32_blkrest.h) against the
 * vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random channel, enable (1 / 0 / random), d03a0 (0 / 1 / other), AE
 * parameter and tuning blocks (random, the two checked words forced to 0 in
 * a third of the cases each), zone table d0950 and register read results.
 * Compared: the enable table, the completion pointer, every register read
 * (channel, register, order), every byte of the 160-byte info block
 * (canary filled).  Not compared: the return value (vendor left-over
 * register) and info +126 .. +131 (vendor stack left-over; the reference's
 * stack and ours are 0).  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

u32 t32_data_e15b8[4];
u8 t32_data_d0950[4 * 1052];
extern u32 t32_data_e15a8[4];
extern u64 tisp_ae_algo_init(u32, u32, u32, u32);

static u32 rs = 0x7f4a7c15u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 8
struct hlog { unsigned int n; u32 e[LOGN][3]; };
static struct hlog vl, ol, *cur;
static u32 rdv[2];
static void hrec(u32 k, u32 a, u32 b)
{
	if (cur->n < LOGN) { cur->e[cur->n][0] = k; cur->e[cur->n][1] = a; cur->e[cur->n][2] = b; }
	cur->n++;
}
u64 private_init_completion(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(1, a0, 0); return 0; }
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hrec(2, a0, a1); return rdv[cur->n & 1]; }
static void o_ic(void *c) { private_init_completion(T32O_PTR(c), 0, 0, 0); }
static u32 o_rd(u32 ch, u32 reg) { return (u32)tisp_reg_read(ch, reg, 0, 0); }
static const struct t32_aea_ops ops = { o_ic, o_rd };

#define INFB 160
static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	u8 *par = t32o_alloc(512), *tun = t32o_alloc(8640), *ctx = t32o_alloc(2016);
	u8 *vi = t32o_alloc(INFB), *oi = t32o_alloc(INFB);
	u32 ctxw[4], en[4], i;
	unsigned long n, N = 20000, on = 0;

	for (n = 0; n < N; n++) {
		struct t32_aea a;
		u32 ch = rnd() & 3, r = rnd() % 3, enable = r == 0 ? 1 : (r == 1 ? 0 : rnd());
		u8 sh = (rnd() % 3 == 0) ? 0 : ((rnd() & 1) ? 1 : rnd() | 2);

		fill(par, 512); fill(tun, 8640); fill(t32_data_d0950, sizeof(t32_data_d0950));
		if (rnd() % 3 == 0) memset(tun + 8620, 0, 4);
		if (rnd() % 3 == 0) memset(tun + 8636, 0, 4);
		memcpy(ctx, (u32[2]){ T32O_PTR(par), T32O_PTR(tun) }, 8);
		for (i = 0; i < 4; i++) { ctxw[i] = T32O_PTR(ctx); en[i] = rnd(); }
		rdv[0] = rnd(); rdv[1] = rnd();
		fill(vi, INFB); memcpy(oi, vi, INFB);
		memcpy(t32_data_e15b8, en, 16);

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		a.ops = &ops; a.enable = en; a.ctx = ctxw; a.shadow = &sh; a.zone = t32_data_d0950;
		t32_ae_algo_init(&a, ch, enable, (struct t32_aea_info *)oi);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_e15a8, ctxw, 16);
		t32_data_d03a0[0] = sh;
		t32o_call((t32o_fn)(void (*)(void))tisp_ae_algo_init, ch, enable, T32O_PTR(vi), rnd(), 0);
		t32_data_d03a0[0] = 0;

		if (memcmp(t32_data_e15b8, en, 16) || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) ||
		    memcmp(vi, oi, 126) || memcmp(vi + 132, oi + 132, INFB - 132)) {
			printf("t32_aealgo_oracle: FAIL case %lu ch %u en %#x sh %u: calls %u/%u\n", n, ch, enable, sh, vl.n, ol.n);
			for (i = 0; i < INFB; i++)
				if (vi[i] != oi[i]) { printf("  info +%u: %#x / %#x\n", i, vi[i], oi[i]); break; }
			return 1;
		}
		on += enable == 1;
	}
	printf("t32_aealgo_oracle: PASS %lu cases (%lu enabled)\n", N, on);
	return 0;
}
