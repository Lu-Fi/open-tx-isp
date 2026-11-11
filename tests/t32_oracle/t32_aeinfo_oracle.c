// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_aeinfo.h (tisp_ae_max_exp_calc_fps,
 * tisp_ae_algo_getinfo) against the vendor functions (Matt Davis' recovered
 * C, PR #15 @ 2e2e06bc).  Per case random channel, EV, context / state /
 * tuning / parameter blocks and zone statistics (the same on both sides;
 * the fraction-bit word also >= 32 to check the 5-bit shift).  The
 * fix-point callees and tisp_g_ae_exprinfo_attr are stubs here that log
 * their arguments and return / write deterministic values.  Compared:
 * return values, every call in order (lock, unlock with the flags token,
 * callee arguments), every byte of the four AE contexts.  Exit 1 on the
 * first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_aeinfo.h"

extern u32 t32_data_e1588[4], t32_data_e15a8[4];
extern u8 t32_small_bss[64];
u8 t32_data_d0950[4 * T32_AEI_ZONE_STRIDE] __attribute__((weak, aligned(16)));	/* .bss: AE zone statistics */
extern u64 tisp_ae_max_exp_calc_fps(u32, u32, u32, u32), tisp_ae_algo_getinfo(u32, u32, u32, u32);

static u32 rs = 0x510e527fu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 8
struct hlog { unsigned int n; u32 e[LOGN][4]; };
static struct hlog vl, ol, *cur;
static void hrec(u32 k, u32 a, u32 b, u32 c)
{
	if (cur->n < LOGN) { cur->e[cur->n][0] = k; cur->e[cur->n][1] = a; cur->e[cur->n][2] = b; cur->e[cur->n][3] = c; }
	cur->n++;
}
static u32 mix(u32 a, u32 b) { return (a * 0x9e3779b1u) ^ (b + 0x7f4a7c15u) ^ (a >> 7); }
static u32 c_mult(u32 f, u32 a, u32 b) { hrec(1, f, a, b); return mix(a, b + f); }
static u32 c_div(u32 f, u32 a, u32 b) { hrec(2, f, a, b); return mix(b, a ^ f); }
static u32 c_lock(void) { hrec(3, 0, 0, 0); return 0x5eed; }
static void c_unlock(u32 fl) { hrec(4, fl, 0, 0); }
static void c_expr(u32 ch, u32 *info)
{
	u32 i;

	hrec(5, ch, 0, 0);
	for (i = 0; i < 36; i++)
		info[i] = mix(ch, i);
}
u64 fix_point_mult2_32(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return c_mult(a0, a1, a2); }
u64 fix_point_div_32(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return c_div(a0, a1, a2); }
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	*(u32 *)T32O_HOST(a1) = c_lock();
	if (a0 != T32O_PTR(t32_small_bss))
		hrec(99, a0, 0, 0);
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (a0 != T32O_PTR(t32_small_bss))
		hrec(99, a0, 0, 0);
	c_unlock(a1);
	return 0;
}
u64 tisp_g_ae_exprinfo_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; c_expr(a0, T32O_HOST(a1)); return 0; }
static const struct t32_aeinfo_ops ops = { c_mult, c_div, c_lock, c_unlock, c_expr };

#define CTXB 2016
#define TUNB 8560
#define STB 1740
#define PARB 1264
static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	u8 *ctx[2][4], *tun[4], *st[4], *par[4], zones[4 * T32_AEI_ZONE_STRIDE];
	unsigned long n, N = 6000, per[2] = { 0 };
	u32 c, s;

	for (c = 0; c < 4; c++) {
		for (s = 0; s < 2; s++) ctx[s][c] = t32o_alloc(CTXB);
		tun[c] = t32o_alloc(TUNB); st[c] = t32o_alloc(STB); par[c] = t32o_alloc(PARB);
	}
	for (n = 0; n < N; n++) {
		struct t32_aeinfo_dev d;
		int fn = (int)(rnd() & 1);
		u32 ch = rnd() & 3, ev = rnd(), rv, ro, cw[2][4], pw[4];

		fill(zones, sizeof(zones));
		for (c = 0; c < 4; c++) {
			u32 w[2];

			fill(tun[c], TUNB); fill(st[c], STB); fill(par[c], PARB);
			if (rnd() & 1) { u16 f = rnd() % 40; memcpy(st[c] + T32_AEI_ST_FRAC, &f, 2); }
			fill(ctx[0][c], CTXB);
			w[0] = T32O_PTR(st[c]); w[1] = T32O_PTR(tun[c]);
			memcpy(ctx[0][c], w, 8);
			memcpy(ctx[1][c], ctx[0][c], CTXB);
			for (s = 0; s < 2; s++) cw[s][c] = T32O_PTR(ctx[s][c]);
			pw[c] = T32O_PTR(par[c]);
		}

		t32o_reset();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		d.ops = &ops; d.ctx = cw[1]; d.par = pw; d.zones = zones;
		ro = fn ? T32O_PTR(t32_ae_algo_getinfo(&d, ch)) : t32_ae_max_exp_calc_fps(&d, ch, ev);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_e15a8, cw[0], 16);
		memcpy(t32_data_e1588, pw, 16);
		memcpy(t32_data_d0950, zones, sizeof(zones));
		rv = (u32)t32o_call(fn ? (t32o_fn)(void (*)(void))tisp_ae_algo_getinfo
				       : (t32o_fn)(void (*)(void))tisp_ae_max_exp_calc_fps, ch, ev, rnd(), rnd(), 0);
		if (fn)	/* the result is the info block: compare as offsets */
			rv -= cw[0][ch], ro -= cw[1][ch];

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e))) {
			printf("t32_aeinfo_oracle: FAIL case %lu fn %d ch %u: ret %#x/%#x hooks %u/%u\n", n, fn, ch, rv, ro, vl.n, ol.n);
			return 1;
		}
		for (c = 0; c < 4; c++)
			if (memcmp(ctx[0][c], ctx[1][c], CTXB)) {
				for (s = 0; s < CTXB && ctx[0][c][s] == ctx[1][c][s]; s++)
					;
				printf("t32_aeinfo_oracle: FAIL case %lu fn %d: context %u byte %u\n", n, fn, c, s);
				return 1;
			}
		per[fn]++;
	}
	printf("t32_aeinfo_oracle: PASS %lu cases (max_exp_calc_fps %lu, algo_getinfo %lu)\n", N, per[0], per[1]);
	return 0;
}
