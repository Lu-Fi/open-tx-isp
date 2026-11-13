// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 5a: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_adr_map_y_filter and adr_map_y_filter_sp.
 * Canary framed arena (x positions, the 14 y values, the 33 point table, decoys); the vendor's interp1_short / fix_point_div refs on
 * the vendor side, our ports (interp1_short, fixdiv64) on ours.  The whole arena is compared after the call.
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
#define ARN	(1u << 12)

WEAK s16 t32_data_c0444[40];

u64 tisp_tmo_adr_map_y_filter(u32, u32, u32, u32);
u64 tisp_tmo_adr_map_y_filter_sp(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_Y, F_SP, F_N };
static const char *const fname[F_N] = { "adr_map_y_filter", "adr_map_y_filter_sp" };

static u32 rs = 0x6c1f55u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 OX, OB, TOT, cur, IT, IDX;
static s16 C44[40];
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

static void gen(void)
{
	u32 i, v, step;
	s16 x[16];
	u32 y[14];

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	lay(16);			/* decoy before */
	OX = lay(34);
	OB = lay(14 * 4);
	lay(16);
	v = rb(100);
	step = 1 + rb(rb(2) ? 300 : 20);
	for (i = 0; i < 16; i++) {
		x[i] = (s16)(rb(40) == 0 ? rnd() : v);
		v += rb(step + 1);
		if (rb(8) == 0)
			v -= rb(3);
	}
	memcpy(IMG + OX, x, 32);
	v = rb(200);
	step = 1 + rb(rb(2) ? 600 : 60);
	for (i = 0; i < 14; i++) {
		y[i] = rb(30) == 0 ? rnd() : rb(50) == 0 ? 0 : v;
		v += rb(step + 1);
		if (rb(6) == 0)
			v = rb(4096);
		if (rb(12) == 0)
			v -= rb(500);
	}
	memcpy(IMG + OB, y, 56);
	v = rb(50);
	step = 1 + rb(rb(2) ? 200 : 40);
	for (i = 0; i < 33; i++) {
		C44[i] = (s16)(rb(60) == 0 ? rnd() : v);
		v += rb(step + 1);
	}
	IT = rb(5) == 0 ? rb(7) : rb(3);
	if (rb(40) == 0)
		IT = rnd() & 0xffff;
	if (IT > 5 && IT < 0x8000)
		IT = rb(6);
	IDX = rb(4) ? rb(17) : (u32)(int)rb(24) - 4;
	if (rb(30) == 0)
		IDX = rnd();
	IT |= rb(2) ? 0 : rnd() & 0xffff0000u;	/* high bits of the s16 arguments are ignored */
	IDX = (IDX & 0xffff) | (rb(2) ? 0 : rnd() & 0xffff0000u);
	for (i = 0; i < 4; i++)
		;
}

static u64 run_vendor(int f)
{
	u8 *b = VA;

	memcpy(t32_data_c0444, C44, sizeof(C44));
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	if (f == F_Y)
		return t32o_call(FN(tisp_tmo_adr_map_y_filter), T32O_PTR(b + OX), T32O_PTR(b + OB), IT, IDX, 0);
	return t32o_call(FN(tisp_tmo_adr_map_y_filter_sp), T32O_PTR(b + OX), T32O_PTR(b + OB), IT, 0, 0);
}

static void run_ours(int f)
{
	if (f == F_Y)
		t32_tmo_adr_map_y_filter((s16 *)(OA + OX), (u32 *)(OA + OB), (int)IT, (int)IDX);
	else
		t32_tmo_adr_map_y_filter_sp((s16 *)(OA + OX), (u32 *)(OA + OB), (int)IT, C44);
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			t32o_reset();
			VA = t32o_alloc(ARN);
			gen();
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			run_vendor((int)f);
			run_ours((int)f);
			compared++;
			if (memcmp(VA, OA, TOT)) {
				u32 i;

				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u (iters %u idx %d) vendor %02x ours %02x\n", fname[f], c, i, IT, (int)(s16)IDX, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo5a_oracle: %u cases compared (2 functions), 0 mismatches\n", compared);
	return 0;
}
