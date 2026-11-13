// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 1: our readable port (driver/t32/tx_isp_t32_tmo.h)
 * against the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc): tisp_tmo_log2, tmo_interplate, tmo_interplate1, tmo_absfun,
 * ramY_reg_read, tmo_reg_read, adr_5x5_out, adr_interp1_short,
 * adr_ispint_adr_16/_32/_64, adr_subsection_map, tmo_size_update,
 * adr_tgain_func.
 *
 * Both sides get the same random arena (tables, outputs, per channel records)
 * made of canary framed regions; the whole arena is compared after the call
 * (outputs, inputs unchanged, frames, decoy channels), plus the return value
 * (low word; the high word of the vendor return is a leftover register).
 * Callees (fix_point_div_32, fix_point_mult2_32, fix_point_div) run as the
 * vendor reference on the vendor side and as our port on ours.
 * tmo_size_update: the LCE selector byte (ctx + 10039) is checked for every
 * path including the 0x8000 cell case.  Exit 0 = all equal, 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_tmo.h"

#define WEAK __attribute__((weak))
#define FR	64u
#define CAN	0xc5
#define ARN	(1u << 17)

/* vendor data of adr_tgain_func and the channel table of size_update (weak: not in env.c) */
WEAK u32 t32_data_e35a0[2];
WEAK u32 t32_data_e3590[2];
WEAK s16 t32_data_e3520[8];
WEAK u32 t32_data_e35e0[4];



u64 tisp_tmo_log2(u32, u32, u32, u32);
u64 tisp_tmo_tmo_interplate(u32, u32, u32, u32, u32);
u64 tisp_tmo_tmo_interplate1(u32, u32, u32, u32, u32);
u64 tisp_tmo_tmo_absfun(u32, u32, u32, u32);
u64 tisp_tmo_ramY_reg_read(u32, u32, u32, u32);
u64 tisp_tmo_tmo_reg_read(u32, u32, u32, u32);
u64 tisp_tmo_adr_5x5_out(u32, u32, u32, u32, u32);
u64 tisp_tmo_adr_interp1_short(u32, u32, u32, u32, u32, u32);
u64 tisp_tmo_adr_ispint_adr_16(u32, u32, u32, u32);
u64 tisp_tmo_adr_ispint_adr_32(u32, u32, u32, u32);
u64 tisp_tmo_adr_ispint_adr_64(u32, u32, u32, u32, u32);
u64 tisp_tmo_adr_subsection_map(u32, u32, u32, u32, u32, u32, u32, u32, u32, u32);
u64 tisp_tmo_tmo_size_update(u32, u32, u32, u32);
u64 tisp_tmo_adr_tgain_func(u32, u32, u32, u32);

#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_LOG2, F_INTERP, F_INTERP1, F_ABS, F_RAMY, F_REGRD, F_5X5, F_SHORT, F_I16, F_I32, F_I64, F_SUB, F_SIZE,
       F_TGAIN, F_N };
static const char *const fname[F_N] = {
	"log2", "interplate", "interplate1", "absfun", "ramY_reg_read", "tmo_reg_read", "adr_5x5_out",
	"adr_interp1_short", "ispint_16", "ispint_32", "ispint_64", "subsection_map", "size_update", "tgain_func"
};

static u32 rs = 0x51c3a7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rb(u32 n) { return rnd() % n; }

/* test case: scalar arguments, arena layout, the image */
static u32 A[10], OFF[8], TOT, CH, NA;
static u8 IMG[ARN];
static u8 *VA;
static u8 OA[ARN] __attribute__((aligned(16)));
static u32 cur;

static void lay_begin(void)
{
	cur = 0;
	memset(IMG, CAN, sizeof(IMG));
}
static u32 lay(u32 n)	/* region of n random bytes behind a canary frame */
{
	u32 i, o;

	cur = (cur + FR + 7u) & ~7u;
	o = cur;
	for (i = 0; i < n; i++)
		IMG[o + i] = (u8)(rnd() >> 9);
	cur += n;
	TOT = cur + FR;
	return o;
}
static void put16(u32 o, u32 i, int v) { s16 x = (s16)v; memcpy(IMG + o + 2 * i, &x, 2); }
static void put32(u32 o, u32 i, int v) { memcpy(IMG + o + 4 * i, &v, 4); }
static void put64(u32 o, u32 i, long long v) { memcpy(IMG + o + 8 * i, &v, 8); }

