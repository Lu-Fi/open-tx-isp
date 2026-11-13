// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 9a: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_param_array_set, tisp_tmo_api_set_curve, tisp_tmo_wake_up, tisp_tmo_dn_params_refresh (phase 9c; new context pointer dd0a4[ch] + 11976, gamma block dc274[ch] + 32).
 * Four channels in a canary framed arena.  The setters run the vendor refs of params_hard_refresh, dark_ctr_interp,
 * satcurve_ev_interp, ev_interp, adr_hardpars_ctl and adr_gauss_local (ours: the ports) with random context fields; the unported
 * register refresh callees (tmoadr_default_reg_refresh, tmo_default_reg_refresh, ramY_vi0 / vi1, adr_reg_write_one / 5x5 /
 * sometimes / every, tmo2fbf_process, tmo_soft_process, tisp_lcac_no_refresh) are recording stubs on the vendor side and hooks on
 * ours (symbol id and channel in call order compared); system_reg_write (wake_up) is compared register and value.  The whole arena,
 * the e3518 / e3520 / e3528 flags, record bytes 50 / 52, the e3210 cache and the e31b0 output are compared.  The vendor's isp_printf
 * (param_array_set / api_set_curve, ctx byte 10042 == 0 only) is dropped in ours: its count is checked against that condition.
 * The api_set_curve jump table t32_data_c04f0 is the module's (driver/t32/data_relocations.json, .rodata 0x30d0 labels).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_tmo.h"

#define WEAK __attribute__((weak))
#define FR	64u
#define CAN	0xc5
#define ARN	(1u << 19)
#define CTXN	11200u
#define STN	1400u
#define BIGN	41000u

WEAK u32 t32_data_e35e0[4];
WEAK u32 t32_data_e35d0[4];
WEAK u8 t32_data_c0430[20];
extern u32 t32o_dma_n, t32o_dma_a[4];
WEAK u8 t32_data_c029c[402];
WEAK u8 t32_data_d89a0[402];
WEAK u8 t32_data_c0544[402];
WEAK u16 t32_data_e3518[4];
WEAK u16 t32_data_e3520[4];
WEAK u8 t32_data_d03a0[4];
WEAK u32 t32_data_c04f0[10];
WEAK u32 t32_data_e31b0[24];
WEAK u8 t32_data_ca61c[64];
WEAK s16 t32_data_e3210[4];
u8 t32_tabblk[780] __attribute__((aligned(16)));
u8 t32_evblk[16] __attribute__((aligned(16)));
__asm__(".weak t32_data_e3528\n.set t32_data_e3528, t32_evblk\n"
	".weak t32_data_bff90\n.set t32_data_bff90, t32_tabblk\n.weak t32_data_bff9c\n.set t32_data_bff9c, t32_tabblk+12\n"
	".weak t32_data_bffa8\n.set t32_data_bffa8, t32_tabblk+24\n.weak t32_data_bffb4\n.set t32_data_bffb4, t32_tabblk+36\n"
	".weak t32_data_bffcc\n.set t32_data_bffcc, t32_tabblk+60\n.weak t32_data_bffe4\n.set t32_data_bffe4, t32_tabblk+84\n"
	".weak t32_data_bfffc\n.set t32_data_bfffc, t32_tabblk+108\n.weak t32_data_c0014\n.set t32_data_c0014, t32_tabblk+132\n"
	".weak t32_data_c002c\n.set t32_data_c002c, t32_tabblk+156\n.weak t32_data_c0044\n.set t32_data_c0044, t32_tabblk+180\n"
	".weak t32_data_c005c\n.set t32_data_c005c, t32_tabblk+204\n.weak t32_data_c0074\n.set t32_data_c0074, t32_tabblk+228\n"
	".weak t32_data_c008c\n.set t32_data_c008c, t32_tabblk+252\n.weak t32_data_c00a4\n.set t32_data_c00a4, t32_tabblk+276\n"
	".weak t32_data_c00bc\n.set t32_data_c00bc, t32_tabblk+300\n.weak t32_data_c011c\n.set t32_data_c011c, t32_tabblk+396\n"
	".weak t32_data_c017c\n.set t32_data_c017c, t32_tabblk+492\n.weak t32_data_c01dc\n.set t32_data_c01dc, t32_tabblk+588\n"
	".weak t32_data_c023c\n.set t32_data_c023c, t32_tabblk+684\n");

