// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 2a: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_tmo_dark_ctr_interp, tmo_detail_ev_interp,
 * tmo_satcurve_ev_interp.  Both sides get the same random arena (four channels: big context + stat record, canary
 * framed, tables made monotonic most of the time, mode byte 0 / 1, ev values at and around the table entries, the
 * "too large" high word); the whole arena is compared after the call (outputs, inputs unchanged, frames, decoy
 * channels).  dark_ctr_interp calls tmo_interplate / interplate1: the vendor reference runs the vendor leaf, ours
 * our port (real cross-check).  Void functions: the leftover return registers are not compared.
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
#define CTXN	11200u
#define STN	512u

WEAK u32 t32_data_e35e0[4];

u64 tisp_tmo_tmo_dark_ctr_interp(u32, u32, u32, u32);
u64 tisp_tmo_tmo_detail_ev_interp(u32, u32, u32, u32);
u64 tisp_tmo_tmo_satcurve_ev_interp(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_DARK, F_DETAIL, F_SAT, F_N };
static const char *const fname[F_N] = { "dark_ctr_interp", "detail_ev_interp", "satcurve_ev_interp" };

static u32 rs = 0x7a3c91u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 CH, EV0, EV1, EV1M, OC[4], OS[4], TOT, cur;
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
static void put32(u32 o, u32 i, u32 v) { memcpy(IMG + o + 4 * i, &v, 4); }
static u32 get32(u32 o, u32 i) { u32 v; memcpy(&v, IMG + o + 4 * i, 4); return v; }

/* ten entry table, ascending or descending with duplicates / big steps, rarely garbage */
static void table(u32 o, int desc)
{
	u32 i, v = desc ? 20000000u + rb(100000) : rb(1000), step = rb(4) == 0 ? 1 + rb(1000000) : 1 + rb(5000);
	u32 dup = rb(6) == 0, garb = rb(10) == 0;

	for (i = 0; i < 10; i++) {
		put32(o, i, garb ? rnd() : v);
		if (dup && rb(3) == 0)
			continue;
		v = desc ? v - 1 - rb(step) : v + 1 + rb(step);
	}
}

static u32 near_entry(u32 o)	/* ev at, just around or between table entries */
{
	u32 a = get32(o, rb(10)), b = get32(o, rb(10));

	switch (rb(6)) {
	case 0: return a;
	case 1: return a + rb(3) - 1;
	case 2: return a < b ? a + (b - a) / 2 : b + (a - b) / 2;
	case 3: return rb(1 << 20);
	case 4: return rnd();
	default: return a + rb(40) - 20;
	}
}

static void gen(void)
{
	u32 i, o;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		OS[i] = lay(STN);
		IMG[OC[i] + 10931] = rb(2);
		table(OC[i] + 888, 0);
		table(OC[i] + 968, 0);
		table(OC[i] + 1008, 1);
		table(OC[i] + 1048, 1);
	}
	for (i = 0; i < 4; i++)		/* the u16 curve coefficients: small, around the clamps (128, 4096), random */
		for (o = 2700; o < 2850; o += 2) {
			u32 v;

			switch (rb(6)) {
			case 0: v = rb(20); break;
			case 1: v = 120 + rb(16); break;
			case 2: v = 4096 - (1u << rb(13)) + rb(3) - 1; break;
			case 3: v = 4090 + rb(12); break;
			default: continue;
			}
			memcpy(IMG + OC[i] + o, &v, 2);
		}
	for (i = 0; i < 4; i++) {	/* equal neighbour curves: the "span 0" branches of detail_ev_interp */
		if (rb(3) == 0)
			memcpy(IMG + OC[i] + 2552, IMG + OC[i] + 2530, 22);
		if (rb(3) == 0)
			memcpy(IMG + OC[i] + 2640, IMG + OC[i] + 2618, 22);
		if (rb(3) == 0)
			memcpy(IMG + OC[i] + 2662, IMG + OC[i] + 2684, 22);
	}
	CH = rb(4);
	o = OC[CH];
	EV0 = near_entry(o + (rb(2) ? 888 : 968));
	EV1M = near_entry(o + (rb(2) ? 1008 : 1048));
	EV1 = rb(6) == 0 ? rnd() : 0;
	if (rb(3) == 0)
		EV0 = EV1M;		/* the same value for both modes' tables */
	if (rb(25) == 0)
		IMG[o + 10931] = (u8)rnd();
}

static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 48);
	u32 i;

	for (i = 0; i < 4; i++) {
		rec[i * 12 + 0] = T32O_PTR(b + OC[i]);
		rec[i * 12 + 1] = T32O_PTR(b + OS[i]);
		rec[i * 12 + 6] = EV0;
		rec[i * 12 + 7] = EV1;
		rec[i * 12 + 8] = EV1M;
		t32_data_e35e0[i] = T32O_PTR(&rec[i * 12]);
	}
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	switch (f) {
	case F_DARK: return t32o_call(FN(tisp_tmo_tmo_dark_ctr_interp), CH, 0, 0, 0, 0);
	case F_DETAIL: return t32o_call(FN(tisp_tmo_tmo_detail_ev_interp), CH, 0, 0, 0, 0);
	default: return t32o_call(FN(tisp_tmo_tmo_satcurve_ev_interp), CH, 0, 0, 0, 0);
	}
}

static void run_ours(int f)
{
	const u8 *c = OA + OC[CH];
	u8 *s = OA + OS[CH];

	switch (f) {
	case F_DARK: t32_tmo_dark_ctr_interp(c, (struct t32_tmo_dark_out *)(s + 238), EV0, EV1, EV1M); break;
	case F_DETAIL: t32_tmo_detail_ev_interp(c, s, EV0, EV1, EV1M); break;
	default: t32_tmo_satcurve_ev_interp(c, s, EV0, EV1, EV1M); break;
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int f, c, compared = 0;

	if (sizeof(struct t32_tmo_dark_out) != 12) {
		printf("struct layout\n");
		return 1;
	}
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
				printf("MISMATCH %s case %u: arena byte %u (ch %u ctx %u stat %u) vendor %02x ours %02x\n", fname[f], c, i,
				       CH, OC[CH], OS[CH], VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo2_oracle: %u cases compared (3 functions), 0 mismatches\n", compared);
	return 0;
}
