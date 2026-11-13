// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB colour temperature 5: phase 0 of our t32_awb_ct_detect_prep
 * (driver/t32/tx_isp_t32_awbdet.h) against the prologue of the vendor tisp_awb_ct_detect_simd_3
 * (0x209b8) from Matt Davis' mechanical MIPS/MXU2-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 * The vendor function is built with trunc_ct3.sh, which stops it at the phase boundary (label
 * L_20ff4, the zone loop) and copies its local stack to t32o_ct_stack; the gray world
 * fallback leaves earlier and the reference runs to its end (Tiziano_Awb_Ct_Detect_GrayWorld
 * and Ct_Cal included).  The oracle builds the vendor's 47 pointer context.
 * Compared: stop vs. fallback, out / valid, every byte of par and state, the .bss
 * scratch (seven accumulators, the two limit copies), and the spilled working set
 * (tab_b, tab_c, idx, nlim, unit, half).
 * Exit 0 = equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_awbdet.h"

u64 tisp_awb_ct_detect_simd_3(u32 a0, u32 a1, u32 a2, u32 a3);
#define SECB	6740u
#define PARB	sizeof(((struct t32_awb_par *)0)->raw)
#define STB	sizeof(((struct t32_awb_state *)0)->raw)
#define HWB	4292u

static u32 rs = 0x7d3c91u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void p16(u8 *p, u32 off, u32 v) { p[off] = (u8)v; p[off + 1] = (u8)(v >> 8); }
static void p32(u8 *p, u32 off, u32 v) { p16(p, off, v); p16(p, off + 2, v >> 16); }
static u32 g16(const u8 *p, u32 off) { return p[off] | (u32)p[off + 1] << 8; }

static unsigned int cases, failed, nstop, nfb[3];

/* the pointer table of tisp_awb_long_par_update (0x3d158) */
static void ctx_build(u8 *ctx, u32 par, u32 st, u32 hw, u32 tbl)
{
	static const struct { u16 slot, off; } pp[] = {
		{ 0, 3322 }, { 20, 3184 }, { 24, 60 }, { 28, 120 }, { 32, 2164 }, { 36, 5714 }, { 40, 244 }, { 44, 44 },
		{ 52, 3306 }, { 56, 3448 }, { 60, 3438 }, { 64, 292 }, { 68, 3362 }, { 76, 324 }, { 88, 5126 }, { 92, 5166 },
		{ 96, 52 }, { 104, 5206 }, { 112, 5226 }, { 116, 5234 }, { 128, 5682 } };
	static const struct { u16 slot; u32 off; } ps[] = {
		{ 4, 28688 }, { 8, 0x9490 }, { 12, 0x94a0 }, { 48, 28764 }, { 72, 28708 }, { 80, 28718 }, { 84, 28738 },
		{ 100, 29800 }, { 120, 0x9468 }, { 124, 0x947c }, { 132, 0 }, { 136, 16384 }, { 140, 24576 },
		{ 144, 65536 + 13692 }, { 148, 65536 + 14476 }, { 152, 65536 + 15260 }, { 156, 65536 + 15652 },
		{ 160, 65536 + 15796 }, { 164, 65536 + 16516 }, { 168, 65536 + 17476 }, { 172, 65536 + 21572 },
		{ 176, 65536 + 25668 }, { 180, 65536 + 29764 }, { 184, 65536 + 14476 + 19384 }, { 188, 65536 + 14476 + 23480 } };
	unsigned int i;

	for (i = 0; i < sizeof(pp) / sizeof(pp[0]); i++)
		p32(ctx, pp[i].slot, par + pp[i].off);
	for (i = 0; i < sizeof(ps) / sizeof(ps[0]); i++)
		p32(ctx, ps[i].slot, st + ps[i].off);
	p32(ctx, 16, par + tbl);
	p32(ctx, 108, hw + 4246);
}

