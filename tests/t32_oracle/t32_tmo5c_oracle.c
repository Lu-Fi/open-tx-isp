// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 5c: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * function (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_tmo_adr_ev_func.  Four channels (context, ev output
 * block, data block t32_data_e3528 / e3530 / e3550), canary framed arena; the vendor's adr_ispint_adr_64 ref on the
 * vendor side, our port on ours.  The 64 bit axis ctx + 800 is built around the point 2000 (entries at 2000 / 2001,
 * unsorted garbage), selectors 0 / 2 / other / negative, pending flag 1 / 0 / 2 / wild.  The whole arena (also
 * the three vendor tables, copied into it) is compared; the return value is leftover registers (not compared).
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
#define CTXN	10000u
#define EVN	256u
#define BKN	72u

WEAK u32 t32_data_e35e0[4];
WEAK u32 t32_data_e35b0[4];
u8 t32_evblk[BKN] __attribute__((aligned(16)));
__asm__(".weak t32_data_e3528\n.set t32_data_e3528, t32_evblk\n.weak t32_data_e3530\n.set t32_data_e3530, t32_evblk+8\n"
	".weak t32_data_e3534\n.set t32_data_e3534, t32_evblk+12\n.weak t32_data_e3550\n.set t32_data_e3550, t32_evblk+40\n");

u64 tisp_tmo_adr_ev_func(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

static u32 rs = 0x3d91a7u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 CH, OC[4], OE[4], OK, TOT, cur;
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
static void p64(u32 o, long long v) { memcpy(IMG + o, &v, 8); }

static long long sel64(void)
{
	switch (rb(8)) {
	case 0: return 2000;
	case 1: return 2001;
	case 2: return 1999;
	case 3: return 0;
	case 4: return -1 - (long long)rb(3000);
	case 5: return (long long)(((u64)rnd() << 32 | rnd()) >> rb(60));
	default: return 1500 + (long long)rb(1200);
	}
}

static void axis(u32 o)
{
	u32 i, mode = rb(10);
	long long v = 2000 - (long long)rb(900), step = 1 + rb(200);

	for (i = 0; i < 11; i++) {
		long long w;

		if (mode == 0)
			w = (long long)(((u64)rnd() << 32 | rnd()) >> rb(50)) * (rb(2) ? 1 : -1);	/* garbage, unsorted */
		else if (mode == 1)
			w = 2000;
		else if (mode == 2)
			w = i == 5 ? 2000 : v;
		else if (mode == 3)
			w = i == 4 ? 2001 : v;
		else
			w = v;
		p64(o + 8 * i, w);
		v += 1 + (long long)rb((u32)step);
	}
}

static void gen(void)
{
	u32 i, k, ofs[] = { 8358, 8380, 8402, 8424, 9150, 9172, 8446, 8688, 8710, 8842 };

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		OE[i] = lay(EVN);
	}
	OK = lay(BKN);
	for (i = 0; i < 4; i++) {
		u32 o = OC[i];
		int sw;

		axis(o + 800);
		for (k = 0; k < CTXN - 8; k += 2)	/* y tables: big and small s16, mostly smooth */
			if (rb(3) == 0)
				p16(o + k, rb(5) ? rb(60) : rnd());
		(void)ofs;
		for (k = 0; k < 8; k++)
			p16(o + 8266 + 2 * k, rb(2) ? rb(2000) : rnd());
		switch (rb(5)) { case 0: sw = 2; break; case 1: sw = 0; break; case 2: sw = 1; break; case 3: sw = -1; break; default: sw = rb(2) ? 2 : 0; }
		p16(o + 9574, (u32)sw);
		p16(o + 9846, rb(2) ? 0 : 1 + rb(3));
		p16(o + 9896, rb(2) ? 0 : (u32)rnd());
		p16(o + 9268, rb(2) ? 0 : 1 + rb(3));
		if (rb(4) == 0) {	/* identical y rows: the interpolation of a flat curve */
			u32 y = rb(65536);

			for (k = 0; k < 11; k++)
				p16(o + 8358 + 2 * k, y);
		}
		if (rb(3) == 0) {	/* steep rows */
			for (k = 0; k < 11; k++)
				p16(o + 8380 + 2 * k, rb(2) ? 32767 - rb(4) : (u32)(-32768 + (int)rb(4)));
		}
	}
	for (i = 0; i < 4; i++) {	/* the vendor tables */
		u32 f = rb(7) == 0 ? rb(4) : rb(5) ? 1 : 2;
		long long sd[4];

		p16(OK + 2 * i, rb(7) ? f : rnd());
		for (k = 0; k < 4; k++)
			sd[k] = sel64();
		memcpy(IMG + OK + 8, sd, 32);
		memcpy(IMG + OK + 40, sd, 32);	/* e3550 starts as anything; use other values */
		for (k = 0; k < 4; k++)
			p64(OK + 40 + 8 * k, sel64());
	}
	CH = rb(4);
	if (rb(8))
		p16(OK + 2 * CH, 1);
}

static void run_vendor(void)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), *pb = t32o_alloc(4 * 16), i;

	for (i = 0; i < 4; i++) {
		rec[i * 16] = T32O_PTR(b + OC[i]);
		t32_data_e35e0[i] = T32O_PTR(&rec[i * 16]);
		pb[i * 4] = 0x12345678;
		pb[i * 4 + 1] = T32O_PTR(b + OE[i]);
		t32_data_e35b0[i] = T32O_PTR(&pb[i * 4]);
	}
	memcpy(t32_evblk, b + OK, BKN);
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	t32o_call(FN(tisp_tmo_adr_ev_func), CH, 0, 0, 0, 0);
	memcpy(b + OK, t32_evblk, BKN);
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
		run_vendor();
		t32_tmo_adr_ev_func((int)CH, OA + OC[CH], OA + OE[CH], (u64 *)(OA + OK + 8), (u64 *)(OA + OK + 40), (s16 *)(OA + OK));
		compared++;
		if (memcmp(VA, OA, TOT)) {
			u32 i;

			for (i = 0; i < TOT && VA[i] == OA[i]; i++)
				;
			printf("MISMATCH ev_func case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", c, i, CH, VA[i], OA[i]);
			return 1;
		}
	}
	printf("t32_tmo5c_oracle: %u cases compared (1 function), 0 mismatches\n", compared);
	return 0;
}