static int wide(void)	/* mixed magnitude 32 bit value */
{
	switch (rb(6)) {
	case 0: return (int)rb(40) - 20;
	case 1: return (int)rb(2000) - 1000;
	case 2: return (int)rb(300000) - 150000;
	case 3: return (int)rnd();
	case 4: return (rnd() & 1) ? (int)0x7fffffff - (int)rb(3) : (int)0x80000000 + (int)rb(3);
	default: return (int)rb(100000) - 30000;
	}
}

/* sorted (or, sometimes, unsorted) 16 bit axis with duplicates */
static void axis16(u32 o, u32 n, u32 mode)
{
	int v = (int)rb(2000) - 1000;
	u32 i;

	for (i = 0; i < n; i++) {
		put16(o, i, v);
		if (mode == 0)
			v += (int)rb(30) + 1;
		else if (mode == 1)
			v += (int)rb(3);
		else
			v = (int)(s16)rnd();
	}
}
static void axis32(u32 o, u32 n, u32 mode)
{
	int v = wide() / 4;
	u32 i;

	for (i = 0; i < n; i++) {
		put32(o, i, v);
		if (mode == 0)
			v += (int)rb(5000) + 1;
		else if (mode == 1)
			v += (int)rb(3);
		else
			v = wide();
	}
}
static void axis64(u32 o, u32 n, u32 mode)
{
	long long v = (long long)(int)rnd() * (long long)(rnd() & 0xffff);
	u32 i;

	if (rb(4) == 0)
		v = (long long)((unsigned long long)rnd() << 32 | rnd());
	for (i = 0; i < n; i++) {
		put64(o, i, v);
		if (mode == 0)
			v += (long long)rnd() * (long long)(rb(1000) + 1);
		else if (mode == 1)
			v += (long long)rb(3);
		else
			v = (long long)((unsigned long long)rnd() << 32 | rnd());
	}
}

static int pick16(u32 o, u32 n)	/* an x near the axis points */
{
	s16 t[1];

	if (rb(4) == 0)
		return (int)(s16)rnd() | (rb(2) ? (int)(rnd() << 16) : 0);
	memcpy(t, IMG + o + 2 * rb(n), 2);
	return t[0] + (int)rb(5) - 2;
}