static void gen(u8 *par, u8 *stt, const u8 *sec)
{
	u32 i, x, y, os, fp, rows, cols, mode = rnd() % 16;

	if (sec)
		memcpy(par, sec, PARB);
	else
		for (i = 0; i < PARB; i++)
			par[i] = (u8)rnd();
	x = rnd() % 100; y = rnd() % 100;
	for (i = 0; i < 15; i++) {
		x += 1 + rnd() % 200;
		y += 1 + rnd() % 200;
		p32(par, 60 + 4 * i, x);
		p32(par, 120 + 4 * i, y);
	}
	fp = 8 + rnd() % 6;
	os = rnd() % 5 ? 6 + rnd() % 6 : rnd() % 40;
	p16(par, 3306, fp);
	p16(par, 3308, os);
	rows = rnd() % 8 ? 1 + rnd() % 32 : rnd() % 33;
	cols = rnd() % 8 ? 1 + rnd() % 32 : rnd() % 33;
	p16(par, 3186, rows);
	p16(par, 3190, cols);
	p16(par, 3448, rnd() % 3 ? 1 : rnd() % 3);
	p16(par, 3450, mode == 0 ? 2 : rnd() % 3 ? 1 : rnd() % 3);
	p16(par, 3452, rnd() % 3 ? 1 : rnd() % 3);
	p16(par, 3454, mode == 1 ? 1 : mode < 4 ? 0 : rnd() % 3);
	for (i = 0; i < 20; i++)
		if (i % 2 == 0 && rnd() % 5 == 0)
			p16(par, 5126 + 2 * i, 0);
	if (rnd() % 4 == 0) {	/* all ten non-zero */
		for (i = 0; i < 10; i++)
			p16(par, 5126 + 4 * i, 1 + rnd() % 60000);
	}
	for (i = 0; i < 1024; i++)
		par[5714 + i] = rnd() % 4;
	for (i = 0; i < STB; i++)
		stt[i] = (u8)rnd();
	/* raw zone values: small, large, zero, equal */
	for (i = 0; i < 1024; i++) {
		u32 v = rnd() % 4 == 0 ? rnd() : rnd() % 5 == 0 ? 0 : rnd() % 100000;

		if (mode == 2)
			v = 0;
		else if (mode == 3)
			v = 77;
		p32(stt, 16384 + 4 * i, v);
	}
	/* statistics around the clamp bounds */
	{
		u32 lr = (u32)(par[60] | par[61] << 8) << (os & 31), hr = (u32)(par[60 + 56] | par[61 + 56] << 8) << (os & 31);

		for (i = 0; i < 4096; i++) {
			u32 v;

			switch (rnd() % 6) {
			case 0: v = lr - 1 + rnd() % 3; break;
			case 1: v = hr - 1 + rnd() % 3; break;
			case 2: v = rnd() % (hr + 1000); break;
			default: v = rnd();
			}
			p32(stt, 4 * i, v);
		}
	}
	if (mode >= 12)
		for (i = 0; i < 4096; i++)
			p32(stt, 4 * i, (par[(i & 1024) ? 120 : 60] | par[(i & 1024) ? 121 : 61] << 8) << (os & 31));
}

