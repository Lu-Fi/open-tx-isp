// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_ae_par_update_trig (tx_isp_t32_blkrest.h) against the
 * vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random channel, AE context (WDR word 1 / 0 / random),
 * tuning, parameter and context blocks, sensor attributes (dd2d4) and
 * d03a0 (0 / 1 / other).  Compared: return value, every callee in order with
 * its channel and, for the two sensor updates, all 232 bytes of the
 * by-value attribute argument (a1..a3 + 55 stack words), every byte of the
 * parameter blocks.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
extern u8 t32_data_dd2d4[0x400];
extern u64 tisp_ae_par_update_trig(u32, u32, u32, u32);

static u32 rs = 0x1b873593u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 8
struct hlog { unsigned int n; u32 k[LOGN], ch[LOGN]; u8 attr[LOGN][232]; };
static struct hlog vl, ol, *cur;
static u32 fps_ret;
static void hrec(u32 k, u32 ch, const u8 *attr)
{
	if (cur->n < LOGN) { cur->k[cur->n] = k; cur->ch[cur->n] = ch; if (attr) memcpy(cur->attr[cur->n], attr, 232); }
	cur->n++;
}
#define SENS(name, k) \
u64 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 s0, u32 s1, u32 s2, u32 s3, u32 s4, u32 s5, u32 s6, u32 s7, u32 s8, u32 s9, u32 s10, u32 s11, u32 s12, u32 s13, u32 s14, u32 s15, u32 s16, u32 s17, u32 s18, u32 s19, u32 s20, u32 s21, u32 s22, u32 s23, u32 s24, u32 s25, u32 s26, u32 s27, u32 s28, u32 s29, u32 s30, u32 s31, u32 s32, u32 s33, u32 s34, u32 s35, u32 s36, u32 s37, u32 s38, u32 s39, u32 s40, u32 s41, u32 s42, u32 s43, u32 s44, u32 s45, u32 s46, u32 s47, u32 s48, u32 s49, u32 s50, u32 s51, u32 s52, u32 s53, u32 s54); \
u64 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 s0, u32 s1, u32 s2, u32 s3, u32 s4, u32 s5, u32 s6, u32 s7, u32 s8, u32 s9, u32 s10, u32 s11, u32 s12, u32 s13, u32 s14, u32 s15, u32 s16, u32 s17, u32 s18, u32 s19, u32 s20, u32 s21, u32 s22, u32 s23, u32 s24, u32 s25, u32 s26, u32 s27, u32 s28, u32 s29, u32 s30, u32 s31, u32 s32, u32 s33, u32 s34, u32 s35, u32 s36, u32 s37, u32 s38, u32 s39, u32 s40, u32 s41, u32 s42, u32 s43, u32 s44, u32 s45, u32 s46, u32 s47, u32 s48, u32 s49, u32 s50, u32 s51, u32 s52, u32 s53, u32 s54) \
{ \
	u32 w[58] = { a1, a2, a3, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15, s16, s17, s18, s19, s20, s21, s22, s23, s24, s25, s26, s27, s28, s29, s30, s31, s32, s33, s34, s35, s36, s37, s38, s39, s40, s41, s42, s43, s44, s45, s46, s47, s48, s49, s50, s51, s52, s53, s54 }; \
	hrec(k, a0, (const u8 *)w); \
	return 0; \
}
SENS(tisp_ae_sensor_par_ctrls_update, 1)
SENS(tisp_ae_short_sensor_par_ctrls_update, 2)
u64 tisp_ae_par_calc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(3, a0, NULL); return 0; }
u64 tisp_ae_clac_deflicker_cfg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(4, a0, NULL); return 0; }
u64 tisp_set_ae_sensor_fps(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(5, a0, NULL); return fps_ret; }
static void o_su(u32 ch, const u8 *attr) { hrec(1, ch, attr); }
static void o_ssu(u32 ch, const u8 *attr) { hrec(2, ch, attr); }
static void o_pc(u32 ch) { tisp_ae_par_calc(ch, 0, 0, 0); }
static void o_dc(u32 ch) { tisp_ae_clac_deflicker_cfg(ch, 0, 0, 0); }
static u32 o_fps(u32 ch) { return (u32)tisp_set_ae_sensor_fps(ch, 0, 0, 0); }
static const struct t32_aeu_ops ops = { o_su, o_ssu, o_pc, o_dc, o_fps };

#define TUNB 12848
#define PARB 1300
#define BLKB 1168
static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	u8 *tun = t32o_alloc(TUNB), *blk = t32o_alloc(BLKB), *ctx = t32o_alloc(16), *par[2], sen[0x400];
	u32 ctxw[4], parw[2][4], i, s;
	unsigned long n, N = 20000, wdr = 0;

	for (s = 0; s < 2; s++) par[s] = t32o_alloc(PARB);
	for (n = 0; n < N; n++) {
		struct t32_aeu a;
		u32 ch = rnd() & 3, r = rnd() % 3, wd = r == 0 ? 1 : (r == 1 ? 0 : rnd()), rv, ro;
		u8 sh = (rnd() % 3 == 0) ? 1 : ((rnd() & 1) ? 0 : rnd() | 2);

		fill(tun, TUNB); fill(blk, BLKB); fill(par[0], PARB); memcpy(par[1], par[0], PARB); fill(sen, sizeof(sen));
		memcpy(ctx, (u32[3]){ T32O_PTR(blk), T32O_PTR(tun), wd }, 12);
		for (i = 0; i < 4; i++) { ctxw[i] = T32O_PTR(ctx); parw[0][i] = T32O_PTR(par[0]); parw[1][i] = T32O_PTR(par[1]); }
		fps_ret = rnd();

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		a.ops = &ops; a.ctx = ctxw; a.par = parw[1]; a.sensor = sen; a.shadow = &sh;
		ro = t32_ae_par_update_trig(&a, ch);

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_e15a8, ctxw, 16);
		memcpy(t32_data_e1588, parw[0], 16);
		memcpy(t32_data_dd2d4, sen, sizeof(sen));
		t32_data_d03a0[0] = sh;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_par_update_trig, ch, rnd(), rnd(), rnd(), 0);
		t32_data_d03a0[0] = 0;

		if (rv != ro || vl.n != ol.n || memcmp(vl.k, ol.k, sizeof(vl.k)) || memcmp(vl.ch, ol.ch, sizeof(vl.ch)) ||
		    memcmp(vl.attr, ol.attr, sizeof(vl.attr)) || memcmp(par[0], par[1], PARB)) {
			printf("t32_aeupd_oracle: FAIL case %lu ch %#x wdr %#x sh %u: ret %#x/%#x calls %u/%u\n", n, ch, wd, sh, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++) printf("  %u: %u %#x / %u %#x\n", i, vl.k[i], vl.ch[i], ol.k[i], ol.ch[i]);
			return 1;
		}
		wdr += wd == 1;
	}
	printf("t32_aeupd_oracle: PASS %lu cases (%lu WDR)\n", N, wdr);
	return 0;
}