u64 tisp_tmo_param_array_set(u32, u32, u32, u32);
u64 tisp_tmo_api_set_curve(u32, u32, u32, u32);
u64 tisp_tmo_wake_up(u32, u32, u32, u32);
u64 tisp_tmo_dn_params_refresh(u32, u32, u32, u32);
u64 tisp_math_exp2(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_SET, F_CURVE, F_WAKE, F_DN, F_N };
static const char *const fname[F_N] = { "param_array_set", "api_set_curve", "wake_up", "dn_params_refresh" };

/* vendor stubs: symbol id << 8 | channel (arguments of the unported callees) */
enum { S_ADR, S_LCAC, S_TMOD, S_VI1, S_VI0, S_SOM, S_F2P, S_SOFT, S_ONE, S_5X5, S_EVERY };
static u32 VLOG[16], VN, OLOGA[16], ON;
static void vrec(u32 id, u32 ch) { if (VN < 16) VLOG[VN++] = id << 8 | ch; }
u64 tisp_tmo_tmoadr_default_reg_refresh(u32 ch) { vrec(S_ADR, ch); return 0; }
u64 tisp_lcac_no_refresh(u32 ch) { vrec(S_LCAC, ch); return 0; }
u64 tisp_tmo_tmo_default_reg_refresh(u32 ch) { vrec(S_TMOD, ch); return 0; }
u64 tisp_tmo_ramY_vi1_reg_refresh(u32 ch) { vrec(S_VI1, ch); return 0; }
u64 tisp_tmo_ramY_vi0_reg_refresh(u32 ch) { vrec(S_VI0, ch); return 0; }
u64 tisp_tmo_adr_reg_write_sometimes(u32 ch) { vrec(S_SOM, ch); return 0; }
u64 tisp_tmo_tmo_tmo2fbf_process(u32 ch) { vrec(S_F2P, ch); return 0; }
u64 tisp_tmo_tmo_soft_process(u32 ch) { vrec(S_SOFT, ch); return 0; }
u64 tisp_tmo_adr_reg_write_one(u32 ch) { vrec(S_ONE, ch); return 0; }
u64 tisp_tmo_adr_reg_write_5x5(u32 ch) { vrec(S_5X5, ch); return 0; }
u64 tisp_tmo_adr_reg_write_every(u32 ch) { vrec(S_EVERY, ch); return 0; }
static void orec(u32 id, int ch) { if (ON < 16) OLOGA[ON++] = id << 8 | (u32)ch; }
static void h_set(void *c, int id, int ch) { (void)c; orec((u32)id, ch); }	/* setter ids are the symbol ids */
static void h_wake(void *c, int id, int ch)
{
	static const u8 map[9] = { S_ADR, S_LCAC, S_F2P, S_TMOD, S_SOFT, S_ONE, S_5X5, S_SOM, S_EVERY };

	(void)c;
	orec(map[id], ch);
}
static u32 OW[8][2], OWN;
static void h_regw(void *c, u32 a, u32 v) { (void)c; if (OWN < 8) { OW[OWN][0] = a; OW[OWN][1] = v; } OWN++; }
static u32 vexp2(u32 v, u32 a, u32 b) { return (u32)t32o_call(FN(tisp_math_exp2), v, a, b, 0, 0); }

static u32 rs = 0x9a17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 CH, ACH, AIDX, EV0, EV1, EV1M, DRC, STR, OC[4], OS[4], OO[4], OG[4], OB[4], OT[4], OD[4], OSRC, OCN, OGM, VCTXO, TOT, cur, REG[4];
static struct t32_tmo_rec OR;
static struct { u16 e3520[4], e3518[4]; s16 pend[4]; u16 r50[4]; u8 r52[4]; u8 d03a0; s16 cache[4]; int g[24]; } TB, TO;
static u8 TAB[780];
static u8 IMG[ARN];
static u8 *VA;
static u8 OA[ARN] __attribute__((aligned(16)));