static void gen(int f)
{
	u32 i, n, o;

	lay_begin();
	memset(A, 0, sizeof(A));
	NA = 0;
	switch (f) {
	case F_LOG2:
		A[0] = rb(3) ? rb(65536) : rb(70000);
		A[1] = rb(3) ? rb(65536) : rb(4);
		if (rb(4) == 0) { A[0] = A[1] + rb(3) - 1; }
		if (rb(4) == 0) { A[0] = rnd() | 0xffff0000u; A[1] = rnd() | 0x10000u; }
		if (rb(5) == 0) { A[0] = 0; A[1] = 0xffff; }
		if (rb(5) == 0) { A[0] = 1; A[1] = (rb(2) ? 1u : 0u) + (65536u >> rb(17)) - rb(2); }
		NA = 2;
		break;
	case F_INTERP: case F_INTERP1:
		A[0] = (u32)wide(); A[1] = (rb(5) == 0) ? A[0] : (u32)wide();
		if (rb(6) == 0) A[1] = A[0] - 1;
		A[2] = (u32)wide(); A[3] = (u32)wide(); A[4] = (u32)wide();
		if (rb(10) == 0) {	/* span -1 and numerator INT_MIN + 1: the INT_MIN / -1 division */
			if (f == F_INTERP) { A[1] = A[0] - 1; A[4] = A[0] + 0x80000001u; }
			else { A[0] = A[1] - 1; A[4] = A[0] - 0x80000001u; }
			A[2] = 0; A[3] = 1;
		}
		if (rb(3) == 0) { A[0] = rb(200); A[1] = A[0] + rb(100); A[4] = A[0] + rb(120) - 10; A[2] = rb(500); A[3] = rb(500); }
		NA = 5;
		break;
	case F_ABS:
		A[0] = (u32)wide(); A[1] = (rb(6) == 0) ? A[0] : (u32)wide();
		NA = 4;
		break;
	case F_RAMY: case F_REGRD:
		NA = 4;
		break;
	case F_5X5:
		o = lay(31 * 2);
		axis16(o, 31, 0);
		if (rb(3) == 0) axis16(o, 31, 2);
		A[0] = (u32)(rb(2) ? (int)rb(400) - 200 : wide()); A[1] = (u32)(rb(2) ? (int)rb(400) - 200 : wide());
		A[2] = (u32)(rb(2) ? (int)rb(400) - 200 : wide()); A[3] = (u32)(rb(2) ? (int)rb(400) - 200 : wide());
		if (rb(3) == 0) {	/* a table entry equal to the distance, or just around it */
			int dx = (int)((int)A[2] < (int)A[0] ? A[0] - A[2] : A[2] - A[0]) / 8;
			int dy = (int)((int)A[3] < (int)A[1] ? A[1] - A[3] : A[3] - A[1]) / 8;
			put16(o, rb(31), dx * dx + dy * dy + (int)rb(3) - 1);
		}
		if (rb(8) == 0) for (i = 0; i < 31; i++) put16(o, i, rb(2) ? -5 : 32767);
		OFF[0] = o;
		NA = 5;
		break;
	case F_SHORT:
		n = rb(10) == 0 ? rb(30) : rb(8);
		A[4] = rb(12) == 0 ? (u32)(int)(-(int)rb(3)) : n;
		A[4] |= rb(2) ? rnd() << 16 : 0;
		A[5] = rb(12);
		if (rb(10) == 0) A[5] = (u32)-(int)rb(2);
		A[5] |= rb(2) ? rnd() << 16 : 0;
		OFF[0] = lay(60 * 2);	/* xs */
		OFF[1] = lay(60 * 2);	/* ys */
		OFF[2] = lay(16 * 2);	/* in */
		OFF[3] = lay(16 * 2);	/* out */
		axis16(OFF[0], 40, rb(4) == 0 ? 1 : 0);
		if (rb(5) == 0) axis16(OFF[0], 40, 2);
		axis16(OFF[1], 40, rb(3));
		for (i = 0; i < 14; i++)
			put16(OFF[2], i, rb(5) == 0 ? (int)(s16)rnd() : pick16(OFF[0], n ? n : 1));
		A[0] = A[1] = A[2] = A[3] = 0;
		NA = 6;
		break;
	case F_I16:
		n = 1 + rb(rb(6) == 0 ? 20 : 8);
		OFF[0] = lay(24 * 2);
		OFF[1] = lay(24 * 2);
		axis16(OFF[0], 24, rb(5) == 0 ? 1 : rb(8) == 0 ? 2 : 0);
		axis16(OFF[1], 24, rb(3));
		A[0] = (u32)pick16(OFF[0], n);
		A[3] = n;
		NA = 4;
		break;
	case F_I32:
		n = 1 + rb(rb(6) == 0 ? 20 : 8);
		OFF[0] = lay(24 * 4);
		OFF[1] = lay(24 * 2);
		axis32(OFF[0], 24, rb(5) == 0 ? 1 : rb(8) == 0 ? 2 : 0);
		axis16(OFF[1], 24, rb(3));
		{
			int t;

			memcpy(&t, IMG + OFF[0] + 4 * rb(n), 4);
			A[0] = rb(4) == 0 ? (u32)wide() : (u32)(t + (int)rb(5) - 2);
		}
		A[3] = n;
		NA = 4;
		break;
	case F_I64:
		n = 1 + rb(rb(6) == 0 ? 20 : 8);
		OFF[0] = lay(24 * 8);
		OFF[1] = lay(24 * 2);
		axis64(OFF[0], 24, rb(5) == 0 ? 1 : rb(8) == 0 ? 2 : 0);
		axis16(OFF[1], 24, rb(3));
		{
			long long t, x;

			memcpy(&t, IMG + OFF[0] + 8 * rb(n), 8);
			x = rb(4) == 0 ? (long long)((unsigned long long)rnd() << 32 | rnd()) : t + (long long)rb(5) - 2;
			if (rb(6) == 0) x = t + (long long)(int)rnd() * 1000;
			A[0] = (u32)x; A[1] = (u32)((unsigned long long)x >> 32);
		}
		A[4] = n;
		NA = 5;
		break;
	case F_SUB:
		OFF[0] = lay(130 * 2);	/* a: [-1] .. [128] read, frame in front */
		OFF[1] = lay(130 * 2);
		OFF[2] = lay(512 * 4);
		put16(OFF[0], 0, 0); put16(OFF[1], 0, 0);
		{
			int v = (int)rb(60) - 30, step = rb(3) == 0 ? 2 : 20;

			for (i = 0; i < 130; i++) {
				put16(OFF[0], i, v);
				v += (rb(10) == 0 && step < 20) ? 0 : (int)rb((u32)step) + (rb(40) == 0 ? 0 : 1);
				put16(OFF[1], i, rb(3) == 0 ? (int)(s16)rnd() : (int)(i * 7 + rb(50)));
			}
			A[0] = (u32)(rb(3) == 0 ? wide() : (int)rb(1500) - 100);
			for (i = 0; i < 512; i++)
				put32(OFF[2], i, rb(3) == 0 ? (int)A[0] + (int)rb(4) - 2 : rb(2) ? (int)rb(3000) : wide());
			if (rb(5) == 0) for (i = 0; i < 512; i++) put32(OFF[2], i, (int)A[0] + (int)(i & 1));
			if (rb(8) == 0) for (i = 0; i < 512; i++) put32(OFF[2], i, 0x7fffff00 - (int)i);
		}
		A[1] = (u32)(rb(2) ? (int)rb(300) - 50 : wide());
		A[2] = (u32)(rb(2) ? (int)rb(300) - 50 : wide());
		A[6] = rb(8) == 0 ? (u32)wide() : rb(4);
		A[7] = rb(10) == 0 ? rb(40) : rb(14);
		A[8] = rb(10) == 0 ? rb(40) : rb(14);
		A[9] = rb(3) == 0 ? 1 : rb(3);
		NA = 10;
		break;
	case F_SIZE: {
		static const u32 cells[] = { 0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 63, 64, 65, 127, 128, 255, 256, 511, 512, 1023,
					     1024, 2047, 2048, 4095, 4096, 8191, 8192, 16383, 16384, 32767, 32768, 32769,
					     65535, 65536, 65537, 70000 };
		u32 gw, gh, t = cells[rb(sizeof(cells) / sizeof(cells[0]))];

		CH = rb(4);
		for (i = 0; i < 4; i++) {
			OFF[i] = lay(sizeof(struct t32_tmo_ctx) + 16);
		}
		o = lay(4 * sizeof(struct t32_tmo_stat) + 8);
		n = lay(4 * sizeof(struct t32_tmo_geom) + 8);
		OFF[4] = o; OFF[5] = n;
		OFF[6] = lay(8);
		gw = 1 + rb(rb(2) ? 40 : 700);
		gh = t / gw + rb(3) - 1;
		if (rb(5) == 0) { gw = rb(2000); gh = rb(2000); }
		{
			u32 w = gw * 15 + rb(15) - 7, h = gh * 25 + rb(25) - 12;

			if (rb(4) == 0) { w = rnd(); h = rnd(); }
			put32(OFF[6], 0, (int)h);
			put32(OFF[6], 1, (int)w);
		}
		A[0] = CH;
		NA = 4;
		break;
	}
	case F_TGAIN:
		OFF[0] = lay(8 + 8 + 16);
		for (i = 0; i < 8; i++)
			put16(OFF[0] + 16, i, rb(3) == 0 ? 1 : (int)rb(4) - 1);
		A[0] = rb(8);
		NA = 4;
		break;
	}
}

