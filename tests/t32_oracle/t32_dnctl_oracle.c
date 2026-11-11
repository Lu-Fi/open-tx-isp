// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_day_or_night_s_ctrl (tx_isp_t32_blkrest.h) against
 * the vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random channel 0 / 1 and index 0 .. 5 (both with high bits), mode (0, 1,
 * other), bin present / missing, keep flag e401c, d03a0, channel state
 * blocks and sequence words.  Compared: return value, every callee in order
 * with its arguments (lut copy, register writes, refresh, both debug prints
 * with their format / line / index), every byte of the working tuning
 * (0xf238 B), da850, dc284, e401c, f32f8 / f32e8.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

u32 t32_data_dd0b4[12], t32_data_f32d8[4], t32_data_f32f8[4], t32_data_f32e8[4];
u8 t32_data_e401c[4], t32_data_cb724[4], t32_data_c2fe4[4], t32_data_cb6e4[4];
extern u64 tisp_day_or_night_s_ctrl(u32, u32, u32, u32);

static u32 rs = 0x27d4eb2fu;
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
u64 tisp_lut_params_copy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hrec(1, a0, a1, 0); return 0; }
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; hrec(2, a0, a1, a2); return 0; }
u64 tisp_day_or_night_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hrec(3, a0, a1, 0); return 0; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a2;
	if (a1 == T32O_PTR(t32_data_cb6e4)) hrec(4, a0, a3, a4);
	else hrec(5, a0, a1 == T32O_PTR(t32_data_cb724), a3);
	return 0;
}
static void o_lut(u32 ch, u32 idx) { hrec(1, ch, idx, 0); }
static void o_wr(u32 ch, u32 reg, u32 val) { hrec(2, ch, reg, val); }
static void o_ref(u32 ch, u32 dn) { hrec(3, ch, dn, 0); }
static void o_nb(u32 idx) { hrec(4, 2, 228, idx); }
static void o_bm(void) { hrec(5, 2, 1, 272); }
static const struct t32_dnc_ops ops = { o_lut, o_wr, o_ref, o_nb, o_bm };

#define TUNB 0xf238
static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	u8 *src = t32o_alloc(TUNB), *tun[2] = { t32o_alloc(TUNB), t32o_alloc(TUNB) };
	static u8 st[2][704], pipe[2][1808];
	u32 bins[12], tw[2][4], sq[4], s1[2][4], s2[2][4], i;
	u8 keep[2], sh;
	unsigned long n, N = 6000, per[4] = { 0 };

	fill(src, TUNB);
	for (n = 0; n < N; n++) {
		struct t32_dnc d;
		u32 ch = rnd() & 1, idx = rnd() % 6, r = rnd() % 5, mode = r < 2 ? r : (r == 2 ? rnd() : r - 2);
		u32 a0 = ch | (rnd() & 0xffffff00u), a2 = idx | (rnd() & 0xffffff00u), rv, ro;

		for (i = 0; i < 12; i++) bins[i] = (rnd() % 5) ? T32O_PTR(src) : 0;
		src[rnd() % TUNB] = rnd();
		fill(tun[0], TUNB); memcpy(tun[1], tun[0], TUNB);
		fill(st[0], 704); memcpy(st[1], st[0], 704);
		fill(pipe[0], 1808); memcpy(pipe[1], pipe[0], 1808);
		for (i = 0; i < 4; i++) {
			tw[0][i] = T32O_PTR(tun[0]); tw[1][i] = T32O_PTR(tun[1]);
			sq[i] = rnd(); s1[0][i] = s1[1][i] = rnd(); s2[0][i] = s2[1][i] = rnd();
		}
		keep[0] = keep[1] = (rnd() & 1) ? 0 : rnd();
		sh = (rnd() % 3 == 0) ? 0 : ((rnd() & 1) ? 1 : rnd() | 2);

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		d.ops = &ops; d.bins = bins; d.tuning = tw[1]; d.st = st[1]; d.pipe = pipe[1]; d.keep = &keep[1];
		d.shadow = &sh; d.seq = sq; d.seq1 = s1[1]; d.seq2 = s2[1];
		ro = t32_day_or_night_s_ctrl(&d, a0, mode, a2);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_dd0b4, bins, 48); memcpy(t32_data_dd0a4, tw[0], 16);
		memcpy(t32_data_da850, st[0], 704); memcpy(t32_data_dc284, pipe[0], 1808);
		t32_data_e401c[0] = keep[0]; t32_data_d03a0[0] = sh;
		memcpy(t32_data_f32d8, sq, 16); memcpy(t32_data_f32f8, s1[0], 16); memcpy(t32_data_f32e8, s2[0], 16);
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_day_or_night_s_ctrl, a0, mode, a2, rnd(), 0);
		t32_data_d03a0[0] = 0;

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(tun[0], tun[1], TUNB) ||
		    memcmp(t32_data_da850, st[1], 704) || memcmp(t32_data_dc284, pipe[1], 1808) || t32_data_e401c[0] != keep[1] ||
		    memcmp(t32_data_f32f8, s1[1], 16) || memcmp(t32_data_f32e8, s2[1], 16)) {
			printf("t32_dnctl_oracle: FAIL case %lu ch %#x mode %#x idx %#x: ret %#x/%#x calls %u/%u\n", n, a0, mode, a2, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x / %u %#x %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3],
				       ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3]);
			return 1;
		}
		per[!bins[ch * 6 + idx] ? 3 : (mode < 2 ? mode : 2)]++;
	}
	printf("t32_dnctl_oracle: PASS %lu cases (day %lu, night %lu, other mode %lu, no bin %lu)\n", N, per[0], per[1], per[2], per[3]);
	return 0;
}
