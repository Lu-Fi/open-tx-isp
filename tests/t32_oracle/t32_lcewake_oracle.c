// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_lce_wake_up (tx_isp_t32_blkrest.h) against the vendor
 * function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case random
 * channel, buffer table (e1db8), bank word (e1d7c), geometry words (e1d80),
 * LCE state bytes, d03a0 (0 / 1 / other), write_ctrl_reg result.  Compared:
 * return value, every tisp_reg_write (channel, register, value, order), the
 * write_curve and write_ctrl_reg calls.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

u32 t32_data_e1d7c[1];
extern u8 t32_data_e1d80[64];
extern u64 tisp_lce_wake_up(u32, u32, u32, u32);

static u32 rs = 0xd3a2646cu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 24
struct hlog { unsigned int n; u32 e[LOGN][4]; };
static struct hlog vl, ol, *cur;
static u32 cret;
static void hrec(u32 k, u32 a, u32 b, u32 c)
{
	if (cur->n < LOGN) { cur->e[cur->n][0] = k; cur->e[cur->n][1] = a; cur->e[cur->n][2] = b; cur->e[cur->n][3] = c; }
	cur->n++;
}
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; hrec(1, a0, a1, a2); return 0; }
u64 tisp_lce_write_curve(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(2, a0, 0, 0); return 0; }
u64 tisp_lce_write_ctrl_reg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(3, a0, 0, 0); return cret; }
static void o_wr(u32 ch, u32 reg, u32 val) { hrec(1, ch, reg, val); }
static void o_wc(u32 ch) { hrec(2, ch, 0, 0); }
static u32 o_cr(u32 ch) { hrec(3, ch, 0, 0); return cret; }
static const struct t32_lcw_ops ops = { o_wr, o_wc, o_cr };

#define STB (0x20000 + 9948)

int main(void)
{
	u8 *st = t32o_alloc(STB), tb[4 * 36];
	u32 tab[4], geom[16], bank, i;
	unsigned long n, N = 20000;

	for (n = 0; n < N; n++) {
		struct t32_lcw w;
		u32 ch = rnd() & 3, rv, ro;
		u8 sh = (rnd() % 3 == 0) ? 0 : ((rnd() & 1) ? 1 : rnd() | 2);

		for (i = 0; i < sizeof(tb); i++) tb[i] = rnd();
		for (i = 0; i < 16; i++) geom[i] = rnd();
		for (i = 0x20000 + 9940; i < STB; i++) st[i] = rnd();
		for (i = 0; i < 4; i++) tab[i] = T32O_PTR(st);
		bank = (rnd() & 1) ? rnd() & 3 : rnd();
		cret = rnd();

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		w.ops = &ops; w.buf = (const struct t32_lci_buf *)tb; w.lce_st = tab; w.shadow = &sh; w.bank = &bank; w.geom = geom;
		ro = t32_lce_wake_up(&w, ch);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_e1db8, tb, sizeof(tb)); memcpy(t32_data_e1da8, tab, 16);
		t32_data_e1d7c[0] = bank; memcpy(t32_data_e1d80, geom, 40);
		t32_data_d03a0[0] = sh;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_wake_up, ch, rnd(), rnd(), rnd(), 0);
		t32_data_d03a0[0] = 0;

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e))) {
			printf("t32_lcewake_oracle: FAIL case %lu ch %u sh %u: ret %#x/%#x calls %u/%u\n", n, ch, sh, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x / %u %#x %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3],
				       ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3]);
			return 1;
		}
	}
	printf("t32_lcewake_oracle: PASS %lu cases\n", N);
	return 0;
}
