// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 3b: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_modechange_stat_refresh
 * and tisp_tmo_tmo_params_hard_refresh.  Four channels, canary framed arena (context, stat record, curve, geometry, big block, gamma
 * block); ctx selector 0 / 1 / other, mode byte 0 / 1, ev at and around the table entries, the dark_protect limits
 * placed around the computed statistic, strength at 127 / 128 / 129 and wide.  modechange_stat_refresh tail calls ev_interp: vendor refs (ev_interp, detail_ev_interp, dark_protect) on the
 * vendor side, our ports on ours (real cross-check); the cache sync call (count and arguments) is compared too.
 * The whole arena is compared after the call.  Void functions: leftover return registers are not compared.
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
WEAK u8 t32_data_c0430[20];
extern u32 t32o_dma_n, t32o_dma_a[4];
WEAK u8 t32_data_c029c[402];
WEAK u8 t32_data_d89a0[402];
WEAK u8 t32_data_c0544[402];

u64 tisp_tmo_modechange_stat_refresh(u32, u32, u32, u32);
u64 tisp_tmo_tmo_params_hard_refresh(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_MODE, F_PAR, F_N };
static const char *const fname[F_N] = { "modechange_stat_refresh", "tmo_params_hard_refresh" };

static u32 rs = 0x2b9d31u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 CH, EV0, EV1, EV1M, DRC, STR, OC[4], OS[4], OO[4], OG[4], OB[4], OT[4], OD[4], TOT, cur;
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

static void gen(int f)
{
	u32 i, k;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 3; i++)
		OD[i] = lay(402);
	OD[3] = lay(20);
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		OS[i] = lay(STN);
		OO[i] = lay(402 + 8);
		OG[i] = lay(8);
		OB[i] = lay(BIGN);
		OT[i] = lay(7520 + 16);
		IMG[OC[i] + 10931] = rb(2);
		IMG[OC[i] + 10042] = rb(6) ? 1 : (u8)rnd();
		p16(OC[i] + 4138, rb(8) == 0 ? rnd() : rb(2));
		table(OC[i] + 888, 0);
		table(OC[i] + 1008, 1);
		for (k = 0; k < 10; k++)
			p16(OC[i] + 4118 + 2 * k, rb(5) ? rb(400) : rnd());
		if (rb(5) == 0)		/* a flat strength table: weight 0 / equal neighbours */
			for (k = 0; k < 10; k++)
				p16(OC[i] + 4118 + 2 * k, 77);
		IMG[OC[i] + 2842] = rb(5) ? 1 : (u8)rnd();
		p16(OC[i] + 2848, rb(4) ? rb(512) : rb(2048));
		p16(OG[i], rb(3) ? 1 + rb(300) : rnd());
		p16(OG[i] + 2, rb(3) ? 1 + rb(200) : rnd());
		for (k = 0; k < 512; k++)
			p32(OB[i] + 30000, k, rb(3) ? rb(40) : rnd() >> rb(32));
		for (k = 0; k < 201; k++)	/* the context's curves and the default: smooth-ish 12 bit values */
			p16(OO[i] + 2 * k, rb(4096));
	}
	for (k = 0; k < 3; k++)
		for (i = 0; i < 201; i++)
			p16(OD[k] + 2 * i, rb(3) ? rb(4096) : rnd());
	{
		static u8 tc[CTXN], tb[BIGN];
		u16 tg[2], to[201], x;
		u32 idx;

		for (i = 0; i < 4; i++) {
			memcpy(tc, IMG + OC[i], CTXN);
			memcpy(tb, IMG + OB[i], BIGN);
			memcpy(tg, IMG + OG[i], 4);
			memcpy(to, IMG + OO[i], 402);
			tc[2842] = 0;
			t32_tmo_dark_protect(tc, tg, tb, to, (const u16 *)(IMG + OD[0]));
			idx = (u32)(tc[2848] | tc[2849] << 8);
			x = (u16)(tb[32048 + 4 * idx] | tb[32049 + 4 * idx] << 8);
			switch (rb(5)) {
			case 0: p16(OC[i] + 2844, x - rb(40)); p16(OC[i] + 2846, x + rb(40)); break;
			case 1: p16(OC[i] + 2844, x + 1); p16(OC[i] + 2846, x + 5); break;
			case 2: p16(OC[i] + 2844, x - 5); p16(OC[i] + 2846, x - 1); break;
			case 3: p16(OC[i] + 2844, rb(65536)); p16(OC[i] + 2846, rb(65536)); break;
			default: p16(OC[i] + 2844, x - 1 - rb(50)); p16(OC[i] + 2846, x + 1 + rb(60000)); break;
			}
		}
	}
	CH = rb(4);
	EV0 = near_entry(OC[CH] + 888);
	EV1M = near_entry(OC[CH] + 1008);
	EV1 = rb(6) == 0 ? rnd() : 0;
	if (rb(3) == 0)
		EV0 = EV1M;
	{ static const u32 dv[] = { 0, 2, 0x100, 0x101, 0xffff, 0x10001 }; DRC = rb(8) ? 1 : dv[rb(6)]; }
	switch (rb(6)) {
	case 0: STR = 127 + rb(3); break;
	case 1: STR = rb(300); break;
	case 2: STR = rnd(); break;
	case 3: STR = 255 + rb(3); break;
	default: STR = rb(257); break;
	}
	(void)f;
}

