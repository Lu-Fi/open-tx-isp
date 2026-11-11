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
 * value of the functions that have one.  get_show_data / param_array_get /
 * param_array_set: show mode 0 / not, zone row < 18 / column < 32, WDR
 * byte equal / different to the t32_data_e20d8 flag (0 / 1 / other), the
 * compress / Gauss / write_ctrl_reg callees stubbed (pointer arguments
 * as state offsets, the Gauss source bytes at the call), output buffer and
 * length compared.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lcem.h"

#define STB 0x22850
#define STATB 40960
#define PARB 720
extern u64 tisp_lce_wdr_en(u32, u32, u32, u32), tisp_lce_top_change_state(u32, u32, u32, u32),
	   tisp_lce_dn_params_refresh(u32, u32, u32, u32), tisp_lce_get_hist_data(u32, u32, u32, u32),
	   tisp_lce_get_sum_data(u32, u32, u32, u32), tisp_lce_get_show_data(u32, u32, u32, u32),
	   tisp_lce_param_array_get(u32, u32, u32, u32), tisp_lce_param_array_set(u32, u32, u32, u32),
	   tisp_lce_tgain_update(u32, u32, u32, u32), tisp_lce_deinit(u32, u32, u32, u32);
u8 t32_data_e1850[20] __attribute__((weak)), t32_data_e1864[20] __attribute__((weak)),
   t32_data_e1878[20] __attribute__((weak));		/* .bss: LCE tables */
extern u8 t32_data_e188c[64];
extern u32 t32_data_d6c64[4];
u8 t32_data_e20d8[4] __attribute__((weak));	/* .bss: LCE WDR flag */

static u32 rs = 0x9e3779b9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 8
struct hlog { unsigned int n; u32 e[LOGN][6]; };
static struct hlog vl, ol, *cur;
static uintptr_t side_st, side_par;
static void hrec5(u32 kind, u32 a, u32 b, u32 c, u32 d)
{
	if (cur->n < LOGN) {
		cur->e[cur->n][0] = kind; cur->e[cur->n][1] = a; cur->e[cur->n][2] = b;
		cur->e[cur->n][3] = c; cur->e[cur->n][4] = d; cur->e[cur->n][5] = 0;
	}
	cur->n++;
}
static void hrec(u32 kind, u32 ch) { hrec5(kind, ch, 0, 0, 0); }
static u32 off(uintptr_t p)	/* a state pointer as offset, the parameter section tagged */
{
	if (p >= side_st && p < side_st + 0x22850) return (u32)(p - side_st);
	if (p >= side_par && p < side_par + PARB) return 0x80000000u | (u32)(p - side_par);
	return 0xdead0000u;
}
static void c_part1(uintptr_t s, uintptr_t d) { hrec5(3, off(s), off(d), 0, 0); }
static void c_comp(u32 n, uintptr_t s, uintptr_t d) { hrec5(4, n, off(s), off(d), 0); }
static void c_gauss(u32 ch, uintptr_t s, uintptr_t d, u32 n)
{
	u32 b = 0;

	memcpy(&b, (const void *)s, 3);
	hrec5(5, ch, off(s), off(d), n);
	if (cur->n <= LOGN) cur->e[cur->n - 1][5] = b;
}
u64 lce_compress_data_part_1(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; c_part1(a0, a1); return 0; }
u64 lce_compress_data(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; c_comp(a0, a1, a2); return 0; }
u64 tisp_lce_gauss_wgt(u32 a0, u32 a1, u32 a2, u32 a3) { c_gauss(a0, a1, a2, a3); return 0; }
u64 tisp_lce_write_ctrl_reg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(6, a0); return 0; }
static void o_part1(const u8 *s, u8 *d) { c_part1((uintptr_t)s, (uintptr_t)d); }
static void o_comp(u32 n, const u8 *s, u8 *d) { c_comp(n, (uintptr_t)s, (uintptr_t)d); }
static void o_gauss(u32 ch, const u8 *s, u8 *d, u32 n) { c_gauss(ch, (uintptr_t)s, (uintptr_t)d, n); }
static void o_wcr(u32 ch) { hrec(6, ch); }
u64 tisp_lce_tgain_interp_strength(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(7, a0); return 0; }
static void o_istr(u32 ch) { hrec(7, ch); }
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(8, a0); return 0; }
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec5(9, off(a0), 0, 0, 0); return 0; }
static void o_kfree(void *p) { hrec(8, T32O_PTR(p)); }
static void o_vfree(void *p) { hrec5(9, off((uintptr_t)p), 0, 0, 0); }

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
static const struct t32_lcem_ops ops = { o_par, o_curve, o_part1, o_comp, o_gauss, o_wcr, o_istr, o_kfree, o_vfree };

