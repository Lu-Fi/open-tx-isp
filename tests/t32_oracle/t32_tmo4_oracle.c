// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 4: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_adr_5x5_init, adr_base_pars, adr_gauss_local and
 * adr_local_info.  Four channels, canary framed arena (context, 24 int output, 31 s16 table); base_pars runs the vendor
 * 5x5_init / 5x5_out refs on the vendor side and our ports on ours.  tisp_math_exp2 (shared math) is the vendor ref on both sides.
 * The whole arena is compared (input tables unchanged, decoy channels); the e3210 cache is compared.  The vendor's
 * private_vmalloc buffers of 5x5_init are a local array on our side (not comparable).
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

WEAK u32 t32_data_e35e0[4];
WEAK s16 t32_data_e3210[4];
WEAK int t32_data_d8724[31];
u8 t32_tabblk[780];
__asm__(".weak t32_data_c00bc\n.set t32_data_c00bc, t32_tabblk+300\n.weak t32_data_c011c\n.set t32_data_c011c, t32_tabblk+396\n"
	".weak t32_data_c017c\n.set t32_data_c017c, t32_tabblk+492\n.weak t32_data_c01dc\n.set t32_data_c01dc, t32_tabblk+588\n"
	".weak t32_data_c023c\n.set t32_data_c023c, t32_tabblk+684\n");

u64 tisp_tmo_adr_5x5_init(u32, u32, u32, u32);
u64 tisp_tmo_adr_base_pars(u32, u32, u32, u32);
u64 tisp_tmo_adr_gauss_local(u32, u32, u32, u32);
u64 tisp_tmo_adr_local_info(u32, u32, u32, u32, u32);
u64 tisp_math_exp2(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_INIT, F_BASE, F_GAUSS, F_LOCAL, F_N };
static const char *const fname[F_N] = { "adr_5x5_init", "adr_base_pars", "adr_gauss_local", "adr_local_info" };

static u32 rs = 0x51a7c3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 CH, W, H, P[5], OC[4], OO[4], OT[4], TOT, cur;
static int D87[31];
static s16 E32[4];
static u8 TAB[780];
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

static u32 vexp2(u32 v, u32 a, u32 b) { return (u32)t32o_call(FN(tisp_math_exp2), v, a, b, 0, 0); }

static u32 size_pick(void)
{
	switch (rb(8)) {
	case 0: return rb(12);
	case 1: return 0x80000000u | rnd();
	case 2: return (u32)-(int)rb(40);
	case 3: return 1 + rb(5) * 6 - 4 + rb(3);	/* around multiples of 6 / 4 */
	default: return 6 + rb(rb(5) ? 160 : 400);
	}
}

static void gen(int f)
{
	u32 i, k;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		OO[i] = lay(24 * 4);
		OT[i] = lay(31 * 2);
	}
	CH = rb(4);
	W = size_pick();
	H = size_pick();
	if (f == F_LOCAL || f == F_GAUSS) {
		W = H = 0;
	}
	/* the 5x5 distance table: ascending, covers the squared distances of the grid */
	for (i = 0; i < 4; i++) {
		u32 v = rb(3), step = 1 + rb(rb(3) ? 80 : 400);

		for (k = 0; k < 31; k++) {
			p16(OT[i] + 2 * k, rb(40) == 0 ? rnd() : v);
			v += rb(step + 1);
		}
	}
	for (k = 0; k < 31; k++)
		D87[k] = rb(5) ? (int)(rb(1 << 20) * (rb(8) == 0 ? -1 : 1)) : (int)rnd();
	for (k = 0; k < 780 / 4; k++) {
		int v = rb(5) ? (int)rb(500) - 80 : rb(2) ? (int)(rnd() >> 8) : (int)rb(3000);

		memcpy(TAB + 4 * k, &v, 4);
	}
	for (i = 0; i < 4; i++) {
		static const u32 kv[] = { 0, 1, 2, 3, 4, 5, 6, 7, 0xffff, 0xfffe };

		p16(OC[i] + 9898, rb(6) ? (u32)(int)(rb(1250) - 140) : rnd());
		p16(OC[i] + 9900, rb(6) ? (u32)(int)(rb(280) - 40) : rnd());
		p16(OC[i] + 9902, rb(6) ? (u32)(int)(rb(380) - 40) : rnd());
		p16(OC[i] + 9904, rb(5) ? rb(6) : kv[rb(10)]);
	}
	for (k = 0; k < 4; k++)
		E32[k] = (s16)rnd();
	if (rb(3) == 0) {	/* a cache that matches */
		int V = (int)(s16)(OC[CH] ? (IMG[OC[CH] + 9898] | IMG[OC[CH] + 9899] << 8) : 0) + 80, t = (s16)(IMG[OC[CH] + 9900] | IMG[OC[CH] + 9901] << 8);
		int s = (s16)(IMG[OC[CH] + 9902] | IMG[OC[CH] + 9903] << 8), kk = (s16)(IMG[OC[CH] + 9904] | IMG[OC[CH] + 9905] << 8);

		V = V < 80 ? 80 : V > 1104 ? 1104 : V;
		t = t < 0 ? 0 : t > 200 ? 200 : t;
		s = s < 0 ? 0 : s > 320 ? 320 : s;
		kk = kk < 0 ? 0 : kk > 5 ? 5 : kk;
		E32[0] = (s16)V; E32[1] = (s16)t; E32[2] = (s16)s; E32[3] = (s16)kk;
		if (rb(2))
			E32[rb(4)] += rb(2) ? 1 : -1;	/* ... or one that does not */
	}
	for (k = 0; k < 5; k++)
		P[k] = rb(8) ? rb(10001) : rnd();
	P[1] = rb(3) ? rb(400) : rb(3) ? (u32)(int)(rb(600) - 300) : rnd();
	P[4] = rb(4) ? rb(400) : rb(3) ? (u32)(int)(rb(60) - 30) : rnd();
	P[2] = 1 + rb(rb(3) ? 100 : 3000);
	P[3] = 1 + rb(rb(3) ? 100 : 3000);
	if (rb(6) == 0) {
		static const u32 p4v[] = { 0, 1, 2, 0xffffffffu };

		P[4] = p4v[rb(4)];
	}
	if (rb(3) == 0)
		P[1] = P[4] + rb(5) - 2;
}