/* ---- sides ---- */
static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 P0 = T32O_PTR(b + OFF[0]), P1 = T32O_PTR(b + OFF[1]), P2 = T32O_PTR(b + OFF[2]), P3 = T32O_PTR(b + OFF[3]);

	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	switch (f) {
	case F_LOG2: return t32o_call(FN(tisp_tmo_log2), A[0], A[1], 0, 0, 0);
	case F_INTERP: return t32o_call(FN(tisp_tmo_tmo_interplate), A[0], A[1], A[2], A[3], A[4]);
	case F_INTERP1: return t32o_call(FN(tisp_tmo_tmo_interplate1), A[0], A[1], A[2], A[3], A[4]);
	case F_ABS: return t32o_call(FN(tisp_tmo_tmo_absfun), A[0], A[1], 0, 0, 0);
	case F_RAMY: return t32o_call(FN(tisp_tmo_ramY_reg_read), 0, 0, 0, 0, 0);
	case F_REGRD: return t32o_call(FN(tisp_tmo_tmo_reg_read), 0, 0, 0, 0, 0);
	case F_5X5: return t32o_call(FN(tisp_tmo_adr_5x5_out), A[0], A[1], A[2], A[3], P0);
	case F_SHORT:
		t32o_arg5 = A[5];
		return t32o_call(FN(tisp_tmo_adr_interp1_short), P0, P1, P2, P3, A[4]);
	case F_I16: return t32o_call(FN(tisp_tmo_adr_ispint_adr_16), A[0], P0, P1, A[3], 0);
	case F_I32: return t32o_call(FN(tisp_tmo_adr_ispint_adr_32), A[0], P0, P1, A[3], 0);
	case F_I64: return t32o_call(FN(tisp_tmo_adr_ispint_adr_64), A[0], A[1], P0, P1, A[4]);
	case F_SUB:
		t32o_arg5 = T32O_PTR(b + OFF[2]);	/* arg5: table */
		t32o_arg6 = A[6]; t32o_arg7 = A[7]; t32o_arg8 = A[8]; t32o_arg9 = A[9];
		return t32o_call(FN(tisp_tmo_adr_subsection_map), A[0], A[1], A[2], P0, P1);
	case F_SIZE: {
		u32 *rec = t32o_alloc(4 * 16);
		u32 i;

		for (i = 0; i < 4; i++) {
			rec[i * 4 + 0] = T32O_PTR(b + OFF[i]);
			rec[i * 4 + 1] = T32O_PTR(b + OFF[4] + i * sizeof(struct t32_tmo_stat));
			rec[i * 4 + 2] = 0x12345678u;
			rec[i * 4 + 3] = T32O_PTR(b + OFF[5] + i * sizeof(struct t32_tmo_geom));
			t32_data_e35e0[i] = T32O_PTR(&rec[i * 4]);
		}
		return t32o_call(FN(tisp_tmo_tmo_size_update), CH, T32O_PTR(b + OFF[6]), 0, 0, 0);
	}
	default:
		memcpy(t32_data_e35a0, b + OFF[0], 8);
		memcpy(t32_data_e3590, b + OFF[0] + 8, 8);
		memcpy(t32_data_e3520, b + OFF[0] + 16, 16);
		{
			u64 r = t32o_call(FN(tisp_tmo_adr_tgain_func), A[0], 0, 0, 0, 0);

			memcpy(b + OFF[0], t32_data_e35a0, 8);
			memcpy(b + OFF[0] + 8, t32_data_e3590, 8);
			memcpy(b + OFF[0] + 16, t32_data_e3520, 16);
			return r;
		}
	}
}

