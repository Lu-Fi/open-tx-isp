// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 7a: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_tgain_update, tmo_api_strength_ctrl, tmo_param_array_get,
 * tmo_api_get_curve, tmo_ev_update.  Four channels, canary framed arena as in t32_tmo3b_oracle (generator reused); ev_update
 * runs the vendor ev_interp / dark_ctr_interp / satcurve_ev_interp refs (ours: the ports), the register refresh callees
 * (tmoadr_default_reg_refresh, tmo_default_reg_refresh, ramY_vi0 / vi1_reg_refresh) are recording stubs on both sides
 * (call count and channel compared).  The whole arena, the record fields written and the vendor tables are compared.
 * api_get_curve: the vendor jump table t32_data_c04c8 is filled with the label addresses in index order (assumption, see
 * the header).  Return values are leftover registers (param_array_get: 0, compared).
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

WEAK u16 t32_data_e3520[4];
WEAK u32 t32_data_e3590[4];
WEAK u32 t32_data_e35a0[4];
WEAK u8 t32_data_d03a0[4];
WEAK u32 t32_data_c04c8[10];
WEAK u32 t32_data_e35b0[4];
u8 t32_evblk[72] __attribute__((aligned(16)));
__asm__(".weak t32_data_e3528\n.set t32_data_e3528, t32_evblk\n.weak t32_data_e3530\n.set t32_data_e3530, t32_evblk+8\n"
	".weak t32_data_e3534\n.set t32_data_e3534, t32_evblk+12\n");

u64 tisp_tmo_tgain_update(u32, u32, u32, u32);
u64 tisp_tmo_api_strength_ctrl(u32, u32, u32, u32);
u64 tisp_tmo_param_array_get(u32, u32, u32, u32);
u64 tisp_tmo_api_get_curve(u32, u32, u32, u32);
u64 tisp_tmo_ev_update(u32, u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_TGAIN, F_STR, F_GET, F_CURVE, F_EVUP, F_N };
static const char *const fname[F_N] = { "tgain_update", "api_strength_ctrl", "param_array_get", "api_get_curve", "ev_update" };

/* recording stubs of the unported callees (vendor side) and hooks (ours): id << 8 | channel */
static u32 VLOG[8], VN, OLOGA[8], ON;
u64 tisp_tmo_tmoadr_default_reg_refresh(u32 ch) { if (VN < 8) VLOG[VN++] = 0x100 | ch; return 0; }
u64 tisp_tmo_tmo_default_reg_refresh(u32 ch) { if (VN < 8) VLOG[VN++] = 0x200 | ch; return 0; }
u64 tisp_tmo_ramY_vi0_reg_refresh(u32 ch) { if (VN < 8) VLOG[VN++] = 0x300 | ch; return 0; }
u64 tisp_tmo_ramY_vi1_reg_refresh(u32 ch) { if (VN < 8) VLOG[VN++] = 0x400 | ch; return 0; }
static void h_adr(void *c, int ch) { (void)c; if (ON < 8) OLOGA[ON++] = 0x100 | (u32)ch; }
static void h_tmo(void *c, int ch) { (void)c; if (ON < 8) OLOGA[ON++] = 0x200 | (u32)ch; }
static void h_vi0(void *c, int ch) { (void)c; if (ON < 8) OLOGA[ON++] = 0x300 | (u32)ch; }
static void h_vi1(void *c, int ch) { (void)c; if (ON < 8) OLOGA[ON++] = 0x400 | (u32)ch; }

static u32 rs = 0x5c17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 A2, A3, A4, STRN, IDX, OSZ, OBUF, OCV, OBLK[4];
static struct { u16 e3520[4]; u32 e3590[4], e35a0[4]; s16 pend[4]; u64 seed[4]; u8 d03a0; u32 r9, r6, r7, r8; u16 r54, r56; } TB, TO;
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



