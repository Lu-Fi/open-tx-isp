// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 2b: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_tmo_dark_protect and
 * tisp_tmo_adr_stat_calc.  Both sides get the same random arena (four channels, canary framed; dark_protect: context with the
 * protect limits placed at and around the computed statistic, geometry, curve, big histogram block, the default
 * curve t32_data_c029c; stat_calc: the stat buffer and the two per channel scalars); the whole arena is compared
 * after the call (outputs, inputs unchanged, frames, decoy channels).  Void functions: leftover return registers
 * are not compared.
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
#define CTXN	3000u		/* only the protect fields (up to 2850) are read */
#define BIGN	41000u
#define STATN	10112u

WEAK u32 t32_data_e35e0[4];
WEAK u32 t32_data_e35b0[4];
WEAK u32 t32_data_e3570[4];
WEAK u32 t32_data_e3580[4];
WEAK u8 t32_data_c029c[402];

u64 tisp_tmo_tmo_dark_protect(u32, u32, u32, u32);
u64 tisp_tmo_adr_stat_calc(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_PROT, F_STAT, F_N };
static const char *const fname[F_N] = { "dark_protect", "stat_calc" };

static u32 rs = 0x3d51e7u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 CH, OC[4], OG[4], OO[4], OB[4], OD, TOT, cur, SP, SQ;
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
static void p32(u32 o, u32 v) { memcpy(IMG + o, &v, 4); }
static u32 mixed(void)
{
	switch (rb(6)) {
	case 0: return rb(50);
	case 1: return rb(3000);
	case 2: return rnd();
	case 3: return 0;
	case 4: return 0x7fffff00u + rb(512);
	default: return rb(100000);
	}
}

static void gen(int f)
{
	u32 i, k;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	CH = rb(4);
	if (f == F_PROT) {
		OD = lay(402);
		for (i = 0; i < 4; i++) {
			OC[i] = lay(CTXN);
			OG[i] = lay(8);
			OO[i] = lay(402 + 8);
			OB[i] = lay(BIGN);
			IMG[OC[i] + 2842] = rb(5) ? 1 : (u8)rnd();
			p16(OC[i] + 2848, rb(4) ? rb(512) : rb(2048));
			p16(OG[i], rb(3) ? 1 + rb(300) : rnd());
			p16(OG[i] + 2, rb(3) ? 1 + rb(200) : rnd());
			for (k = 0; k < 512; k++)
				p32(OB[i] + 30000 + 4 * k, rb(3) ? rb(40) : mixed());
			if (rb(4) == 0)
				for (k = 0; k < 512; k++)
					p32(OB[i] + 30000 + 4 * k, 0);
			if (rb(3) == 0)		/* the curve close to the default */
				for (k = 0; k < 201; k++) {
					u16 d;

					memcpy(&d, IMG + OD + 2 * k, 2);
					p16(OO[i] + 2 * k, d + rb(40) - 20);
				}
			if (rb(8) == 0)
				for (k = 0; k < 201; k++)
					p16(OO[i] + 2 * k, rb(2) ? 0 : 0xffff);
			/* limits at and around the statistic x (computed by our port on a copy, ctx[2842] = 0 stops after it) */
			{
				static u8 tc[CTXN], tb[BIGN];
				u16 tg[2], to[201], x;
				u32 idx;

				memcpy(tc, IMG + OC[i], CTXN);
				memcpy(tb, IMG + OB[i], BIGN);
				memcpy(tg, IMG + OG[i], 4);
				memcpy(to, IMG + OO[i], 402);
				tc[2842] = 0;
				t32_tmo_dark_protect(tc, tg, tb, to, (const u16 *)(IMG + OD));
				idx = (u32)(tc[2848] | tc[2849] << 8);
				x = (u16)(tb[32048 + 4 * idx] | tb[32049 + 4 * idx] << 8);
				switch (rb(8)) {
				case 0: p16(OC[i] + 2844, x); p16(OC[i] + 2846, x); break;	/* span 0, x == lo == hi */
				case 1: p16(OC[i] + 2844, x); p16(OC[i] + 2846, x + 1 + rb(2000)); break;
				case 2: p16(OC[i] + 2844, x - 1 - rb(2000)); p16(OC[i] + 2846, x); break;
				case 3: p16(OC[i] + 2844, x + 1); p16(OC[i] + 2846, x + 5); break;	/* x below lo */
				case 4: p16(OC[i] + 2844, x - 5); p16(OC[i] + 2846, x - 1); break;	/* x above hi */
				case 5: p16(OC[i] + 2844, x - rb(40)); p16(OC[i] + 2846, x + rb(40)); break;
				case 6: p16(OC[i] + 2844, rb(65536)); p16(OC[i] + 2846, rb(65536)); break;
				default: p16(OC[i] + 2844, x - 1 - rb(50)); p16(OC[i] + 2846, x + 1 + rb(60000)); break;
				}
			}
		}
	} else {
		for (i = 0; i < 4; i++) {
			OB[i] = lay(STATN);
			for (k = 0; k < STATN / 4; k++)
				p32(OB[i] + 4 * k, rb(5) ? rb(120) : mixed());
		}
		SP = rb(3) ? rb(200) : rnd();
		SQ = rb(3) ? rb(200) : rnd();
		if (rb(6) == 0) { SP = 5120 * 4 / 7 + rb(3) - 1; SQ = 7; }
		if (rb(6) == 0) { SP = 4; SQ = 5118 + rb(4); }
		if (rb(10) == 0) { SP = 0x80000000u >> rb(4); SQ = 1 + rb(8); }
	}
}

static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 48);
	u32 i;

	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	if (f == F_PROT) {
		for (i = 0; i < 4; i++) {
			rec[i * 12 + 0] = T32O_PTR(b + OC[i]);
			rec[i * 12 + 2] = T32O_PTR(b + OO[i]);
			rec[i * 12 + 3] = T32O_PTR(b + OG[i]);
			rec[i * 12 + 4] = T32O_PTR(b + OB[i]);
			t32_data_e35e0[i] = T32O_PTR(&rec[i * 12]);
		}
		memcpy(t32_data_c029c, b + OD, 402);
		return t32o_call(FN(tisp_tmo_tmo_dark_protect), CH, 0, 0, 0, 0);
	}
	for (i = 0; i < 4; i++) {
		rec[i * 12 + 2] = T32O_PTR(b + OB[i]);
		t32_data_e35b0[i] = T32O_PTR(&rec[i * 12]);
		t32_data_e3580[i] = i == CH ? SP : rnd();
		t32_data_e3570[i] = i == CH ? SQ : rnd();
	}
	return t32o_call(FN(tisp_tmo_adr_stat_calc), CH, 0, 0, 0, 0);
}

static void run_ours(int f)
{
	if (f == F_PROT)
		t32_tmo_dark_protect(OA + OC[CH], (const u16 *)(OA + OG[CH]), OA + OB[CH], (u16 *)(OA + OO[CH]),
				     (const u16 *)(OA + OD));
	else
		t32_tmo_adr_stat_calc(OA + OB[CH], SP, SQ);
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 4000;
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
			if (memcmp(VA, OA, TOT)) {
				u32 i;

				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", fname[f], c, i, CH, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo2b_oracle: %u cases compared (2 functions), 0 mismatches\n", compared);
	return 0;
}