static u64 run_ours(int f)
{
	u8 *b = OA;

	switch (f) {
	case F_LOG2: return t32_tmo_log2(A[0], A[1]);
	case F_INTERP: return (u32)t32_tmo_interplate((int)A[0], (int)A[1], (int)A[2], (int)A[3], (int)A[4]);
	case F_INTERP1: return (u32)t32_tmo_interplate1((int)A[0], (int)A[1], (int)A[2], (int)A[3], (int)A[4]);
	case F_ABS: return t32_tmo_absfun(A[0], A[1]);
	case F_RAMY: return t32_tmo_ramy_reg_read();
	case F_REGRD: return t32_tmo_reg_read();
	case F_5X5: return t32_tmo_adr_5x5_out((int)A[0], (int)A[1], (int)A[2], (int)A[3], (const s16 *)(b + OFF[0]));
	case F_SHORT:
		t32_tmo_adr_interp1_short((const s16 *)(b + OFF[0]), (const s16 *)(b + OFF[1]), (const s16 *)(b + OFF[2]),
					  (s16 *)(b + OFF[3]), (int)A[4], (int)A[5]);
		return 0;
	case F_I16: return (u32)t32_tmo_adr_ispint_16((int)A[0], (const s16 *)(b + OFF[0]), (const s16 *)(b + OFF[1]), (int)A[3]);
	case F_I32: return (u32)t32_tmo_adr_ispint_32((int)A[0], (const int *)(b + OFF[0]), (const s16 *)(b + OFF[1]), (int)A[3]);
	case F_I64:
		return (u32)t32_tmo_adr_ispint_64((long long)((unsigned long long)A[1] << 32 | A[0]),
						  (const long long *)(b + OFF[0]), (const s16 *)(b + OFF[1]), (int)A[4]);
	case F_SUB:
		return (u32)t32_tmo_adr_subsection_map((int)A[0], (int)A[1], (int)A[2], (const s16 *)(b + OFF[0]),
						       (const s16 *)(b + OFF[1]), (const int *)(b + OFF[2]), (int)A[6], A[7],
						       A[8], A[9]);
	case F_SIZE: {
		const u32 *sz = (const u32 *)(b + OFF[6]);

		t32_tmo_size_update((struct t32_tmo_ctx *)(b + OFF[CH]),
				    (struct t32_tmo_stat *)(b + OFF[4]) + CH,
				    (struct t32_tmo_geom *)(b + OFF[5]) + CH, sz[1], sz[0]);
		return 0;
	}
	default: {
		struct t32_tmo_tgain t;

		memcpy(&t, b + OFF[0], 16);
		t.flag = (s16 *)(b + OFF[0] + 16);
		t32_tmo_adr_tgain_func(&t, A[0]);
		memcpy(b + OFF[0], &t, 16);	/* struct t32_tmo_tgain starts with the two u32 pairs */
		return 0;
	}
	}
}