static void gen2(int f)
{
	u32 i;

	gen(f);
	OBUF = lay(12300);
	OCV = lay(402 + 8);
	OSZ = lay(8);
	for (i = 0; i < 4; i++) {
		OBLK[i] = lay(800 + 16);
		IMG[OS[i] + 385] = rb(2) ? 1 : rb(2) ? 0 : (u8)rnd();
	}
	A2 = rb(4) ? rnd() : rb(1 << 20);
	A3 = rb(4) ? rnd() : rb(1 << 12);
	A4 = rb(3) ? rb(1 << 22) : rnd();
	STRN = rb(2) ? rb(300) : rnd();
	IDX = rb(14);
	for (i = 0; i < 4; i++) {
		TB.e3520[i] = (u16)rnd();
		TB.e3590[i] = rnd();
		TB.e35a0[i] = rnd();
		TB.pend[i] = (s16)(rb(2) ? rb(3) : rnd());
		TB.seed[i] = ((u64)rnd() << 32) | rnd();
	}
	TB.d03a0 = rb(3) ? (u8)rb(2) : (u8)rnd();
	TB.r6 = rnd(); TB.r7 = rnd(); TB.r8 = rnd(); TB.r9 = rnd();
	TB.r54 = (u16)rnd(); TB.r56 = (u16)rnd();
	if (f == F_EVUP) {	/* ev_update / ev_interp read drc and strength of the record: the generator values */
		TB.r54 = (u16)DRC; TB.r56 = (u16)STR; TB.r6 = EV0; TB.r7 = EV1; TB.r8 = EV1M;
	}
	TO = TB;
}

static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), *pb = t32o_alloc(4 * 16), i;
	u64 ret = 0;

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
		r[9] = i == CH ? TB.r9 : 0;
		((u16 *)r)[27] = i == CH ? TB.r54 : (u16)DRC;
		((u16 *)r)[28] = i == CH ? TB.r56 : (u16)STR;
		t32_data_e35e0[i] = T32O_PTR(r);
		r[5] = T32O_PTR(b + OT[i]);
		pb[i * 4] = T32O_PTR(b + OBLK[i]);
		t32_data_e35b0[i] = T32O_PTR(&pb[i * 4]);
	}
	rec[CH * 16 + 6] = TB.r6;
	rec[CH * 16 + 7] = TB.r7;
	rec[CH * 16 + 8] = TB.r8;
	memcpy(t32_data_c029c, b + OD[0], 402);
	memcpy(t32_data_d89a0, b + OD[1], 402);
	memcpy(t32_data_c0544, b + OD[2], 402);
	memcpy(t32_data_c0430, b + OD[3], 20);
	memcpy(t32_data_e3520, TB.e3520, 8);
	memcpy(t32_data_e3590, TB.e3590, 16);
	memcpy(t32_data_e35a0, TB.e35a0, 16);
	memcpy(t32_evblk, TB.pend, 8);
	memcpy(t32_evblk + 8, TB.seed, 32);
	t32_data_d03a0[0] = TB.d03a0;
	{	/* jump table: label addresses of ctx + 4182 + 402 * k in index order */
		static const u32 lab[10] = { 0x9e968, 0x9e978, 0x9e988, 0x9e998, 0x9e9a8, 0x9e9b8, 0x9e9c8, 0x9e9d8, 0x9e948, 0x9e958 };

		memcpy(t32_data_c04c8, lab, sizeof(lab));
	}
	t32o_dma_n = 0;
	memset(t32o_dma_a, 0, sizeof(t32o_dma_a));
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	VN = 0;
	switch (f) {
	case F_TGAIN: ret = t32o_call(FN(tisp_tmo_tgain_update), CH, 0, A2, A3, 0); break;
	case F_STR: ret = t32o_call(FN(tisp_tmo_api_strength_ctrl), CH, STRN, 0, 0, 0); break;
	case F_GET: ret = t32o_call(FN(tisp_tmo_param_array_get), CH, T32O_PTR(b + OBUF), T32O_PTR(b + OSZ), 0, 0); break;
	case F_CURVE: t32o_call(FN(tisp_tmo_api_get_curve), CH, IDX, T32O_PTR(b + OCV), 0, 0); break;
	default: t32o_call(FN(tisp_tmo_ev_update), CH, 0, A2, A3, A4); break;
	}
	TB.r6 = rec[CH * 16 + 6]; TB.r7 = rec[CH * 16 + 7]; TB.r8 = rec[CH * 16 + 8]; TB.r9 = rec[CH * 16 + 9];
	TB.r54 = ((u16 *)&rec[CH * 16])[27]; TB.r56 = ((u16 *)&rec[CH * 16])[28];
	memcpy(TB.e3520, t32_data_e3520, 8);
	memcpy(TB.e3590, t32_data_e3590, 16);
	memcpy(TB.e35a0, t32_data_e35a0, 16);
	memcpy(TB.pend, t32_evblk, 8);
	memcpy(TB.seed, t32_evblk + 8, 32);
	return ret;
}