static int run_case(const char *what, const u8 *sec)
{
	static u8 par[PARB], opar[PARB], stt[STB], ost[STB];
	u32 oout[2] = { 0x11111111, 0x22222222 }, oval[2] = { rnd() % 3, rnd() % 3 };
	u32 vout[2] = { 0x11111111, 0x22222222 }, vval[2];
	u8 *vpar, *vst, *vhw, *vctx, *vo, *vv;
	struct t32_awb_ctd d;
	struct t32_awb a;
	const char *bad = 0;
	u32 i, ret, tbl = rnd() % 2 ? 1264 : 364;
	const u8 *stk = (const u8 *)t32o_ct_stack;

	gen(par, stt, sec);
	vval[0] = oval[0]; vval[1] = oval[1];
	memcpy(opar, par, PARB);
	memcpy(ost, stt, STB);

	t32o_reset();
	t32o_ct_stop = 0;
	memset(t32o_ct_stack, 0, sizeof(u32) * 208);
	vctx = t32o_alloc(256);
	vpar = t32o_alloc(PARB);
	vst = t32o_alloc(STB);
	vhw = t32o_alloc(HWB);
	vo = t32o_alloc(16);
	vv = t32o_alloc(16);
	memcpy(vpar, par, PARB);
	memcpy(vst, stt, STB);
	memcpy(vo, vout, 8);
	memcpy(vv, vval, 8);
	ctx_build(vctx, T32O_PTR(vpar), T32O_PTR(vst), T32O_PTR(vhw), tbl);
	t32o_call((t32o_fn)(void (*)(void))tisp_awb_ct_detect_simd_3, T32O_PTR(vctx), T32O_PTR(vo), T32O_PTR(vv), 0, 0);

	a.par = (struct t32_awb_par *)opar;
	a.st = (struct t32_awb_state *)ost;
	a.hw = 0;
	memset(&d, 0xa5, sizeof(d));
	ret = (u32)t32_awb_ct_detect_prep(&a, &d, oout, oval);

	cases++;
	if (t32o_ct_stop) {
		nstop++;
		if (!ret)
			bad = "ours left, vendor continued";
	} else {
		if (ret)
			bad = "ours continued, vendor left";
		else {
			u32 gw = g16(par, 3454) == 1 ? 0 : g16(par, 3450) == 2 ? 1 : 2;

			nfb[gw]++;
		}
	}
	if (!bad && t32o_ct_stop) {
		const u8 *bss = t32o_ctbss;
		static const u16 off[7] = { 0x123c, 0x1214, 0x11ec, 0x11c4, 0x119c, 0x1174, 0x114c };
		u32 k;

		for (k = 0; k < 7 && !bad; k++)
			if (memcmp(bss + off[k], d.acc[k], 40))
				bad = "acc";
		if (!bad && memcmp(bss + 0x1138, d.lim_a, 6))
			bad = "lim_a";
		if (!bad && memcmp(bss + 0x1124, d.lim_b, 6))
			bad = "lim_b";
		if (!bad && memcmp(stk + 64 + 32, d.idx, 10))
			bad = "idx";
		if (!bad && memcmp(stk + 64 + 52, d.tab_b, 8))
			bad = "tab_b";
		if (!bad && memcmp(stk + 64 + 44, d.tab_c, 8))
			bad = "tab_c";
		if (!bad && t32o_ct_stack[16 + 38] != d.nlim)
			bad = "nlim";
		if (!bad && t32o_ct_stack[16 + 75] != d.unit)
			bad = "unit";
		if (!bad && t32o_ct_stack[16 + 29] != d.half)
			bad = "half";
	}
	(void)memcpy(vout, vo, 8);
	(void)memcpy(vval, vv, 8);
	if (!bad && !t32o_ct_stop && (vout[0] != oout[0] || vout[1] != oout[1]))
		bad = "output";
	else if (!bad && !t32o_ct_stop && (vval[0] != oval[0] || vval[1] != oval[1]))
		bad = "valid";
	else if (!bad && memcmp(vpar, opar, PARB))
		bad = "par";
	else if (!bad && memcmp(vst, ost, STB))
		bad = "state";
	if (!bad)
		return 0;
	if (failed++ < 5) {
		printf("MISMATCH %s: %s (stop %u ret %u zones %ux%u en3 %u en1 %u)\n", what, bad, t32o_ct_stop, ret, g16(par, 3186),
		       g16(par, 3190), g16(par, 3454), g16(par, 3450));
		for (i = 0; i < PARB; i++)
			if (vpar[i] != opar[i]) { printf("  par+%u vendor %02x ours %02x\n", i, vpar[i], opar[i]); break; }
		for (i = 0; i < STB; i++)
			if (vst[i] != ost[i]) { printf("  st+%u vendor %02x ours %02x\n", i, vst[i], ost[i]); break; }
	}
	return 1;
}

static int bin_cases(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int nbins = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char pth[1024];
		static u8 b[1u << 20];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);
		u32 off, len = 0, k;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(pth, sizeof(pth), "%s/%s", dir, e->d_name);
		f = fopen(pth, "rb");
		if (!f)
			continue;
		bytes = fread(b, 1, sizeof(b), f);
		fclose(f);
		off = t32_iq_section(b, (u32)bytes, T32_IQ_SEC_AWB, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		for (k = 0; k < 40; k++)
			run_case(e->d_name, b + off);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6000;
	int nbins;

	for (c = 0; c < n; c++)
		run_case("random", 0);
	rc_r = cases;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_awbd_oracle: random %u, IQ bins %d (%u cases), stopped at the zone loop %u, fallback en3/en1/zero %u/%u/%u, %u mismatches\n",
	       rc_r, nbins, cases - rc_r, nstop, nfb[0], nfb[1], nfb[2], failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbd_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
