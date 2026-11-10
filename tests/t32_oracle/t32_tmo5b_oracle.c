// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 5b: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * function (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_tmo_adr_map_curve1.  Canary framed arena (the 14 u32 output); the vendor's
 * fix_point_div ref on the vendor side, our fixdiv64 on ours; random table t32_data_d8324 (256 s32, negative / zero / large entries),
 * indices / positions around the limits 0 / 255 / 256 and wild.  The whole arena is compared.
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
#define ARN	(1u << 11)

WEAK int t32_data_d8324[256];
u64 tisp_tmo_adr_map_curve1(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

static u32 rs = 0x77b3e1u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 OO, TOT, A1, A2;
static int TAB[256];
static u8 IMG[ARN];
static u8 *VA;
static u8 OA[ARN] __attribute__((aligned(16)));

static u32 pick(void)
{
	static const int sv[] = { -1, 0, 1, 254, 255, 256, 257, 100, 128 };

	switch (rb(4)) {
	case 0: return (u32)sv[rb(9)];
	case 1: return rnd();
	default: return rb(256);
	}
}

static void gen(void)
{
	u32 i;

	memset(IMG, CAN, sizeof(IMG));
	OO = FR;
	for (i = 0; i < 14 * 4; i++)
		IMG[OO + i] = (u8)rnd();
	TOT = OO + 14 * 4 + FR;
	for (i = 0; i < 256; i++) {
		int v;

		switch (rb(6)) {
		case 0: v = (int)rb(300) - 150; break;
		case 1: v = (int)rb(1 << 20) - (1 << 19); break;
		case 2: v = (int)rnd(); break;
		case 3: v = 0; break;
		case 4: v = -(int)rb(300); break;
		default: v = (int)rb(800); break;
		}
		TAB[i] = v;
	}
	A1 = pick();
	A2 = pick();
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int c, compared = 0;

	for (c = 0; c < n; c++) {
		t32o_reset();
		VA = t32o_alloc(ARN);
		gen();
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		memcpy(t32_data_d8324, TAB, sizeof(TAB));
		t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
		t32o_call(FN(tisp_tmo_adr_map_curve1), T32O_PTR(VA + OO), A1, A2, 0, 0);
		t32_tmo_adr_map_curve1((u32 *)(OA + OO), (int)A1, (int)A2, TAB);
		compared++;
		if (memcmp(VA, OA, TOT)) {
			u32 i;

			for (i = 0; i < TOT && VA[i] == OA[i]; i++)
				;
			printf("MISMATCH curve1 case %u: arena byte %u (a1 %d a2 %d tab %d) vendor %02x ours %02x\n", c, i, (int)A1, (int)A2, TAB[A1 < 256 ? A1 : 0], VA[i], OA[i]);
			return 1;
		}
	}
	printf("t32_tmo5b_oracle: %u cases compared (1 function), 0 mismatches\n", compared);
	return 0;
}
