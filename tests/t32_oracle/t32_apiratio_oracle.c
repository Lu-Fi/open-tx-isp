// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our module ratio setters (driver/t32/tx_isp_t32_api_ratio.h)
 * against the vendor tisp_s_sdns_ratio, tisp_s_mdns_ratio and
 * tisp_s_dpc_ratio (with the reference tisp_get_tuning) from Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * The refresh / gain_interp / write_reg callees are recording stubs.
 * Compared: the calls (order, arguments), the return value and every byte of
 * the attribute block, the tuning of all four channels, the e22c8 block, the
 * SDNS and DPC contexts of all channels.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_ratio.h"

extern u32 t32_data_f0904[4], t32_data_e2410[4], t32_data_e35f0[4];
extern u8 t32_data_e22c8[0x200];

static u32 rs = 0x3a7e91u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define NREC 4
static struct { u32 fn, a0, a1; } lg[NREC];
static unsigned int nlg;
static u32 stub_ret;

static u64 rec(u32 fn, u32 a0, u32 a1)
{
	if (nlg < NREC) {
		lg[nlg].fn = fn; lg[nlg].a0 = a0; lg[nlg].a1 = a1;
	}
	nlg++;
	return stub_ret + fn * 0x1001 + nlg;
}
u64 tisp_sdns_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(1, a0, a1); }
u64 tisp_mdns_refresh(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(2, a0, a1); }
u64 tisp_dpc_gain_interp(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(3, a0, a1); }
u64 tisp_dpc_write_reg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return rec(4, a0, 0); }

static int o_sdns(u32 c, u32 a) { return (int)tisp_sdns_refresh(c, a, 0, 0); }
static int o_mdns(u32 c, u32 a) { return (int)tisp_mdns_refresh(c, a, 0, 0); }
static int o_gi(u32 c, u32 s) { return (int)tisp_dpc_gain_interp(c, s, 0, 0); }
static int o_wr(u32 c) { return (int)tisp_dpc_write_reg(c, 0, 0, 0); }
static const struct t32_api_ratio_ops ops = {
	.sdns_refresh = o_sdns, .mdns_refresh = o_mdns, .dpc_gain_interp = o_gi, .dpc_write_reg = o_wr,
};

u64 tisp_s_sdns_ratio(u32, u32, u32, u32);
u64 tisp_s_mdns_ratio(u32, u32, u32, u32);
u64 tisp_s_dpc_ratio(u32, u32, u32, u32);

#define ATB	(4 * 128 + 1100)
#define TUNB	36000
#define CTXB	128
static u8 *attr, *tun[4], *sctx[4], *dctx[4], *mctx[4];
static u8 at0[ATB], tun0[4][TUNB], sc0[4][CTXB], dc0[4][CTXB], mc0[4][CTXB], blk0[0x200];

static void snap(u8 *a, u8 (*t)[TUNB], u8 (*s)[CTXB], u8 (*d)[CTXB], u8 (*m)[CTXB], u8 *b)
{
	unsigned int k;

	memcpy(a, attr, ATB);
	for (k = 0; k < 4; k++) {
		memcpy(t[k], tun[k], TUNB);
		memcpy(s[k], sctx[k], CTXB);
		memcpy(d[k], dctx[k], CTXB);
		memcpy(m[k], mctx[k], CTXB);
	}
	memcpy(b, t32_data_e22c8, 0x200);
}

static void load(void)
{
	unsigned int k;

	memcpy(attr, at0, ATB);
	for (k = 0; k < 4; k++) {
		memcpy(tun[k], tun0[k], TUNB);
		memcpy(sctx[k], sc0[k], CTXB);
		memcpy(dctx[k], dc0[k], CTXB);
		memcpy(mctx[k], mc0[k], CTXB);
	}
	memcpy(t32_data_e22c8, blk0, 0x200);
}

static unsigned int failed, compared;

