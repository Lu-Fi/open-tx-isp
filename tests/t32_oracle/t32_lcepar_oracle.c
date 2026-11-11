// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_lce_par (tx_isp_t32_blkrest.h) against the vendor
 * function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case random
 * channel, LCE state bytes (selector 0 / not, update flag, frame counter near
 * the wrap), lock flags.  Compared: return value, every callee in order
 * (lock / unlock / soft v4 / soft T41 / write_curve) with its arguments as
 * offsets into the state, a checksum of the work area at each callee (so the
 * copy sits between lock and unlock), every byte of the state.
 * Not observable here: kernel_mxu_begin_partial / _end (strong in env.c).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

extern u64 t32o_ref_tisp_lce_par(u32, u32, u32, u32);

static u32 rs = 0x68e31da4u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define STB 0x22850
#define LOGN 8
struct hlog { unsigned int n; u32 e[LOGN][6]; };
static struct hlog vl, ol, *cur;
static u8 *base;
static u32 flags_tok;
static u32 sum(void)
{
	u32 h = 2166136261u, i;

	for (i = T32_LCP_WORK; i < T32_LCP_WORK2 + T32_LCP_IN2_LEN; i++) h = (h ^ base[i]) * 16777619u;
	return h;
}
static void hrec(u32 k, u32 a, u32 b, u32 c, u32 d)
{
	if (cur->n < LOGN) {
		u32 *e = cur->e[cur->n];

		e[0] = k; e[1] = a; e[2] = b; e[3] = c; e[4] = d; e[5] = sum();
	}
	cur->n++;
}
static u32 off(u32 p) { return p - T32O_PTR(base); }
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	hrec(1, off(a0), 0, 0, 0);
	memcpy((void *)(unsigned long)a1, &flags_tok, 4);
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hrec(2, off(a0), a1, 0, 0); return 0; }
u64 Tisp_lce_soft_simd_v4(u32 a0, u32 a1, u32 a2, u32 a3) { hrec(3, a0, off(a1), off(a2), a3); return 0; }
u64 Tisp_lce_soft_t41_simd(u32 a0, u32 a1, u32 a2, u32 a3) { hrec(4, a0, off(a1), off(a2), a3); return 0; }
u64 tisp_lce_write_curve(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(5, a0, 0, 0, 0); return 0; }

static u32 lockf;
static u32 o_lock(void *l) { u32 f; lockf = 0; __private_spin_lock_irqsave(T32O_PTR(l), T32O_PTR(&lockf), 0, 0); f = lockf; return f; }
static void o_unlock(void *l, u32 f) { private_spin_unlock_irqrestore(T32O_PTR(l), f, 0, 0); }
static void o_mb(u32 z) { (void)z; }
static void o_me(void) { }
static void o_v4(u32 ch, u8 *p, u8 *w, u32 u) { Tisp_lce_soft_simd_v4(ch, T32O_PTR(p), T32O_PTR(w), u); }
static void o_t41(u32 ch, u8 *p, u8 *w, u32 u) { Tisp_lce_soft_t41_simd(ch, T32O_PTR(p), T32O_PTR(w), u); }
static void o_wc(u32 ch) { tisp_lce_write_curve(ch, 0, 0, 0); }
static const struct t32_lcep_ops ops = { o_lock, o_unlock, o_mb, o_me, o_v4, o_t41, o_wc };

int main(void)
{
	u8 *st[2] = { t32o_alloc(STB), t32o_alloc(STB) };
	u32 tab[2][4], i, s;
	unsigned long n, N = 3000, t41 = 0, wrap = 0;

	for (n = 0; n < N; n++) {
		u32 ch = rnd() & 3, rv, ro, cnt;

		for (i = 0; i < STB; i += 4) { u32 w = rnd(); memcpy(st[0] + i, &w, 4); }
		st[0][T32_LCP_SEL] = (rnd() & 1) ? 0 : rnd() | 1;
		st[0][T32_LCP_UPDATE] = rnd() & 1 ? 1 : rnd();
		cnt = (rnd() & 1) ? T32_LCP_COUNT_WRAP - 1 - (rnd() & 3) : ((rnd() & 1) ? rnd() : rnd() | 0x20000000u);
		memcpy(st[0] + T32_LCP_COUNT, &cnt, 4);
		memcpy(st[1], st[0], STB);
		for (s = 0; s < 2; s++) for (i = 0; i < 4; i++) tab[s][i] = (i == ch) ? T32O_PTR(st[s]) : rnd();
		flags_tok = rnd();

		memset(&ol, 0, sizeof(ol)); cur = &ol; base = st[1];
		ro = (u32)t32_lce_par(&ops, tab[1], ch);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl; base = st[0];
		memcpy(t32_data_e1da8, tab[0], 16);
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))t32o_ref_tisp_lce_par, ch, rnd(), rnd(), rnd(), 0);

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(st[0], st[1], STB)) {
			printf("t32_lcepar_oracle: FAIL case %lu ch %u: ret %#x/%#x calls %u/%u\n", n, ch, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x %#x / %u %#x %#x %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3],
				       vl.e[i][5], ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3], ol.e[i][5]);
			for (i = 0; i < STB; i++) if (st[0][i] != st[1][i]) { printf("  state +%#x\n", i); break; }
			return 1;
		}
		t41 += st[0][T32_LCP_SEL] != 0;
		wrap += cnt + 1 >= T32_LCP_COUNT_WRAP;
	}
	printf("t32_lcepar_oracle: PASS %lu cases (%lu T41 path, %lu counter wraps)\n", N, t41, wrap);
	return 0;
}
