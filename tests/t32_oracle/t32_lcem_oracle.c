// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_lcem.h (tisp_lce_wdr_en, _top_change_state,
 * _dn_params_refresh, _get_hist_data, _get_sum_data) against the vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case: four
 * channel states (0x22850 B) and a statistics buffer filled with random
 * bytes, the same on both sides, a random parameter section (top byte 1 /
 * 0 / other), random tuning bases, random channel and enable value.
 * Compared: every byte of all four channel states, the hook calls
 * (tisp_lce_par_to_used / tisp_lce_curve_init_default, stubbed here: they
 * are checked by t32_lce_oracle) in order with the channel, and the return
 * value of the functions that have one.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lcem.h"

#define STB 0x22850
#define STATB 40960
extern u64 tisp_lce_wdr_en(u32, u32, u32, u32), tisp_lce_top_change_state(u32, u32, u32, u32),
	   tisp_lce_dn_params_refresh(u32, u32, u32, u32), tisp_lce_get_hist_data(u32, u32, u32, u32),
	   tisp_lce_get_sum_data(u32, u32, u32, u32);

static u32 rs = 0x9e3779b9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 8
struct hlog { unsigned int n; u32 e[LOGN][2]; };
static struct hlog vl, ol, *cur;
static void hrec(u32 kind, u32 ch)
{
	if (cur->n < LOGN) { cur->e[cur->n][0] = kind; cur->e[cur->n][1] = ch; }
	cur->n++;
}

u64 tisp_lce_par_to_used(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	hrec(1, a0);
	return 0x1111;
}
u64 tisp_lce_curve_init_default(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	hrec(2, a0);
	return 0x2222;
}
static void o_par(u32 ch) { hrec(1, ch); }
static u32 o_curve(u32 ch) { hrec(2, ch); return 0x2222; }
static const struct t32_lcem_ops ops = { o_par, o_curve };

int main(void)
{
	u8 *vs[4], *os[4], *stats = t32o_alloc(STATB), *par = t32o_alloc(512);
	unsigned long n, N = 3000, per[5] = { 0 };
	u32 ch, i;

	for (ch = 0; ch < 4; ch++) {
		vs[ch] = t32o_alloc(STB);
		os[ch] = t32o_alloc(STB);
	}
	for (n = 0; n < N; n++) {
		struct t32_lcem l;
		int fn = (int)(rnd() % 5);
		u32 c = rnd() & 3, en = rnd(), rv = 0, ro = 0;
		u32 tun[4];

		for (i = 0; i < STATB; i += 4) { u32 r = rnd(); memcpy(stats + i, &r, 4); }
		for (i = 0; i < 512; i++) par[i] = rnd();
		par[T32_LCEM_PAR_TOP] = (n % 3 == 0) ? 1 : (n % 3 == 1 ? 0 : par[T32_LCEM_PAR_TOP]);
		for (ch = 0; ch < 4; ch++) {
			if (n < 4 || (rnd() & 15) == 0)
				for (i = 0; i < STB; i += 4) { u32 r = rnd(); memcpy(vs[ch] + i, &r, 4); }
			else
				for (i = 0; i < 64; i++) vs[ch][rnd() % STB] = rnd();
			t32_lcem_wr32(vs[ch], T32O_PTR(par));
			tun[ch] = rnd();
			memcpy(os[ch], vs[ch], STB);
		}

		t32o_reset();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		l.ops = &ops;
		for (ch = 0; ch < 4; ch++) l.st[ch] = os[ch];
		l.tuning = tun;
		switch (fn) {
		case 0: ro = t32_lce_wdr_en(&l, c, en & 0xff); break;
		case 1: t32_lce_top_change_state(&l, c); break;
		case 2: ro = t32_lce_dn_params_refresh(&l, c); break;
		case 3: t32_lce_get_hist_data(&l, c, stats); break;
		default: t32_lce_get_sum_data(&l, c, stats); break;
		}

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		for (ch = 0; ch < 4; ch++) {
			t32_data_e1da8[ch] = T32O_PTR(vs[ch]);
			t32_data_dd0a4[ch] = tun[ch];
		}
		switch (fn) {
		case 0: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_wdr_en, c, en, rnd(), rnd(), 0); break;
		case 1: t32o_call((t32o_fn)(void (*)(void))tisp_lce_top_change_state, c, rnd(), rnd(), rnd(), 0); break;
		case 2: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_dn_params_refresh, c, rnd(), rnd(), rnd(), 0); break;
		case 3: t32o_call((t32o_fn)(void (*)(void))tisp_lce_get_hist_data, c, T32O_PTR(stats), rnd(), rnd(), 0); break;
		default: t32o_call((t32o_fn)(void (*)(void))tisp_lce_get_sum_data, c, T32O_PTR(stats), rnd(), rnd(), 0); break;
		}

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e))) {
			printf("t32_lcem_oracle: FAIL case %lu fn %d ch %u: ret %#x/%#x hooks %u/%u\n", n, fn, c, rv, ro, vl.n, ol.n);
			return 1;
		}
		for (ch = 0; ch < 4; ch++)
			if (memcmp(vs[ch], os[ch], STB)) {
				for (i = 0; i < STB && vs[ch][i] == os[ch][i]; i++)
					;
				printf("t32_lcem_oracle: FAIL case %lu fn %d ch %u: state %u byte %#x vendor %#x ours %#x\n",
				       n, fn, c, ch, i, vs[ch][i], os[ch][i]);
				return 1;
			}
		per[fn]++;
	}
	printf("t32_lcem_oracle: PASS %lu cases (wdr_en %lu, top_change_state %lu, dn_params_refresh %lu, get_hist_data %lu, get_sum_data %lu)\n",
	       N, per[0], per[1], per[2], per[3], per[4]);
	return 0;
}
