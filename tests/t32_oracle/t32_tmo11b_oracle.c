// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 11b: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_ramV_vi0_reg_refresh, ramV_vi1_reg_refresh and ramY_vi0_reg_refresh (which
 * calls drc_ctrl: the vendor ref + its callees on the vendor side, the ports on ours).  Four channels, canary framed arena, random contexts and
 * tone buffers / curves, d03a0 = 0 / 2 / random (direct registers) and d03a0 = 1 (shadow: tisp_reg_write is the oracle's ref/oa_reg_write.c
 * (tagged log), tisp_reg_lut_write the vendor ref writing the shadow image, armed with t32o_shadow_arm, channel 0)).  system_reg_write /
 * tisp_reg_write register and value, in order, and the shadow image are compared; system_reg_read (28960) is scripted; private_dma_cache_sync
 * count and arguments; the whole arena (outputs, inputs unchanged).
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
WEAK u8 t32_data_d03a0[4];
u8 t32_evblk[16] __attribute__((aligned(16)));
__asm__(".weak t32_data_e3528\n.set t32_data_e3528, t32_evblk\n");

u64 tisp_tmo_ramV_vi0_reg_refresh(u32, u32, u32, u32);
u64 tisp_tmo_ramV_vi1_reg_refresh(u32, u32, u32, u32);
u64 tisp_tmo_ramY_vi0_reg_refresh(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_V0, F_V1, F_Y0, F_N };
static const char *const fname[F_N] = { "ramV_vi0_reg_refresh", "ramV_vi1_reg_refresh", "ramY_vi0_reg_refresh" };

static u32 rs = 0x9a17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 CH, EV0, EV1, EV1M, DRC, STR, OC[4], OS[4], OO[4], OG[4], OB[4], OT[4], OD[4], TOT, cur, RDV;
static u8 D03;
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
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		OS[i] = lay(STN);
		OO[i] = lay(402 + 8);
		OG[i] = lay(8);
		OB[i] = lay(BIGN);
		OT[i] = lay(7520 + 16);
		fillctx(OC[i]);
		p16(OG[i], rb(3) ? 1 + rb(300) : rnd());
		p16(OG[i] + 2, rb(3) ? 1 + rb(200) : rnd());
		for (k = 0; k < 512; k++)
			p32(OB[i] + 30000, k, rb(3) ? rb(40) : rnd() >> rb(32));
		for (k = 0; k < 201; k++)
			p16(OO[i] + 2 * k, rb(5) ? rb(8192) : rnd());
		for (k = 0; k < 7520; k += 2)	/* tone buffer: full range u16 with sparse values */
			p16(OT[i] + k, rb(4) == 0 ? (rb(2) ? 0xffff : 0) : rnd());
	}
	for (k = 0; k < 3; k++)
		for (i = 0; i < 201; i++)
			p16(OD[k] + 2 * i, rb(3) ? rb(4096) : rnd());
	CH = rb(4);
	D03 = rb(3) ? (u8)(rb(2) ? 0 : 2) : rb(3) ? 1 : (u8)rnd();
	if (f == F_V1 && D03 == 1)
		CH = 0;		/* the shadow image is armed for channel 0 */
	RDV = rnd();
	{ u32 o = OC[CH]; EV0 = near_entry(o + 888); EV1M = near_entry(o + 1008); }
	EV1 = rb(6) == 0 ? rnd() : 0;
	{ static const u32 dv[] = { 0, 2, 0x100, 0x101, 0xffff, 0x10001 }; DRC = rb(8) ? 1 : dv[rb(6)]; }
	switch (rb(6)) {
	case 0: STR = 127 + rb(3); break;
	case 1: STR = rb(300); break;
	case 2: STR = rnd(); break;
	default: STR = rb(257); break;
	}
}


static u32 OW[4096][2], OWN, ODN, ODA, SHN;
static u32 SHADOW[8192];
static void o_sysw(void *c, u32 reg, u32 val) { (void)c; if (OWN < 4096) { OW[OWN][0] = reg; OW[OWN][1] = val; } OWN++; }
static void o_regw(void *c, int ch, u32 reg, u32 val)
{
	(void)c;
	if (OWN < 4096) { OW[OWN][0] = D03 == 1 ? 0x80000000u | ((u32)ch & 7) << 24 | reg : reg; OW[OWN][1] = val; }
	OWN++;
}
static void o_lutw(void *c, int ch, u32 reg, u32 val, u32 idx)
{
	(void)c; (void)ch;
	if (D03 == 1) {
		if (reg + idx < 8192) SHADOW[reg + idx] = val;
		else SHN++;
	} else {
		o_sysw(0, reg, val);
	}
}
static u32 o_rd(void *c, u32 reg) { (void)c; if (reg != 28960) SHN += 1000; return RDV; }
static void o_dma(void *c, u8 *b, u32 n) { (void)c; ODN += n == 7520 ? 1 : 100; ODA = (u32)(b - OA); }
static u32 rdhook(u32 reg) { return reg == 28960 ? RDV : 0x5a000000u | reg; }