static int tgain_layout_ok(void)
{
	return offsetof(struct t32_tmo_tgain, b) == 8;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int f, c, compared = 0;
	u32 sel_zero = 0, sel_any = 0;

	if (!tgain_layout_ok() || sizeof(struct t32_tmo_geom) != 20 || sizeof(struct t32_tmo_stat) != 54 ||
	    sizeof(struct t32_tmo_ctx) != 10040) {
		printf("struct layout\n");
		return 1;
	}
	t32o_reset();
	VA = t32o_alloc(ARN);
	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			u64 rv, ro;

			t32o_reset();
			VA = t32o_alloc(ARN);
			gen((int)f);
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			rv = run_vendor((int)f);
			ro = run_ours((int)f);
			compared++;
			if (f == F_SHORT)
				rv = ro = 0;	/* void, the return is a leftover register */
			if (f == F_SIZE || f == F_TGAIN)
				rv = ro = 0;	/* void or a leftover register */
			if ((u32)rv != (u32)ro) {
				printf("MISMATCH %s case %u: return vendor %08x ours %08x\n", fname[f], c, (u32)rv, (u32)ro);
				return 1;
			}
			if (memcmp(VA, OA, TOT)) {
				u32 i;

				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u vendor %02x ours %02x\n", fname[f], c, i, VA[i], OA[i]);
				return 1;
			}
			if (f == F_SIZE) {
				u8 s = OA[OFF[CH] + 10039];

				sel_any++;
				if (s == 0)
					sel_zero++;
				if (s != 0 && (s < 13 || s > 25)) {
					printf("MISMATCH size_update: LCE selector %u out of 13..25\n", s);
					return 1;
				}
			}
		}
	if (sel_zero == 0 || sel_any == sel_zero) {
		printf("size_update: selector 0 (L_90908) %u of %u cases, not both paths\n", sel_zero, sel_any);
		return 1;
	}
	printf("t32_tmo1_oracle: %u cases compared (14 functions), 0 mismatches (selector 0 in %u of %u size_update cases)\n",
	       compared, sel_zero, sel_any);
	return 0;
}