int main(void)
{
	u8 *vs[4], *os[4], *stats = t32o_alloc(STATB), *par = t32o_alloc(PARB), *opar = t32o_alloc(PARB);
	unsigned long n, N = 6000, per[10] = { 0 };
	u8 *vout = t32o_alloc(1500), *oout = t32o_alloc(1500), *in = t32o_alloc(712);
	u32 *vlen = t32o_alloc(4), *olen = t32o_alloc(4);
	u32 ch, i;

	for (ch = 0; ch < 4; ch++) {
		vs[ch] = t32o_alloc(STB);
		os[ch] = t32o_alloc(STB);
	}
	for (n = 0; n < N; n++) {
		struct t32_lcem l;
		int fn = (int)(rnd() % 10);
		u8 bufs0[144], obufs[144], tabs0[4][24], otabs[4][24], sz0[4], osz[4];
		u8 *vtabs[4] = { t32_data_e188c, t32_data_e1878, t32_data_e1864, t32_data_e1850 };
		u32 vst0 = 0;
		u32 glo = rnd(), ghi = (rnd() & 3) ? 0 : rnd(), elo = rnd(), ehi = (rnd() & 3) ? 0 : rnd();
		u8 wflag = (rnd() % 3 == 0) ? 0 : ((rnd() & 1) ? 1 : rnd()), oflag;
		u32 c = rnd() & 3, en = rnd(), rv = 0, ro = 0;
		u32 tun[4];

		for (i = 0; i < STATB; i += 4) { u32 r = rnd(); memcpy(stats + i, &r, 4); }
		for (i = 0; i < PARB; i++) par[i] = rnd();
		par[T32_LCEM_PAR_MODE] = rnd() % 3;
		par[T32_LCEM_PAR_TOP] = (n % 3 == 0) ? 1 : (n % 3 == 1 ? 0 : par[T32_LCEM_PAR_TOP]);
		for (ch = 0; ch < 4; ch++) {
			if (n < 4 || (rnd() & 15) == 0)
				for (i = 0; i < STB; i += 4) { u32 r = rnd(); memcpy(vs[ch] + i, &r, 4); }
			else
				for (i = 0; i < 64; i++) vs[ch][rnd() % STB] = rnd();
			t32_lcem_wr32(vs[ch], T32O_PTR(par));
			tun[ch] = rnd();
			vs[ch][T32_LCEM_SHOW_ROW] = rnd() % 18;
			vs[ch][T32_LCEM_SHOW_COL] = rnd() % 32;
			if (rnd() & 1) vs[ch][T32_LCEM_SHOW_MODE] = 0;
			if (rnd() & 1) vs[ch][T32_LCEM_DAYNIGHT] = 0;
			{ u32 r = rnd() % 3; vs[ch][T32_LCEM_WDR] = r == 0 ? wflag : (r == 1 ? 0 : rnd()); }
			{	/* last gain / EV near the new values */
				u32 r = rnd() % 4, lg = r == 0 ? glo : r == 1 ? glo + (rnd() % 400) - 200 : rnd();
				u32 le = r == 0 ? elo : r == 2 ? elo + (rnd() % 3) - 1 : rnd();

				t32_lcem_wr32(vs[ch] + T32_LCEM_LAST_GAIN, lg);
				t32_lcem_wr32(vs[ch] + T32_LCEM_LAST_EV, le);
			}
			memcpy(os[ch], vs[ch], STB);
			t32_lcem_wr32(os[ch], T32O_PTR(opar));	/* our own copy of the parameter section */
		}
		memcpy(opar, par, PARB);
		for (i = 0; i < 712; i++) in[i] = rnd();
		for (i = 0; i < 144; i++) bufs0[i] = rnd();
		for (i = 12; i < 144; i += 4) if (rnd() & 1) memset(bufs0 + i, 0, 4);
		memcpy(obufs, bufs0, 144);
		for (i = 0; i < 4 * 24; i++) tabs0[i / 24][i % 24] = rnd();
		memcpy(otabs, tabs0, sizeof(otabs));
		for (i = 0; i < 4; i++) sz0[i] = osz[i] = rnd();
		for (i = 0; i < 1500; i++) vout[i] = oout[i] = rnd();
		*vlen = *olen = rnd();
		oflag = wflag;

		t32o_reset();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		l.ops = &ops;
		for (ch = 0; ch < 4; ch++) l.st[ch] = os[ch];
		l.tuning = tun;
		l.wdr_flag = &oflag;
		l.bufs = obufs; l.size_par = osz;
		for (i = 0; i < 4; i++) l.tabs[i] = otabs[i];
		if (fn == 9 && (rnd() & 3) == 0) l.st[c] = NULL, vst0 = 1;
		side_st = (uintptr_t)os[c]; side_par = (uintptr_t)opar;
		switch (fn) {
		case 0: ro = t32_lce_wdr_en(&l, c, en & 0xff); break;
		case 1: t32_lce_top_change_state(&l, c); break;
		case 2: ro = t32_lce_dn_params_refresh(&l, c); break;
		case 3: t32_lce_get_hist_data(&l, c, stats); break;
		case 4: t32_lce_get_sum_data(&l, c, stats); break;
		case 5: t32_lce_get_show_data(&l, c); break;
		case 6: ro = t32_lce_param_array_get(&l, c, oout, olen); break;
		case 7: ro = t32_lce_param_array_set(&l, c, in); break;
		case 8: ro = t32_lce_tgain_update(&l, c, glo, ghi, elo, ehi); break;
		default: ro = t32_lce_deinit(&l, c); break;
		}

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		t32_data_e20d8[0] = wflag;
		memcpy(t32_data_e1db8, bufs0, 144);
		memcpy(t32_data_d6c64, sz0, 4);
		for (i = 0; i < 4; i++) memcpy(vtabs[i], tabs0[i], i ? 20 : 24);
		side_st = (uintptr_t)vs[c]; side_par = (uintptr_t)par;
		for (ch = 0; ch < 4; ch++) {
			t32_data_e1da8[ch] = T32O_PTR(vs[ch]);
			t32_data_dd0a4[ch] = tun[ch];
		}
		switch (fn) {
		case 0: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_wdr_en, c, en, rnd(), rnd(), 0); break;
		case 1: t32o_call((t32o_fn)(void (*)(void))tisp_lce_top_change_state, c, rnd(), rnd(), rnd(), 0); break;
		case 2: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_dn_params_refresh, c, rnd(), rnd(), rnd(), 0); break;
		case 3: t32o_call((t32o_fn)(void (*)(void))tisp_lce_get_hist_data, c, T32O_PTR(stats), rnd(), rnd(), 0); break;
		case 4: t32o_call((t32o_fn)(void (*)(void))tisp_lce_get_sum_data, c, T32O_PTR(stats), rnd(), rnd(), 0); break;
		case 5: t32o_call((t32o_fn)(void (*)(void))tisp_lce_get_show_data, c, rnd(), rnd(), rnd(), 0); break;
		case 6: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_param_array_get, c, T32O_PTR(vout), T32O_PTR(vlen), rnd(), 0); break;
		case 7: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_param_array_set, c, T32O_PTR(in), rnd(), rnd(), 0); break;
		case 8: t32o_arg5 = ehi;
			rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_tgain_update, c, rnd(), glo, ghi, elo); break;
		default: if (vst0) t32_data_e1da8[c] = 0;
			rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_deinit, c, rnd(), rnd(), rnd(), 0); break;
		}

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(vout, oout, 1500) ||
		    *vlen != *olen || t32_data_e20d8[0] != oflag) {
			printf("t32_lcem_oracle: FAIL case %lu fn %d ch %u: ret %#x/%#x hooks %u/%u\n", n, fn, c, rv, ro, vl.n, ol.n);
			return 1;
		}
		if (fn == 9) {	/* the state pointer table and the freed objects */
			for (i = 0; i < 4; i++)
				if ((t32_data_e1da8[i] == 0) != (l.st[i] == NULL) ||
				    memcmp(vtabs[i], otabs[i], i ? 20 : 24)) {
					printf("t32_lcem_oracle: FAIL case %lu deinit: table / state %u\n", n, i);
					return 1;
				}
			if (memcmp(t32_data_e1db8, obufs, 144) || memcmp(t32_data_d6c64, osz, 4)) {
				printf("t32_lcem_oracle: FAIL case %lu deinit: buffers / sizes\n", n);
				return 1;
			}
			per[fn]++;
			continue;
		}
		if (memcmp(par, opar, PARB)) {
			printf("t32_lcem_oracle: FAIL case %lu fn %d: parameter section differs\n", n, fn);
			return 1;
		}
		for (ch = 0; ch < 4; ch++)
			if (t32_lcem_rd32(os[ch]) == T32O_PTR(opar))
				t32_lcem_wr32(os[ch], T32O_PTR(par));
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
	printf("t32_lcem_oracle: PASS %lu cases (wdr_en %lu, top_change_state %lu, dn_params_refresh %lu, get_hist_data %lu, get_sum_data %lu, get_show_data %lu, param_array_get %lu, param_array_set %lu, tgain_update %lu, deinit %lu)\n",
	       N, per[0], per[1], per[2], per[3], per[4], per[5], per[6], per[7], per[8], per[9]);
	return 0;
}
