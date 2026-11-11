// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 10a: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_tmoadr_get_statistics (both statistics layouts selected by the stat byte
 * 385, and the "other" value) and tisp_tmo_tmo_tmo2fbf_process (the register stream, system_reg_read scripted).
 * Random inputs (full random 16 KB statistics buffer and stat record), canary framed arena, the whole arena compared (all output blocks,
 * inputs unchanged); register writes compared in order (register, value).  The vendor's return values are leftover registers (not compared).
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
#define ARN	(1u << 17)

WEAK u32 t32_data_e35e0[4];
WEAK u32 t32_data_e35b0[4];
WEAK u32 t32_data_e35c0[4];

u64 tisp_tmo_tmoadr_get_statistics(u32, u32, u32, u32);
u64 tisp_tmo_tmo_tmo2fbf_process(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_STAT, F_F2P, F_N };
static const char *const fname[F_N] = { "tmoadr_get_statistics", "tmo2fbf_process" };

static u32 rs = 0x5a17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 FV8[4], FV16[4], OST, OSRC, OBLK, OBIG, TOT, cur, CH, RDV, S385;
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

static u32 OW[256][2], OWN, RDN_O;
static u32 rdhook(u32 reg) { (void)reg; return RDV; }
static u32 o_rd(void *c, u32 reg) { (void)c; RDN_O = reg; return RDV; }
static void o_regw(void *c, u32 a, u32 v) { (void)c; if (OWN < 256) { OW[OWN][0] = a; OW[OWN][1] = v; } OWN++; }

static void gen(void)
{
	u32 k;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	OST = lay(1400);
	OSRC = lay(16400);
	OBLK = lay(5800);
	OBIG = lay(32100);
	CH = rb(4);
	for (k = 0; k < 4; k++) { FV8[k] = rnd(); FV16[k] = rnd(); }
	RDV = rb(2) ? rb(8) : rnd();
	S385 = rb(3) == 0 ? 0 : rb(3) == 0 ? 1 : rb(2) ? 2 + rb(250) : rnd() & 0xff;
	if (rb(2)) S385 = rb(2);
	IMG[OST + 385] = (u8)S385;
	for (k = 0; k < 16400; k += 4)	/* mix of full random words and sparse ones (every bit field reaches its limits) */
		if (rb(4) == 0) {
			u32 v = rb(2) ? 0xffffffffu : 0, q;

			for (q = 0; q < 4; q++)
				IMG[OSRC + k + q] = (u8)(v >> (8 * q));
		}
}

static void run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), *bb = t32o_alloc(4 * 16), *cc = t32o_alloc(4 * 32), i;

	for (i = 0; i < 4; i++) {
		rec[i * 16 + 1] = T32O_PTR(b + OST);
		rec[i * 16 + 4] = T32O_PTR(b + OBIG);
		t32_data_e35e0[i] = T32O_PTR(&rec[i * 16]);
		bb[i * 4 + 2] = T32O_PTR(b + OBLK);
		t32_data_e35b0[i] = T32O_PTR(&bb[i * 4]);
		cc[i * 8 + 2] = FV8[i];
		cc[i * 8 + 4] = FV16[i];
		t32_data_e35c0[i] = T32O_PTR(&cc[i * 8]);
	}
	t32o_rd_hook = rdhook;
	t32o_log.n = 0;
	if (f == F_STAT)
		t32o_call(FN(tisp_tmo_tmoadr_get_statistics), CH, T32O_PTR(b + OSRC), 0, 0, 0);
	else
		t32o_call(FN(tisp_tmo_tmo_tmo2fbf_process), CH, 0, 0, 0, 0);
}

static void run_ours(int f)
{
	OWN = 0;
	RDN_O = 0;
	if (f == F_STAT)
		t32_tmo_adr_get_statistics(OA[OST + 385], OA + OSRC, OA + OBLK, OA + OBIG);
	else {
		static u8 fbf[24];

		memset(fbf, 0, sizeof(fbf));
		t32_tmo_wr32(fbf + 8, FV8[CH]);
		t32_tmo_wr32(fbf + 16, FV16[CH]);
		t32_tmo_tmo2fbf_process(OA + OST, fbf, o_rd, o_regw, 0);
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
			VA = t32o_alloc(ARN);
			gen();
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			run_vendor((int)f);
			run_ours((int)f);
			compared++;
			if (f == F_F2P) {
				if (t32o_log.n != OWN || OWN != 101 || RDN_O != 28952) { printf("MISMATCH tmo2fbf_process write count %u / %u\n", t32o_log.n, OWN); return 1; }
				for (i = 0; i < OWN; i++)
					if (t32o_log.w[i].reg != OW[i][0] || t32o_log.w[i].val != OW[i][1]) {
						printf("MISMATCH tmo2fbf_process case %u write %u: %x=%x vs %x=%x\n", c, i, t32o_log.w[i].reg, t32o_log.w[i].val, OW[i][0], OW[i][1]);
						return 1;
					}
			} else if (t32o_log.n) { printf("MISMATCH get_statistics wrote registers\n"); return 1; }
			if (memcmp(VA, OA, TOT)) {
				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u (st385 %u; blk %u big %u src %u) vendor %02x ours %02x\n", fname[f], c, i, S385, OBLK, OBIG, OSRC, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo10a_oracle: %u cases compared (2 functions), 0 mismatches\n", compared);
	return 0;
}