static u32 lay(u32 n)
{
	u32 i, o;

	cur = (cur + FR + 15u) & ~15u;
	o = cur;
	for (i = 0; i < n; i++)
		IMG[o + i] = (u8)(rnd() >> 9);
	cur += n;
	TOT = cur + FR;
	return o;
}
static void p16(u32 o, u32 v) { u16 x = (u16)v; memcpy(IMG + o, &x, 2); }
static void p32(u32 o, u32 i, u32 v) { memcpy(IMG + o + 4 * i, &v, 4); }
static u32 g32(u32 o, u32 i) { u32 v; memcpy(&v, IMG + o + 4 * i, 4); return v; }

static void table(u32 o, int desc)
{
	u32 i, v = desc ? 20000000u + rb(100000) : rb(1000), step = rb(4) == 0 ? 1 + rb(1000000) : 1 + rb(5000);
	u32 dup = rb(6) == 0, garb = rb(10) == 0;

	for (i = 0; i < 10; i++) {
		p32(o, i, garb ? rnd() : v);
		if (dup && rb(3) == 0)
			continue;
		v = desc ? v - 1 - rb(step) : v + 1 + rb(step);
	}
}
static u32 near_entry(u32 o)
{
	u32 k = rb(9), a = g32(o, rb(10)), b = g32(o, rb(10));

	if (rb(2)) {		/* strictly inside interval k (either direction) */
		a = g32(o, k);
		b = g32(o, k + 1);
		if (a != b)
			return (a < b ? a : b) + rnd() % (a < b ? b - a : a - b);
	}

	switch (rb(6)) {
	case 0: return a;
	case 1: return a + rb(3) - 1;
	case 2: return a < b ? a + (b - a) / 2 : b + (a - b) / 2;
	case 3: return rb(1 << 20);
	case 4: return rnd();
	default: return a + rb(40) - 20;
	}
}

static void fillctx(u32 o)
{
	u32 k, x = rb(100);
	static const u32 sv[] = { 0, 1, 2, 3, 0xffff, 0x8000, 129, 128, 127 }, kv[] = { 0, 1, 2, 3, 4, 5, 6, 7, 0xffff, 0xfffe };
	int bp[4] = { (int)rb(19) - 2, 0, 0, 0 };

	IMG[o + 10931] = rb(2);
	IMG[o + 10042] = x < 50 ? 1 : x < 75 ? 0 : x < 90 ? (u8)(2 + rb(3)) : (u8)rnd();
	p16(o + 4138, rb(8) == 0 ? rnd() : rb(2));
	table(o + 888, 0);
	table(o + 1008, 1);
	for (k = 0; k < 10; k++)
		p16(o + 4118 + 2 * k, rb(5) ? rb(400) : rnd());
	IMG[o + 2842] = rb(5) ? 1 : (u8)rnd();
	p16(o + 2848, rb(4) ? rb(512) : rb(2048));
	/* gauss_local fields (as t32_tmo4_oracle) */
	p16(o + 9898, rb(6) ? (u32)(int)(rb(1250) - 140) : rnd());
	p16(o + 9900, rb(6) ? (u32)(int)(rb(280) - 40) : rnd());
	p16(o + 9902, rb(6) ? (u32)(int)(rb(380) - 40) : rnd());
	p16(o + 9904, rb(5) ? rb(6) : kv[rb(10)]);
	/* hardpars_ctl fields (as t32_tmo3c_oracle) */
	p16(o + 9992, rb(3) ? rb(3) : sv[rb(9)]);
	p16(o + 9984, rb(3) ? rb(140) : sv[rb(9)] + rb(2) * 0xffe0);
	p16(o + 9986, rb(3) ? rb(140) : sv[rb(9)] + rb(2) * 0xffe0);
	p16(o + 9994, rb(5) ? 1 : rb(3));
	bp[1] = bp[0] + (int)rb(8);
	bp[2] = bp[1] + (int)rb(8);
	bp[3] = bp[2] + (int)rb(8);
	if (rb(10) == 0)
		for (k = 0; k < 4; k++)
			bp[k] = (int)rnd();
	for (k = 0; k < 4; k++)
		p16(o + 9996 + 2 * k, (u32)bp[k]);
	for (k = 0; k < 4; k++)
		p16(o + 10004 + 2 * k, rb(3) ? rb(5000) : rnd());
}