static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), i;

	for (i = 0; i < 4; i++) {
		u32 *r = &rec[i * 16];

		r[0] = T32O_PTR(b + OC[i]);
		r[1] = T32O_PTR(b + OS[i]);
		r[2] = T32O_PTR(b + OO[i]);
		r[3] = T32O_PTR(b + OG[i]);
		r[4] = T32O_PTR(b + OB[i]);
		r[6] = EV0;
		r[7] = EV1;
		r[8] = EV1M;
		((u16 *)r)[27] = (u16)DRC;
		((u16 *)r)[28] = (u16)STR;
		t32_data_e35e0[i] = T32O_PTR(r);
		r[5] = T32O_PTR(b + OT[i]);
	}
	memcpy(t32_data_c029c, b + OD[0], 402);
	memcpy(t32_data_d89a0, b + OD[1], 402);
	memcpy(t32_data_c0544, b + OD[2], 402);
	memcpy(t32_data_c0430, b + OD[3], 20);
	t32o_dma_n = 0;
	memset(t32o_dma_a, 0, sizeof(t32o_dma_a));
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	if (f == F_MODE)
		return t32o_call(FN(tisp_tmo_modechange_stat_refresh), CH, 0, 0, 0, 0);
	return t32o_call(FN(tisp_tmo_tmo_params_hard_refresh), CH, 0, 0, 0, 0);
}

static u32 SYN_N, SYN_OFF, SYN_LEN;
static void sync_cb(u8 *buf, u32 len) { SYN_N++; SYN_OFF = (u32)(buf - OA); SYN_LEN = len; }

static void run_ours(int f)
{
	struct t32_tmo_rec r;

	r.ctx = OA + OC[CH];
	r.st = OA + OS[CH];
	r.out = (u16 *)(OA + OO[CH]);
	r.geom = (u16 *)(OA + OG[CH]);
	r.big = OA + OB[CH];
	r.ev0 = EV0; r.ev1 = EV1; r.ev1m = EV1M;
	r.drc_on = DRC; r.strength = STR;
	r.dflt = (const u16 *)(OA + OD[0]);
	r.d89a0 = (const u16 *)(OA + OD[1]);
	r.c0544 = (const u16 *)(OA + OD[2]);
	r.tbuf = OA + OT[CH];
	r.c0430 = (const u16 *)(OA + OD[3]);
	r.dma_sync = sync_cb;
	SYN_N = 0;
	if (f == F_MODE)
		t32_tmo_modechange_stat_refresh(&r);
	else
		t32_tmo_params_hard_refresh(r.ctx, r.st);
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			t32o_reset();
			VA = t32o_alloc(ARN);
			gen((int)f);
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			run_vendor((int)f);
			run_ours((int)f);
			compared++;
			if (f == F_MODE && (t32o_dma_n != SYN_N || SYN_N != 1 || t32o_dma_a[0] != 0 || t32o_dma_a[1] != T32O_PTR(VA + SYN_OFF) ||
					     t32o_dma_a[2] != SYN_LEN || t32o_dma_a[3] != 0)) {
				printf("MISMATCH dma sync: vendor n %u (%x %x %u %x) ours n %u off %u len %u\n", t32o_dma_n, t32o_dma_a[0], t32o_dma_a[1], t32o_dma_a[2], t32o_dma_a[3], SYN_N, SYN_OFF, SYN_LEN);
				return 1;
			}
			if (memcmp(VA, OA, TOT)) {
				u32 i;

				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", fname[f], c, i, CH, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo3b_oracle: %u cases compared (2 functions), 0 mismatches\n", compared);
	return 0;
}