static void run(unsigned int k, u32 ch, unsigned int c)
{
	static u8 va[ATB], vt[4][TUNB], vs[4][CTXB], vd[4][CTXB], vm[4][CTXB], vb[0x200];
	static u8 oa[ATB], ot[4][TUNB], os[4][CTXB], od[4][CTXB], om[4][CTXB], ob[0x200];
	unsigned int i, j, vn;
	u32 vr, w, vl[NREC][3];
	int or_;

	t32o_reset();
	attr = t32o_alloc(ATB);
	for (j = 0; j < 4; j++) {
		tun[j] = t32o_alloc(TUNB); sctx[j] = t32o_alloc(CTXB); dctx[j] = t32o_alloc(CTXB);
		mctx[j] = t32o_alloc(CTXB);
	}
	stub_ret = rnd();
	for (i = 0; i < ATB; i++) at0[i] = (u8)rnd();
	for (j = 0; j < 4; j++) {
		for (i = 0; i < TUNB; i++) tun0[j][i] = (u8)rnd();
		for (i = 0; i < CTXB; i++) sc0[j][i] = (u8)rnd(), dc0[j][i] = (u8)rnd(), mc0[j][i] = (u8)rnd();
		if (c & 2)	/* small DPC rows: the minimum 5 and small sums */
			for (i = 0; i < 11 * 2; i += 2) {
				tun0[j][23004 - 22 + i + 1] = 0; tun0[j][23004 - 22 + i] &= 15;
				tun0[j][23004 + 242 + i + 1] = 0; tun0[j][23004 + 242 + i] &= 15;
			}
	}
	for (i = 0; i < 0x200; i++)
		blk0[i] = (u8)((c & 4) ? rnd() : (rnd() % 5 == 0 ? (u8[]){ 0, 1, 127, 128, 179, 180, 254, 255 }[rnd() % 8] : rnd()));
	/* the ratio slot: mode 0 (128) / other, ratio around 128 */
	for (j = 0; j < 5; j++) {
		u8 *s = at0 + ch * 128 + 1060 + j * 8;

		w = (c % 3) ? 1 + rnd() % 3 : 0;
		memcpy(s, &w, 4);
		s[4] = (c & 8) ? (u8)rnd() : (u8)(126 + rnd() % 5);
	}
	t32_data_f0904[0] = T32O_PTR(attr);
	for (j = 0; j < 4; j++) {
		t32_data_dd0a4[j] = T32O_PTR(tun[j]);
		t32_data_e2410[j] = T32O_PTR(sctx[j]);
		t32_data_e35f0[j] = T32O_PTR(dctx[j]);
		w = T32O_PTR(mctx[j]);
		memcpy(blk0 + 4 * j, &w, 4);
	}

	load();
	nlg = 0;
	vr = (u32)t32o_call((t32o_fn)(void (*)(void))(k == 0 ? tisp_s_sdns_ratio : k == 1 ? tisp_s_mdns_ratio :
						      tisp_s_dpc_ratio), ch, rnd(), rnd(), rnd(), 0);
	vn = nlg;
	for (i = 0; i < NREC; i++)
		vl[i][0] = lg[i].fn, vl[i][1] = lg[i].a0, vl[i][2] = lg[i].a1;
	snap(va, vt, vs, vd, vm, vb);

	load();
	nlg = 0;
	if (k == 0)
		or_ = t32_s_sdns_ratio(&ops, attr, sctx[ch], ch);
	else if (k == 1)
		or_ = t32_s_mdns_ratio(&ops, attr, tun[ch], t32_data_e22c8, mctx[ch], ch);
	else
		or_ = t32_s_dpc_ratio(&ops, attr, tun[ch], dctx[ch], ch);
	snap(oa, ot, os, od, om, ob);
	compared++;
	if (vn != nlg || vr != (u32)or_ || memcmp(va, oa, ATB) || memcmp(vt, ot, sizeof(vt)) ||
	    memcmp(vs, os, sizeof(vs)) || memcmp(vd, od, sizeof(vd)) || memcmp(vm, om, sizeof(vm)) ||
	    memcmp(vb, ob, 0x200))
		goto bad;
	for (i = 0; i < vn && i < NREC; i++)
		if (vl[i][0] != lg[i].fn || vl[i][1] != lg[i].a0 || vl[i][2] != lg[i].a1)
			goto bad;
	return;
bad:
	if (failed++ < 8)
		printf("MISMATCH k=%u ch=%u: calls %u/%u ret %08x/%08x mem %d%d%d%d%d\n", k, ch, vn, nlg, vr, (u32)or_,
		       !!memcmp(vt, ot, sizeof(vt)), !!memcmp(vs, os, sizeof(vs)), !!memcmp(vd, od, sizeof(vd)),
		       !!memcmp(vm, om, sizeof(vm)), !!memcmp(vb, ob, 0x200));
}

int main(void)
{
	unsigned int c, k, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;

	for (k = 0; k < 3; k++)
		for (c = 0; c < n; c++)
			run(k, rnd() & 3, c);
	printf("t32_apiratio_oracle: 3 entry points, %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