static void gen(int f)
{
	u32 i, k;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 3; i++)
		OD[i] = lay(402);
	OD[3] = lay(20);
	OSRC = lay(CTXN);
	fillctx(OSRC);
	OCN = lay(CTXN);
	fillctx(OCN);
	OGM = lay(272);
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		OS[i] = lay(STN);
		OO[i] = lay(402 + 8);
		OG[i] = lay(8);
		OB[i] = lay(BIGN);
		OT[i] = lay(7520 + 16);
		fillctx(OC[i]);
		IMG[OS[i] + 385] = rb(2) ? 1 : rb(2) ? 0 : (u8)rnd();
		p16(OG[i], rb(3) ? 1 + rb(300) : rnd());
		p16(OG[i] + 2, rb(3) ? 1 + rb(200) : rnd());
		for (k = 0; k < 512; k++)
			p32(OB[i] + 30000, k, rb(3) ? rb(40) : rnd() >> rb(32));
		for (k = 0; k < 201; k++)
			p16(OO[i] + 2 * k, rb(4096));
		REG[i] = rb(3) ? rb(1 << 20) : rnd();
	}
	for (k = 0; k < 3; k++)
		for (i = 0; i < 201; i++)
			p16(OD[k] + 2 * i, rb(3) ? rb(4096) : rnd());
	for (k = 0; k < 300; k++)
		TAB[k] = (u8)rnd();
	for (k = 75; k < 780 / 4; k++) {
		int v = rb(5) ? (int)rb(500) - 80 : rb(2) ? (int)(rnd() >> 8) : (int)rb(3000);

		memcpy(TAB + 4 * k, &v, 4);
	}
	CH = rb(4);
	{ u32 o = rb(3) ? OC[CH] : OSRC; EV0 = near_entry(o + 888); EV1M = near_entry(o + 1008); }
	EV1 = rb(6) == 0 ? rnd() : 0;
	{ static const u32 dv[] = { 0, 2, 0x100, 0x101, 0xffff, 0x10001 }; DRC = rb(8) ? 1 : dv[rb(6)]; }
	STR = rb(3) ? rb(257) : rnd();
	ACH = CH | (f == F_CURVE && rb(2) ? 256u * (1 + rb(3)) : 0);
	AIDX = (rb(8) ? rb(10) : 10 + rb(250)) | (rb(4) == 0 ? 256u * (1 + rb(3)) : 0);
	memset(&TB, 0, sizeof(TB));
	for (i = 0; i < 4; i++) {
		TB.e3520[i] = (u16)rnd();
		TB.e3518[i] = (u16)rnd();
		TB.pend[i] = (s16)(rb(2) ? rb(3) : rnd());
		TB.r50[i] = (u16)rnd();
		TB.r52[i] = rb(3) ? 0 : (u8)rnd();
		TB.cache[i] = (s16)rnd();
	}
	TB.d03a0 = rb(3) ? (u8)rb(2) : (u8)rnd();
	for (k = 0; k < 24; k++)
		TB.g[k] = (int)rnd();
	if (rb(3) == 0) {	/* a cache that matches the context after the copy (gauss_local returns early) */
		u32 s = f == F_SET ? OSRC : f == F_DN ? OCN : OC[CH];
		int V = (s16)(IMG[s + 9898] | IMG[s + 9899] << 8) + 80, t = (s16)(IMG[s + 9900] | IMG[s + 9901] << 8);
		int sc = (s16)(IMG[s + 9902] | IMG[s + 9903] << 8), kk = (s16)(IMG[s + 9904] | IMG[s + 9905] << 8);

		V = V < 80 ? 80 : V > 1104 ? 1104 : V;
		t = t < 0 ? 0 : t > 200 ? 200 : t;
		sc = sc < 0 ? 0 : sc > 320 ? 320 : sc;
		kk = kk < 0 ? 0 : kk > 5 ? 5 : kk;
		TB.cache[0] = (s16)V; TB.cache[1] = (s16)t; TB.cache[2] = (s16)sc; TB.cache[3] = (s16)kk;
	}
	TO = TB;
}