static u32 SYN_N, SYN_OFF, SYN_LEN;
static void sync_cb(u8 *buf, u32 len) { SYN_N++; SYN_OFF = (u32)(buf - OA); SYN_LEN = len; }

static void run_ours(int f)
{
	struct t32_tmo_rec r;
	u16 rec54[2] = { TO.r54, TO.r56 };

	r.ctx = OA + OC[CH];
	r.st = OA + OS[CH];
	r.out = (u16 *)(OA + OO[CH]);
	r.geom = (u16 *)(OA + OG[CH]);
	r.big = OA + OB[CH];
	r.ev0 = TO.r6; r.ev1 = TO.r7; r.ev1m = TO.r8;
	r.drc_on = TO.r54; r.strength = TO.r56;
	r.dflt = (const u16 *)(OA + OD[0]);
	r.d89a0 = (const u16 *)(OA + OD[1]);
	r.c0544 = (const u16 *)(OA + OD[2]);
	r.tbuf = OA + OT[CH];
	r.c0430 = (const u16 *)(OA + OD[3]);
	r.dma_sync = sync_cb;
	SYN_N = 0;
	ON = 0;
	switch (f) {
	case F_TGAIN: t32_tmo_tgain_update(&TO.r9, &TO.e3520[CH], &TO.e3590[CH], &TO.e35a0[CH], A2, A3); break;
	case F_STR:
		t32_tmo_api_strength_ctrl(rec54, OA + OC[CH], TO.d03a0, STRN, h_vi1, h_vi0, 0, (int)CH);
		TO.r54 = rec54[0];
		TO.r56 = rec54[1];
		break;
	case F_GET: {
		u32 sz = 0;
		int rv = t32_tmo_param_array_get(OA + OBUF, OA + OC[CH], OA + OO[CH], OA + OBLK[CH], &sz);

		memcpy(OA + OSZ, &sz, 4);
		if (rv != 0) { printf("param_array_get returned %d\n", rv); exit(1); }
		break;
	}
	case F_CURVE: t32_tmo_api_get_curve(OA + OCV, OA + OC[CH], IDX); break;
	default:
		t32_tmo_ev_update(&r, A2, A3, A4, &TO.pend[CH], (u32 *)&TO.seed[CH], h_adr, h_tmo, 0, (int)CH);
		TO.r6 = r.ev0; TO.r7 = r.ev1; TO.r8 = r.ev1m;
		break;
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			u64 vret;

			t32o_reset();
			VA = t32o_alloc(ARN);
			gen2((int)f);
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			vret = run_vendor((int)f);
			run_ours((int)f);
			compared++;
			if (f == F_GET && (u32)vret != 0) {
				printf("MISMATCH param_array_get return %u\n", (u32)vret);
				return 1;
			}
			if (f == F_EVUP && (VN != ON || memcmp(VLOG, OLOGA, 4 * ON) || t32o_dma_n != SYN_N)) {
				printf("MISMATCH ev_update calls: vendor n %u ours n %u (%x %x / %x %x), dma %u / %u\n", VN, ON, VLOG[0], VLOG[1], OLOGA[0], OLOGA[1], t32o_dma_n, SYN_N);
				return 1;
			}
			if (f == F_STR && (VN != ON || memcmp(VLOG, OLOGA, 4 * ON))) {
				printf("MISMATCH api_strength_ctrl calls: vendor n %u ours n %u\n", VN, ON);
				return 1;
			}
			if (memcmp(&TB, &TO, sizeof(TB))) {
				u8 *x = (u8 *)&TB, *y = (u8 *)&TO;
				u32 q;

				for (q = 0; q < sizeof(TB) && x[q] == y[q]; q++)
					;
				printf("MISMATCH %s case %u: tables / record fields differ at tb byte %u (ch %u) %02x vs %02x\n", fname[f], c, q, CH, x[q], y[q]);
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
	printf("t32_tmo7a_oracle: %u cases compared (5 functions), 0 mismatches\n", compared);
	return 0;
}