static u32 *VIMG;
static void run_vendor(int f)
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
		r[5] = T32O_PTR(b + OT[i]);
		r[6] = EV0;
		r[7] = EV1;
		r[8] = EV1M;
		((u16 *)r)[27] = (u16)DRC;
		((u16 *)r)[28] = (u16)STR;
		t32_data_e35e0[i] = T32O_PTR(r);
	}
	memcpy(t32_data_c029c, b + OD[0], 402);
	memcpy(t32_data_d89a0, b + OD[1], 402);
	memcpy(t32_data_c0544, b + OD[2], 402);
	memcpy(t32_data_c0430, b + OD[3], 20);
	t32_data_d03a0[0] = D03;
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	t32o_dma_n = 0;
	t32o_rd_hook = rdhook;
	t32o_log.n = 0;
	VIMG = D03 == 1 ? t32o_shadow_arm(8192) : 0;
	t32o_call(FN(f == F_V0 ? tisp_tmo_ramV_vi0_reg_refresh : f == F_V1 ? tisp_tmo_ramV_vi1_reg_refresh : tisp_tmo_ramY_vi0_reg_refresh), CH, 0, 0, 0, 0);
}

static void run_ours(int f)
{
	struct t32_tmo_rec r;
	struct t32_tmo_ramio io;
	u32 i;

	memset(&r, 0, sizeof(r));
	r.ctx = OA + OC[CH];
	r.st = OA + OS[CH];
	r.out = (u16 *)(OA + OO[CH]);
	r.geom = (u16 *)(OA + OG[CH]);
	r.big = OA + OB[CH];
	r.ev0 = EV0; r.ev1 = EV1; r.ev1m = EV1M;
	r.drc_on = (u16)DRC; r.strength = (u16)STR;
	r.dflt = (const u16 *)(OA + OD[0]);
	r.d89a0 = (const u16 *)(OA + OD[1]);
	r.c0544 = (const u16 *)(OA + OD[2]);
	r.tbuf = OA + OT[CH];
	r.c0430 = (const u16 *)(OA + OD[3]);
	r.dma_sync = 0;
	io.cb = 0; io.sysw = o_sysw; io.regw = o_regw; io.lutw = o_lutw; io.rd = o_rd; io.dma = o_dma; io.d03a0 = D03;
	OWN = ODN = SHN = 0;
	for (i = 0; i < 8192; i++)
		SHADOW[i] = 0xa5a50000u ^ i;
	if (f == F_V0) t32_tmo_ramV_vi0_reg_refresh(OA + OT[CH], (int)CH, &io);
	else if (f == F_V1) t32_tmo_ramV_vi1_reg_refresh(OA + OT[CH], (int)CH, &io);
	else t32_tmo_ramY_vi0_reg_refresh(&r, (int)CH, &io);
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 1500;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			u32 i, want;

			t32o_reset();
			VA = t32o_alloc(ARN);
			gen((int)f);
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			run_vendor((int)f);
			run_ours((int)f);
			compared++;
			want = f == F_Y0 ? 100 + 1 + 1 + 1 + 3 + 2 + 1 : f == F_V0 ? 1 + 1500 + 1 + 2 : (D03 == 1 ? 0 : 1500) + 3;
			if (f == F_V1 && D03 == 1) want = 3;
			if (f == F_Y0) want = 1 + 100 + 1 + 1 + 3 + 2;
			if (t32o_log.n != OWN || OWN != want || SHN) { printf("MISMATCH %s case %u (d03a0 %u): write count %u / %u (want %u) shn %u\n", fname[f], c, D03, t32o_log.n, OWN, want, SHN); return 1; }
			for (i = 0; i < OWN; i++)
				if (t32o_log.w[i].reg != OW[i][0] || t32o_log.w[i].val != OW[i][1]) {
					printf("MISMATCH %s case %u (d03a0 %u) write %u: %x=%x vs %x=%x\n", fname[f], c, D03, i, t32o_log.w[i].reg, t32o_log.w[i].val, OW[i][0], OW[i][1]);
					return 1;
				}
			if (f == F_V1) {
				if (t32o_dma_n != 1 || ODN != 1 || t32o_dma_a[1] - T32O_PTR(VA) != ODA || t32o_dma_a[2] != 7520 || t32o_dma_a[0] || t32o_dma_a[3]) { printf("MISMATCH ramV_vi1 dma %u / %u\n", t32o_dma_n, ODN); return 1; }
			} else if (t32o_dma_n || ODN) { printf("MISMATCH %s dma %u / %u\n", fname[f], t32o_dma_n, ODN); return 1; }
			if (D03 == 1 && f == F_V1 && memcmp(VIMG, SHADOW, sizeof(SHADOW))) {
				for (i = 0; i < 8192 && VIMG[i] == SHADOW[i]; i++)
					;
				printf("MISMATCH ramV_vi1 case %u: shadow word %u vendor %x ours %x\n", c, i, VIMG[i], SHADOW[i]);
				return 1;
			}
			if (memcmp(VA, OA, TOT)) {
				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", fname[f], c, i, CH, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo11b_oracle: %u cases compared (3 functions), 0 mismatches\n", compared);
	return 0;
}