static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), *rg = t32o_alloc(4 * 16), *dcr = t32o_alloc(4 * 64), i;
	static const u32 lab[10] = { 0x9ea50, 0x9ea6c, 0x9ea88, 0x9eaa4, 0x9eac0, 0x9eadc, 0x9eaf8, 0x9eb14, 0x9eb30, 0x9eb4c };
	u64 ret = 0;

	for (i = 0; i < 4; i++) {
		u32 *r = &rec[i * 16];

		r[0] = T32O_PTR(b + OC[i]);
		r[1] = T32O_PTR(b + OS[i]);
		r[2] = T32O_PTR(b + OO[i]);
		r[3] = T32O_PTR(b + OG[i]);
		r[4] = T32O_PTR(b + OB[i]);
		r[5] = T32O_PTR(b + OT[i]);
		r[6] = EV0;
		r[7] = EV1;
		r[8] = EV1M;
		((u16 *)r)[25] = TB.r50[i];
		((u8 *)r)[52] = TB.r52[i];
		((u16 *)r)[27] = (u16)DRC;
		((u16 *)r)[28] = (u16)STR;
		t32_data_e35e0[i] = T32O_PTR(r);
		rg[i * 4 + 2] = REG[i];
		t32_data_e35d0[i] = T32O_PTR(&rg[i * 4]);
		dcr[i * 16 + 8] = T32O_PTR(b + OGM);
		t32_data_dc274[i] = T32O_PTR(&dcr[i * 16]);
		t32_data_dd0a4[i] = T32O_PTR(b + OCN) - 11976u;
	}
	memcpy(t32_data_c029c, b + OD[0], 402);
	memcpy(t32_data_d89a0, b + OD[1], 402);
	memcpy(t32_data_c0544, b + OD[2], 402);
	memcpy(t32_data_c0430, b + OD[3], 20);
	memcpy(t32_data_e3520, TB.e3520, 8);
	memcpy(t32_data_e3518, TB.e3518, 8);
	memcpy(t32_evblk, TB.pend, 8);
	memcpy(t32_data_e3210, TB.cache, 8);
	memcpy(t32_data_e31b0, TB.g, 96);
	memcpy(t32_tabblk, TAB, 780);
	memcpy(t32_data_c04f0, lab, sizeof(lab));	/* module jump table: .rodata 0x30d0 of data_relocations.json */
	t32_data_d03a0[0] = TB.d03a0;
	t32o_dma_n = 0;
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	VN = 0;
	switch (f) {
	case F_SET: ret = t32o_call(FN(tisp_tmo_param_array_set), CH, T32O_PTR(b + OSRC), 0, 0, 0); break;
	case F_CURVE: ret = t32o_call(FN(tisp_tmo_api_set_curve), ACH, AIDX, T32O_PTR(b + OSRC), 0, 0); break;
	case F_WAKE: ret = t32o_call(FN(tisp_tmo_wake_up), CH, 0, 0, 0, 0); break;
	default: ret = t32o_call(FN(tisp_tmo_dn_params_refresh), CH, 0, 0, 0, 0); break;
	}
	VCTXO = rec[CH * 16] - T32O_PTR(b);
	for (i = 0; i < 4; i++) {
		TB.r50[i] = ((u16 *)&rec[i * 16])[25];
		TB.r52[i] = ((u8 *)&rec[i * 16])[52];
	}
	memcpy(TB.e3520, t32_data_e3520, 8);
	memcpy(TB.e3518, t32_data_e3518, 8);
	memcpy(TB.pend, t32_evblk, 8);
	memcpy(TB.cache, t32_data_e3210, 8);
	memcpy(TB.g, t32_data_e31b0, 96);
	return ret;
}

static u32 SYN_N;
static void sync_cb(u8 *buf, u32 len) { (void)buf; (void)len; SYN_N++; }

