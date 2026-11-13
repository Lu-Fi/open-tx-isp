// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 13c: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor function
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_tmo_tmo2fbf_input_prep with the vendor tisp_tmo_tmo_interplate ref against our port.
 * Four channels in a canary framed arena; ctx byte 10928 = 0 / 1 / 2 / other, thresholds mostly ascending (with equal / garbage ones), the u16 table
 * thresholds of the p map ascending or random, the big block random words (low half 0 .. 400 mostly, all-zero low halves to reach the division by zero
 * of the vendor emulation, which is -1 -> 255 on both sides).  The whole arena (the two 25 x 15 maps, inputs unchanged, decoy channels) is compared.
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
u64 tisp_tmo_tmo_tmo2fbf_input_prep(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

static u32 rs = 0x1e17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 OC[4], OS[4], OB[4], OG[4], TOT, cur, CH, MODE;
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

/* three thresholds ascending (or not) and three values */
static void th3(u32 o, u32 maxv)
{
	u32 a = rb(maxv), b = rb(4) == 0 ? a : a + rb(maxv / 4 + 1), c = rb(4) == 0 ? b : b + rb(maxv / 4 + 1);

	if (rb(12) == 0) { a = rnd() & 0xff; b = rnd() & 0xff; c = rnd() & 0xff; }
	IMG[o] = (u8)a; IMG[o + 1] = (u8)b; IMG[o + 2] = (u8)c;
}

static void gen(void)
{
	u32 i, k, v, zero;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		OS[i] = lay(STN);
		OB[i] = lay(BIGN);
		OG[i] = lay(32);
		IMG[OC[i] + 10928] = rb(6) ? 2 : (u8)rb(5);
		th3(OC[i] + 10894, 120); th3(OC[i] + 10897, 120);		/* A, B thresholds (percent) */
		th3(OC[i] + 10900, 255); th3(OC[i] + 10903, 255);		/* their values */
		th3(OC[i] + 10906, 12); th3(OC[i] + 10914, 255);		/* count thresholds / values (three each: 10906..08, 10914..16) */
		th3(OC[i] + 10922, 255); th3(OC[i] + 10925, 255);
		IMG[OC[i] + 10891] = (u8)rb(16); IMG[OC[i] + 10892] = (u8)rb(16); IMG[OC[i] + 10893] = (u8)rb(16);
		v = rb(3000);
		for (k = 0; k < 8; k++) {
			p16(OC[i] + 8202 + 2 * k, rb(30) == 0 ? rnd() : v);
			p16(OC[i] + 8218 + 2 * k, rb(3) ? rb(8000) : rnd());
			v += rb(4) ? rb(900) : 0;
		}
		IMG[OG[i] + 18] = (u8)(rb(3) ? 8 + rb(8) : rb(40));
		zero = rb(8) == 0;
		for (k = 0; k < 4500; k++) {	/* the two big word arrays: big + 4 idx and big + 15000 + 4 idx, idx up to ~4500 */
			u32 w2 = rb(6) == 0 ? rnd() : (rnd() & 0xffff0000u) | (zero ? 0 : (rb(3) ? rb(400) : rb(65536)));

			p32(OB[i] + 15000 + 4 * k, w2);
			p32(OB[i] + 4 * k, rb(2) ? rnd() : rb(1u << 14));
		}
	}
	CH = rb(4);
	MODE = IMG[OC[CH] + 10928];
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 600;
	unsigned int c, compared = 0;

	for (c = 0; c < n; c++) {
		u32 *rec = 0, i;
		u32 j;

		t32o_reset();
		VA = t32o_alloc(ARN);
		gen();
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		rec = t32o_alloc(4 * 64);
		for (j = 0; j < 4; j++) {
			rec[j * 16] = T32O_PTR(VA + OC[j]);
			rec[j * 16 + 1] = T32O_PTR(VA + OS[j]);
			rec[j * 16 + 3] = T32O_PTR(VA + OG[j]);
			rec[j * 16 + 4] = T32O_PTR(VA + OB[j]);
			t32_data_e35e0[j] = T32O_PTR(&rec[j * 16]);
		}
		t32o_call(FN(tisp_tmo_tmo_tmo2fbf_input_prep), CH, 0, 0, 0, 0);
		t32_tmo_tmo2fbf_input_prep(OA + OS[CH], OA + OC[CH], OA + OB[CH], OA[OG[CH] + 18]);
		compared++;
		if (memcmp(VA, OA, TOT)) {
			for (i = 0; i < TOT && VA[i] == OA[i]; i++)
				;
			printf("MISMATCH case %u (mode %u): arena byte %u (st at %u, ch %u) vendor %02x ours %02x\n", c, MODE, i, OS[CH], CH, VA[i], OA[i]);
			return 1;
		}
	}
	printf("t32_tmo13c_oracle: %u cases compared (tmo2fbf_input_prep), 0 mismatches\n", compared);
	return 0;
}
