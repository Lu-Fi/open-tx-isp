// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_blkglue.h (tisp_clm_ct_update,
 * tisp_clm_refresh_by_csc, tisp_lce_remove, tisp_ae_get_info) against the
 * vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random channel, CT, interpolation result (0 / not), d03a0 = 0 / 1 /
 * other, LCE states and buffer pointers (each NULL or set), AE blocks
 * (random bytes, the same on both sides).  Compared: return values, every
 * callee / free call in order with its arguments, the LCE pointer tables
 * after remove, every byte of the AE tuning block and the 30-byte reply
 * (canaries after it).  The callees are stubbed here (checked by their
 * block oracles).  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkglue.h"

extern u32 t32_data_e28d0[4], t32_data_e1588[4], t32_data_e15a8[4];
extern u64 tisp_clm_ct_update(u32, u32, u32, u32), tisp_clm_refresh_by_csc(u32, u32, u32, u32),
	   tisp_lce_remove(u32, u32, u32, u32), tisp_ae_get_info(u32, u32, u32, u32);

static u32 rs = 0x3c6ef372u ^ 0x1234u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 24
struct hlog { unsigned int n; u32 e[LOGN][4]; };
static struct hlog vl, ol, *cur;
static void hrec(u32 k, u32 a, u32 b, u32 c)
{
	if (cur->n < LOGN) { cur->e[cur->n][0] = k; cur->e[cur->n][1] = a; cur->e[cur->n][2] = b; cur->e[cur->n][3] = c; }
	cur->n++;
}
static u32 interp_ret;
u64 tisp_clm_ct_interp(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; hrec(1, a0, a1, a2); return interp_ret; }
u64 tisp_clm_write_lut(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(2, a0, 0, 0); return 0; }
u64 tisp_clm_lut_write_lut(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(3, a0, 0, 0); return 0; }
u64 tisp_csc_api_get(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hrec(4, a0, a1, 0); return 0; }
u64 tisp_clm_write_csc_para(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(5, a0, 0, 0); return 0; }
u64 tisp_clm_calc_para_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(6, a0, 0, 0); return 0; }
u64 tisp_clm_write_reg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(7, a0, 0, 0); return 0; }
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(8, a0, 0, 0); return 0; }
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(9, a0, 0, 0); return 0; }

static u32 o_interp(u32 ch, u32 ct, u32 z) { return (u32)tisp_clm_ct_interp(ch, ct, z, 0); }
static void o_wl(u32 ch) { tisp_clm_write_lut(ch, 0, 0, 0); }
static void o_lwl(u32 ch) { tisp_clm_lut_write_lut(ch, 0, 0, 0); }
static void o_csc(u32 ch, void *a) { tisp_csc_api_get(ch, T32O_PTR(a), 0, 0); }
static void o_wcp(u32 ch) { tisp_clm_write_csc_para(ch, 0, 0, 0); }
static void o_cpr(u32 ch) { tisp_clm_calc_para_refresh(ch, 0, 0, 0); }
static void o_wr(u32 ch) { tisp_clm_write_reg(ch, 0, 0, 0); }
static void o_kfree(void *p) { private_kfree(T32O_PTR(p), 0, 0, 0); }
static void o_vfree(void *p) { private_vfree(T32O_PTR(p), 0, 0, 0); }
static const struct t32_blkglue_ops ops = { o_interp, o_wl, o_lwl, o_csc, o_wcp, o_cpr, o_wr, o_kfree, o_vfree };