static void run_ours(int f)
{
	struct t32_tmo_rec *rp = &OR;
	struct t32_tmo_set_env e;

	memset(rp, 0, sizeof(*rp));
	rp->ctx = OA + OC[CH];
	rp->st = OA + OS[CH];
	rp->out = (u16 *)(OA + OO[CH]);
	rp->geom = (u16 *)(OA + OG[CH]);
	rp->big = OA + OB[CH];
	rp->ev0 = EV0; rp->ev1 = EV1; rp->ev1m = EV1M;
	rp->drc_on = (u16)DRC; rp->strength = (u16)STR;
	rp->dflt = (const u16 *)(OA + OD[0]);
	rp->d89a0 = (const u16 *)(OA + OD[1]);
	rp->c0544 = (const u16 *)(OA + OD[2]);
	rp->tbuf = OA + OT[CH];
	rp->c0430 = (const u16 *)(OA + OD[3]);
	rp->dma_sync = sync_cb;
	e.r = rp;
	e.rec50 = &TO.r50[CH];
	e.rec52 = &TO.r52[CH];
	e.pend = (u16 *)&TO.pend[CH];
	e.e3520 = &TO.e3520[CH];
	e.e3518 = &TO.e3518[CH];
	e.hard_tab = TAB;
	e.gauss_out = TO.g;
	e.gauss_cache = TO.cache;
	e.gauss_tab = (const int *)(TAB + 300);
	e.exp2 = vexp2;
	e.d03a0 = TO.d03a0;
	e.hook = h_set;
	e.cb = 0;
	SYN_N = 0;
	ON = 0;
	OWN = 0;
	switch (f) {
	case F_SET: t32_tmo_param_array_set(&e, OA + OSRC, (int)CH); break;
	case F_CURVE: t32_tmo_api_set_curve(&e, AIDX, OA + OSRC, (int)ACH); break;
	case F_WAKE: t32_tmo_wake_up(OA + OC[CH], REG[CH], TO.d03a0, h_regw, h_wake, 0, (int)CH); break;
	default: t32_tmo_dn_params_refresh(&e, OA + OCN, OA + OGM, (int)CH); break;
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			u32 i;

			t32o_reset();
			t32o_log.n = 0;
			t32o_log.printfs = 0;
			VA = t32o_alloc(ARN);
			gen((int)f);
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			run_vendor((int)f);
			run_ours((int)f);
			compared++;
			if (VN != ON || memcmp(VLOG, OLOGA, 4 * ON) || t32o_dma_n != SYN_N) {
				printf("MISMATCH %s case %u: calls vendor n %u ours n %u (%x / %x), dma %u / %u\n", fname[f], c, VN, ON, VLOG[0], OLOGA[0], t32o_dma_n, SYN_N);
				return 1;
			}
			if (f == F_WAKE) {
				if (t32o_log.n != OWN || OWN != 5) { printf("MISMATCH wake_up write count %u / %u\n", t32o_log.n, OWN); return 1; }
				for (i = 0; i < OWN; i++)
					if (t32o_log.w[i].reg != OW[i][0] || t32o_log.w[i].val != OW[i][1]) {
						printf("MISMATCH wake_up write %u: %u %x vs %u %x\n", i, t32o_log.w[i].reg, t32o_log.w[i].val, OW[i][0], OW[i][1]);
						return 1;
					}
			} else {
				u32 want = 0;
				u32 s4 = *(u8 *)(VA + (f == F_DN ? OCN : OC[CH]) + 10042) == 0;

				(void)want;
				if (t32o_log.n || t32o_log.printfs != s4) { printf("MISMATCH %s printf %u (expect %u) writes %u\n", fname[f], t32o_log.printfs, s4, t32o_log.n); return 1; }
			}
			if (f != F_DN) VCTXO = OC[CH];
			if ((u32)(OR.ctx - OA) != VCTXO) { printf("MISMATCH %s case %u: ctx pointer %u vs %u\n", fname[f], c, VCTXO, (u32)(OR.ctx - OA)); return 1; }
			if (memcmp(&TB, &TO, sizeof(TB))) {
				u8 *x = (u8 *)&TB, *y = (u8 *)&TO;
				u32 q;

				for (q = 0; q < sizeof(TB) && x[q] == y[q]; q++)
					;
				printf("MISMATCH %s case %u: flags / cache / e31b0 differ at byte %u (ch %u) %02x vs %02x\n", fname[f], c, q, CH, x[q], y[q]);
				return 1;
			}
			if (memcmp(VA, OA, TOT)) {
				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", fname[f], c, i, CH, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo9a_oracle: %u cases compared (4 functions), 0 mismatches\n", compared);
	return 0;
}
