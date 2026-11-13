// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 6a: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_tmo_adr_subsection_up and tisp_adr_faceae_curve_adjust.
 * subsection_up calls adr_subsection_map / fix_point_div_32 / fix_point_mult2_32: vendor refs on the vendor side, our
 * ports on ours (real cross-check).  Canary framed arena (11 word output, two 129 point curves with a guard before
 * the first entry (the vendor reads x[-1] / y[-1] on a hit of the first point), the 512 + word table with room for any
 * index, the face ae block, enable bytes, table and counter, copied to the vendor symbols and back).  The whole arena
 * is compared; the return values are leftover registers.
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
#define TABN	16384u
#define TABC	8192u
#define BLKN	(128u + 672u + 16u)

WEAK u32 t32_data_e35b0[4];
WEAK u32 t32_data_e3244;
WEAK u8 t32_data_e34e8[24];
WEAK s16 t32_data_e3248[336];

u64 tisp_tmo_adr_subsection_up(u32, u32, u32, u32, u32);
u64 tisp_adr_faceae_curve_adjust(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_UP, F_FACE, F_N };
static const char *const fname[F_N] = { "subsection_up", "faceae_curve_adjust" };

static u32 rs = 0x6e41b3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 OO, OY, OX, OT, OE, OC, S1, MUL, SH, SH0, MODE, TOT, cur, CH, OD[4];
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

/*
 * a 129 point curve: strictly ascending (a step of 0 divides by zero in the vendor's fix_point_div_32), values >= 0 and <= 4095
 * so that every ratio index stays in the table (the vendor indexes tab[] with a ratio of the outputs: a wild value reads outside
 * of the table on the device too, and with fewer than 9 fraction bits the vendor's constant rounding 512 pulls a lookup below
 * the curve: values >= 4, sh >= 8); the guard entry before the first point (read by the vendor on a hit of the first point)
 * equals the first point (slope 0).
 */
static void curve(u32 o, u32 lo, u32 span)
{
	u32 v = lo, k, step = span / 129 + 1;

	p16(o - 2, lo);
	for (k = 0; k < 129; k++) {
		p16(o + 2 * k, v);
		v += 1 + rb(step);
		if (v > 4095 - (128 - k))
			v = 4095 - (128 - k);
		if (v < lo + 1 + k)
			v = lo + 1 + k;
	}
}

static void gen(int f)
{
	u32 i, k;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	if (f == F_UP) {
		static const u32 shs[] = { 8, 9, 10, 10, 10, 11, 12, 13, 14, 15, 16, 10, 9, 10, 11, 12 };
		u32 span = rb(3) ? 4095 : rb(4095) + 1;

		OO = lay(11 * 4);
		OY = lay(129 * 2 + 4) + 2;
		OX = lay(129 * 2 + 4) + 2;
		OT = lay(TABN * 4);
		curve(OY, 4 + rb(100), span);
		curve(OX, rb(3) ? 0 : rb(100), span);
		if (rb(3) == 0)
			p16(OY + 256, 4090 + rb(6));
		if (rb(4) == 0)
			curve(OX, 0, 1);	/* x = 0 .. 128: every value above returns y[128] */
		{ int v = 0, mode = (int)rb(6);
		  for (k = 0; k < 512; k++) {	/* the sample table around the curve values */
			  p32(OT + 4 * (TABC + k), mode == 0 ? rnd() : (u32)v);
			  v += mode == 1 ? (int)rb(60) : (int)rb(20) + 1;
		  }
		  if (mode == 2)
			  for (k = 0; k < TABN; k++)
				  p32(OT + 4 * k, (u32)rb(10000));
		  if (mode == 3)
			  for (k = 0; k < TABN; k++)
				  p32(OT + 4 * k, rb(2) ? (u32)0x7fffffffu - rb(3) : (u32)0x80000000u + rb(3));
		}
		S1 = rb(4) ? rb(201) : rb(400);
		MUL = rb(6) ? 2 + rb(300) : 1 + rb(8);
		SH = shs[rb(16)];
		if (rb(3))
			SH = 10;
		SH0 = rb(2) ? SH : shs[rb(16)];
		MODE = rb(3) == 0 ? 1 : rb(3) == 0 ? rnd() : 0;
		for (i = 0; i < 11; i++)
			p32(OO + 4 * i, rnd());
	} else {
		for (i = 0; i < 4; i++)
			OD[i] = lay(BLKN);
		OE = lay(24);
		OT = lay(336 * 2);
		OC = lay(4);
		for (i = 0; i < 24; i++)
			IMG[OE + i] = rb(3) ? (u8)(rb(4) == 0) : (u8)(rb(2) ? 1 : rb(256));
		for (i = 0; i < 336; i++)
			p16(OT + 2 * i, rb(4) ? rb(65536) : 0x8000 + rb(3) - 1);
		for (i = 0; i < 4; i++)
			for (k = 0; k < 672 / 2; k++)
				if (rb(3))
					p16(OD[i] + 128 + 2 * k, rb(5) ? rb(65536) : 0x7fff + rb(3));
		switch (rb(5)) {
		case 0: p32(OC, 100000); break;
		case 1: p32(OC, 99999); break;
		case 2: p32(OC, 100001); break;
		case 3: p32(OC, 0xffffffffu - rb(2)); break;
		default: p32(OC, rb(2) ? rb(300) : rnd()); break;
		}
		CH = rb(4);
	}
}

static void run_vendor(int f)
{
	u8 *b = VA;

	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	if (f == F_UP) {
		t32o_arg5 = MUL;
		t32o_arg6 = SH;
		t32o_arg7 = SH0;
		t32o_arg8 = MODE;
		t32o_call(FN(tisp_tmo_adr_subsection_up), T32O_PTR(b + OO), S1, T32O_PTR(b + OY), T32O_PTR(b + OX), T32O_PTR(b + OT + 4 * TABC));
		return;
	} else {
		u32 *pb = t32o_alloc(4 * 16), i;

		for (i = 0; i < 4; i++) {
			pb[i * 4] = T32O_PTR(b + OD[i]);
			t32_data_e35b0[i] = T32O_PTR(&pb[i * 4]);
		}
		memcpy(&t32_data_e3244, b + OC, 4);
		memcpy(t32_data_e34e8, b + OE, 24);
		memcpy(t32_data_e3248, b + OT, 672);
		t32o_call(FN(tisp_adr_faceae_curve_adjust), CH, 0, 0, 0, 0);
		memcpy(b + OC, &t32_data_e3244, 4);
	}
}

static void run_ours(int f)
{
	if (f == F_UP)
		t32_tmo_adr_subsection_up((u32 *)(OA + OO), (int)S1, (const s16 *)(OA + OY), (const s16 *)(OA + OX), (const int *)(OA + OT) + TABC, (int)MUL, SH, SH0, MODE);
	else
		t32_adr_faceae_curve_adjust(OA + OD[CH], (u32 *)(OA + OC), OA + OE, (const s16 *)(OA + OT));
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6000;
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
				printf("MISMATCH %s case %u: arena byte %u (sh %u sh0 %u mul %u) vendor %02x ours %02x\n", fname[f], c, i, SH, SH0, MUL, VA[i], OA[i]);
				if (f == F_UP) {
					u32 q;

					for (q = 0; q < 11; q++)
						printf(" out[%u] vendor %d ours %d\n", q, (int)((u32 *)(VA + OO))[q], (int)((u32 *)(OA + OO))[q]);
				}
				return 1;
			}
		}
	printf("t32_tmo6a_oracle: %u cases compared (2 functions), 0 mismatches\n", compared);
	return 0;
}