#define TUNB 8640
#define PARB 1700
#define STB 1744
static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	u8 *tun[2][4], *par[4], *st[4], *ctx[2][4], *vinfo = t32o_alloc(40), *oinfo = t32o_alloc(40);
	unsigned long n, N = 20000, per[4] = { 0 };
	u32 c, s, i;

	for (c = 0; c < 4; c++) {
		for (s = 0; s < 2; s++) { tun[s][c] = t32o_alloc(TUNB); ctx[s][c] = t32o_alloc(8); }
		par[c] = t32o_alloc(PARB); st[c] = t32o_alloc(STB);
	}
	for (n = 0; n < N; n++) {
		struct t32_blkglue g;
		int fn = (int)(rnd() % 4);
		u32 ch = rnd() & 3, a0 = (fn == 1) ? ch : ch | (rnd() & 0xffffff00u), ct = rnd(), r = rnd() % 3;
		u32 clm[4], lce[2][4], ctxw[2][4], parw[4], rv = 0, ro = 0;
		u8 sh[4], lbuf[2][144];

		sh[0] = r == 0 ? 0 : (r == 1 ? 1 : rnd() | 1);
		interp_ret = (rnd() & 1) ? 0 : rnd();
		for (c = 0; c < 4; c++) {
			clm[c] = rnd();
			lce[0][c] = lce[1][c] = (rnd() & 3) ? rnd() | 1 : 0;
			fill(tun[0][c], TUNB); memcpy(tun[1][c], tun[0][c], TUNB);
			fill(par[c], PARB); fill(st[c], STB);
			for (s = 0; s < 2; s++) {
				u32 w[2] = { T32O_PTR(st[c]), T32O_PTR(tun[s][c]) };

				memcpy(ctx[s][c], w, 8);
				ctxw[s][c] = T32O_PTR(ctx[s][c]);
			}
			parw[c] = T32O_PTR(par[c]);
		}
		fill(lbuf[0], 144);
		for (i = 12; i < 144; i += 4)
			if (rnd() & 1) memset(lbuf[0] + i, 0, 4);
		memcpy(lbuf[1], lbuf[0], 144);
		fill(vinfo, 40); memcpy(oinfo, vinfo, 40);

		t32o_reset();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		g.ops = &ops; g.shadow = sh; g.clm_st = clm; g.lce_st = lce[1]; g.lce_buf = lbuf[1];
		g.ae_ctx = ctxw[1]; g.ae_par = parw;
		switch (fn) {
		case 0: ro = t32_clm_ct_update(&g, a0, ct); break;
		case 1: ro = t32_clm_refresh_by_csc(&g, a0); break;
		case 2: t32_lce_remove(&g); break;
		default: ro = t32_ae_get_info(&g, ch, (struct t32_ae_info *)oinfo); break;
		}

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		t32_data_d03a0[0] = sh[0];
		memcpy(t32_data_e28d0, clm, 16);
		memcpy(t32_data_e1da8, lce[0], 16);
		memcpy(t32_data_e1db8, lbuf[0], 144);
		memcpy(t32_data_e15a8, ctxw[0], 16);
		memcpy(t32_data_e1588, parw, 16);
		switch (fn) {
		case 0: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_clm_ct_update, a0, rnd(), ct, rnd(), 0); break;
		case 1: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_clm_refresh_by_csc, a0, rnd(), rnd(), rnd(), 0); break;
		case 2: t32o_call((t32o_fn)(void (*)(void))tisp_lce_remove, rnd(), rnd(), rnd(), rnd(), 0); break;
		default: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_get_info, ch, T32O_PTR(vinfo), rnd(), rnd(), 0); break;
		}
		t32_data_d03a0[0] = 0;

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(t32_data_e1da8, lce[1], 16) ||
		    memcmp(t32_data_e1db8, lbuf[1], 144) || memcmp(vinfo, oinfo, 40)) {
			printf("t32_blkglue_oracle: FAIL case %lu fn %d ch %u: ret %#x/%#x hooks %u/%u\n", n, fn, ch, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x / %u %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], ol.e[i][0], ol.e[i][1], ol.e[i][2]);
			return 1;
		}
		for (c = 0; c < 4; c++)
			if (memcmp(tun[0][c], tun[1][c], TUNB)) {
				printf("t32_blkglue_oracle: FAIL case %lu fn %d: AE tuning block %u differs\n", n, fn, c);
				return 1;
			}
		per[fn]++;
	}
	printf("t32_blkglue_oracle: PASS %lu cases (clm_ct_update %lu, clm_refresh_by_csc %lu, lce_remove %lu, ae_get_info %lu)\n",
	       N, per[0], per[1], per[2], per[3]);
	return 0;
}
