// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 11a: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_adr_reg_write_sometimes, adr_reg_write_one and adr_reg_write_5x5.
 * Random contexts (four channels, canary framed arena), d03a0 = 0 / 1 / 2 / random; the vendor's tisp_reg_write is the oracle's
 * ref/oa_reg_write.c (direct for d03a0 != 1, tagged 0x80000000 | ch << 24 | reg for d03a0 == 1); ours calls regw (ch, reg, value): the
 * sequence of register and value is compared (the register is expected as the vendor logs it).  The arena is compared (inputs unchanged).
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
#define ARN	(1u << 16)
#define CTXN	11200u

WEAK u32 t32_data_e35e0[4];
WEAK u8 t32_data_d03a0[4];
u64 tisp_tmo_adr_reg_write_sometimes(u32, u32, u32, u32);
u64 tisp_tmo_adr_reg_write_one(u32, u32, u32, u32);
u64 tisp_tmo_adr_reg_write_5x5(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_SOME, F_ONE, F_5X5, F_N };
static const char *const fname[F_N] = { "adr_reg_write_sometimes", "adr_reg_write_one", "adr_reg_write_5x5" };
static const u32 NW[F_N] = { 3, 7, 56 };

static u32 rs = 0x6b17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 OC[4], TOT, cur, CH;
static u8 D03, IMG[ARN];
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

static u32 OW[64][3], OWN;
static void o_regw(void *c, int ch, u32 reg, u32 val)
{
	(void)c;
	if (OWN < 64) { OW[OWN][0] = D03 == 1 ? 0x80000000u | ((u32)ch & 7) << 24 | reg : reg; OW[OWN][1] = val; }
	OWN++;
}

static void gen(void)
{
	u32 i, k;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		if (rb(3) == 0)	/* all bits set / clear fields reach the masks */
			for (k = 0; k < 1100; k++)
				IMG[OC[i] + k] = rb(2) ? 0xff : 0;
	}
	CH = rb(4);
	D03 = rb(3) ? (u8)rb(2) : rb(2) ? 2 : (u8)rnd();
}

static void run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), i;

	for (i = 0; i < 4; i++) {
		rec[i * 16] = T32O_PTR(b + OC[i]);
		t32_data_e35e0[i] = T32O_PTR(&rec[i * 16]);
	}
	t32_data_d03a0[0] = D03;
	t32o_log.n = 0;
	t32o_call(FN(f == F_SOME ? tisp_tmo_adr_reg_write_sometimes : f == F_ONE ? tisp_tmo_adr_reg_write_one : tisp_tmo_adr_reg_write_5x5), CH, 0, 0, 0, 0);
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
			OWN = 0;
			if (f == F_SOME) t32_tmo_adr_reg_write_sometimes(OA + OC[CH], D03, o_regw, 0, (int)CH);
			else if (f == F_ONE) t32_tmo_adr_reg_write_one(OA + OC[CH], D03, o_regw, 0, (int)CH);
			else t32_tmo_adr_reg_write_5x5(OA + OC[CH], D03, o_regw, 0, (int)CH);
			compared++;
			if (t32o_log.n != OWN || OWN != NW[f]) { printf("MISMATCH %s write count %u / %u (want %u)\n", fname[f], t32o_log.n, OWN, NW[f]); return 1; }
			for (i = 0; i < OWN; i++)
				if (t32o_log.w[i].reg != OW[i][0] || t32o_log.w[i].val != OW[i][1]) {
					printf("MISMATCH %s case %u (d03a0 %u) write %u: %x=%x vs %x=%x\n", fname[f], c, D03, i, t32o_log.w[i].reg, t32o_log.w[i].val, OW[i][0], OW[i][1]);
					return 1;
				}
			if (memcmp(VA, OA, TOT)) {
				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u vendor %02x ours %02x\n", fname[f], c, i, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo11a_oracle: %u cases compared (3 functions), 0 mismatches\n", compared);
	return 0;
}