static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), i;

	for (i = 0; i < 4; i++) {
		rec[i * 16] = T32O_PTR(b + OC[i]);
		t32_data_e35e0[i] = T32O_PTR(&rec[i * 16]);
	}
	memcpy(t32_data_d8724, D87, sizeof(D87));
	memcpy(t32_data_e3210, E32, sizeof(E32));
	memcpy(t32_tabblk, TAB, sizeof(TAB));
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	switch (f) {
	case F_INIT: return t32o_call(FN(tisp_tmo_adr_5x5_init), W, H, T32O_PTR(b + OT[CH]), CH, 0);
	case F_BASE: return t32o_call(FN(tisp_tmo_adr_base_pars), W, H, CH, 0, 0);
	case F_GAUSS: return t32o_call(FN(tisp_tmo_adr_gauss_local), T32O_PTR(b + OO[CH]), CH, 0, 0, 0);
	}
	return t32o_call(FN(tisp_tmo_adr_local_info), P[0], P[1], P[2], P[3], P[4]);
}

static u32 run_ours(int f)
{
	s16 cache[4];

	memcpy(cache, E32, sizeof(cache));
	switch (f) {
	case F_INIT: t32_tmo_adr_5x5_init(W, H, (const s16 *)(OA + OT[CH]), OA + OC[CH]); break;
	case F_BASE: t32_tmo_adr_base_pars(W, H, OA + OC[CH], D87); break;
	case F_GAUSS: t32_tmo_adr_gauss_local((int *)(OA + OO[CH]), OA + OC[CH], cache, (const int *)(TAB + 300), vexp2); break;
	default: return (u32)t32_tmo_adr_local_info(P[0], P[1], P[2], P[3], P[4], vexp2);
	}
	if (f == F_GAUSS && memcmp(cache, t32_data_e3210, sizeof(cache))) {
		printf("MISMATCH gauss_local cache\n");
		exit(1);
	}
	return 0;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 1500;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			u64 rv;
			u32 ro;

			t32o_reset();
			VA = t32o_alloc(ARN);
			gen((int)f);
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			rv = run_vendor((int)f);
			ro = run_ours((int)f);
			compared++;
			if (f == F_LOCAL && (u32)rv != ro) {
				printf("MISMATCH local_info case %u: vendor %d ours %d (p %u %d %u %u %d)\n", c, (int)rv, (int)ro, P[0], (int)P[1], P[2], P[3], (int)P[4]);
				return 1;
			}
			if (memcmp(VA, OA, TOT)) {
				u32 i;

				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				if (f == F_GAUSS) {
					u32 q;

					for (q = 0; q < 24; q++)
						printf("%d/%d ", ((int *)(VA + OO[CH]))[q], ((int *)(OA + OO[CH]))[q]);
					printf("\nctx %d %d %d %d tab0 %d\n", (s16)(*(u16 *)(VA + OC[CH] + 9898)), (s16)(*(u16 *)(VA + OC[CH] + 9900)), (s16)(*(u16 *)(VA + OC[CH] + 9902)), (s16)(*(u16 *)(VA + OC[CH] + 9904)), ((int *)TAB)[0]);
				}
				printf("MISMATCH %s case %u: arena byte %u (ch %u, w %u h %u) vendor %02x ours %02x\n", fname[f], c, i, CH, W, H, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo4_oracle: %u cases compared (4 functions), 0 mismatches\n", compared);
	return 0;
}
