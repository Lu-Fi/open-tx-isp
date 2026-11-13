// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 9b: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_malloc_cfg, tisp_tmo_fbf_malloc_cfg, tisp_tmo_faceae_refresh.
 * Canary framed arena, four channels of slots (t32_data_e35d0 / e35c0 / e35e0, decoy slots compared).  private_vmalloc /
 * private_kmalloc are recording stubs on both sides (size, flags, order; kmalloc returns a fixed fake address or NULL, vmalloc
 * a block of the arena); the register writes are the oracle's vendor tisp_reg_write (ref/oa_reg_write.c: direct for d03a0 != 1,
 * tagged 0x80000000 | ch << 24 | reg for d03a0 == 1) against our regw callback: register, value, order.  tisp_tmo_faceae_reg_refresh
 * is a recording stub (hook).  The vendor's isp_printf (vmalloc / kmalloc NULL, tbuf == 0) is dropped in ours: its count is checked
 * against that condition (a NULL vmalloc result is a NULL dereference in the vendor and is not generated).
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
#define ARN	(1u << 14)

WEAK u32 t32_data_e35e0[4];
WEAK u32 t32_data_e35d0[4];
WEAK u32 t32_data_e35c0[4];
WEAK u8 t32_data_d03a0[4];
WEAK u8 t32_data_ca38c[64];
WEAK u8 t32_data_ca3a4[64];

u64 tisp_tmo_malloc_cfg(u32, u32, u32, u32);
u64 tisp_tmo_fbf_malloc_cfg(u32, u32, u32, u32);
u64 tisp_tmo_faceae_refresh(u32, u32, u32, u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_MALLOC, F_FBF, F_FACE, F_N };
static const char *const fname[F_N] = { "malloc_cfg", "fbf_malloc_cfg", "faceae_refresh" };

static u32 rs = 0x7a17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

/* arena layout */
static u32 OV[2], OCFG, OST, OSRC[5], TOT, cur, CH, KFAKE, TBUF, FLAG, KNULL;
static u8 D03, IMG[ARN];
static u8 *VA;
static u8 OA[ARN] __attribute__((aligned(16)));
static u32 SV[4][3], SO[4][3];	/* slots e35d0 / e35c0 / e35e0 (vendor / ours) as offsets from the arena base; e35e0 only compared */

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

/* logs: allocator calls (kind << 28 | size, flags) and register writes */
static u32 VA_LOG[8][2], VA_N, OA_LOG[8][2], OA_N, OW[8][2], OWN, VH, OH;
static int ALLOC_BASE_ARENA;
static void *vm_v(u32 n) { VA_LOG[VA_N < 8 ? VA_N : 7][0] = 1u << 28 | n; VA_LOG[VA_N < 8 ? VA_N : 7][1] = 0; VA_N++; return VA + OV[VA_N == 1 ? 0 : 1]; }
u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; if (!ALLOC_BASE_ARENA) return a0 ? (u32)(uintptr_t)t32o_alloc(a0) : 0; return (u32)(uintptr_t)vm_v(a0); }
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (!ALLOC_BASE_ARENA)
		return a0 ? (u32)(uintptr_t)t32o_alloc(a0) : 0;
	VA_LOG[VA_N < 8 ? VA_N : 7][0] = 2u << 28 | a0;
	VA_LOG[VA_N < 8 ? VA_N : 7][1] = a1;
	VA_N++;
	return KNULL ? 0 : KFAKE;
}
u64 tisp_tmo_faceae_reg_refresh(u32 ch) { VH = ch + 1; return 0; }
static void h_face(void *c, int ch) { (void)c; OH = (u32)ch + 1; }
static u32 OVM_N;
static void *o_vm(void *c, u32 n)
{
	(void)c;
	OA_LOG[OA_N < 8 ? OA_N : 7][0] = 1u << 28 | n;
	OA_LOG[OA_N < 8 ? OA_N : 7][1] = 0;
	OA_N++;
	return OA + OV[OVM_N++ ? 1 : 0];
}
static void *o_km(void *c, u32 n, u32 fl)
{
	(void)c;
	OA_LOG[OA_N < 8 ? OA_N : 7][0] = 2u << 28 | n;
	OA_LOG[OA_N < 8 ? OA_N : 7][1] = fl;
	OA_N++;
	return KNULL ? 0 : (void *)(uintptr_t)KFAKE;
}
static void o_regw(void *c, int ch, u32 reg, u32 val)
{
	(void)c; (void)ch;
	if (OWN < 8) { OW[OWN][0] = D03 == 1 ? 0x80000000u | ((u32)ch & 7) << 24 | reg : reg; OW[OWN][1] = val; }
	OWN++;
}

static void gen(int f)
{
	u32 i;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	OV[0] = lay(32);
	OV[1] = lay(32);
	OCFG = lay(16);
	OST = lay(1400);
	for (i = 0; i < 5; i++)
		OSRC[i] = lay(16);
	CH = rb(4);
	KNULL = rb(4) == 0;
	KFAKE = 0x1000u + rnd() % 0x0f000000u;
	TBUF = rb(4) == 0 ? 0 : rb(2) ? rnd() : 0x70000000u + rb(1u << 28);
	FLAG = rb(2) ? rb(256) : rnd();
	D03 = rb(3) ? (u8)rb(2) : (u8)rnd();
	for (i = 0; i < 4; i++)
		SV[i][0] = SV[i][1] = SV[i][2] = 0;
	(void)f;
}

static u64 run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), *cfg = (u32 *)(b + OCFG), i;

	for (i = 0; i < 4; i++) {
		t32_data_e35d0[i] = 0x11110000u + i;	/* decoy slot values */
		t32_data_e35c0[i] = 0x22220000u + i;
		t32_data_e35e0[i] = T32O_PTR(&rec[i * 16]);
		rec[i * 16 + 1] = T32O_PTR(b + OST);
		rec[i * 16 + 5] = TBUF;
	}
	if (f == F_FBF)
		t32_data_e35d0[CH] = T32O_PTR(cfg);
	t32_data_d03a0[0] = D03;
	t32o_arg5 = T32O_PTR(b + OSRC[3]);
	t32o_arg6 = T32O_PTR(b + OSRC[4]);
	t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	VA_N = VH = 0;
	switch (f) {
	case F_MALLOC: t32o_call(FN(tisp_tmo_malloc_cfg), CH, 0, 0, 0, 0); break;
	case F_FBF: t32o_call(FN(tisp_tmo_fbf_malloc_cfg), CH, 0, 0, 0, 0); break;
	default: t32o_call(FN(tisp_tmo_faceae_refresh), CH, FLAG, T32O_PTR(b + OSRC[0]), T32O_PTR(b + OSRC[1]), T32O_PTR(b + OSRC[2])); break;
	}
	for (i = 0; i < 4; i++) {
		SV[i][0] = t32_data_e35d0[i];
		SV[i][1] = t32_data_e35c0[i];
	}
	return 0;
}

static void run_ours(int f)
{
	u32 sl[2][4], i;
	u32 *cfg = (u32 *)(OA + OCFG);

	for (i = 0; i < 4; i++) {
		sl[0][i] = 0x11110000u + i;
		sl[1][i] = 0x22220000u + i;
	}
	OA_N = OWN = OH = OVM_N = 0;
	switch (f) {
	case F_MALLOC: t32_tmo_malloc_cfg(&sl[0][CH], (int)CH, o_vm, o_km, o_regw, D03, 0); break;
	case F_FBF:
		sl[0][CH] = (u32)(uintptr_t)cfg;
		t32_tmo_fbf_malloc_cfg(&sl[1][CH], cfg, TBUF, o_vm, 0);
		break;
	default:
		t32_tmo_faceae_refresh(OA + OST, FLAG, OA + OSRC[0], OA + OSRC[1], OA + OSRC[2], OA + OSRC[3], OA + OSRC[4], h_face, 0, (int)CH);
		break;
	}
	for (i = 0; i < 4; i++) {
		SO[i][0] = sl[0][i];
		SO[i][1] = sl[1][i];
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			u32 i, np;

			t32o_reset();
			t32o_log.n = 0;
			t32o_log.printfs = 0;
			VA = t32o_alloc(ARN);
			gen((int)f);
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			ALLOC_BASE_ARENA = 1;
			run_vendor((int)f);
			run_ours((int)f);
			compared++;
			if (VA_N != OA_N || memcmp(VA_LOG, OA_LOG, 8 * OA_N) || VH != OH) {
				printf("MISMATCH %s case %u: allocator calls %u / %u or hook %u / %u\n", fname[f], c, VA_N, OA_N, VH, OH);
				return 1;
			}
			if (f != F_FACE && f != F_FBF) {
				if (t32o_log.n != OWN || OWN != 5) { printf("MISMATCH %s register write count %u / %u\n", fname[f], t32o_log.n, OWN); return 1; }
				for (i = 0; i < OWN; i++)
					if (t32o_log.w[i].reg != OW[i][0] || t32o_log.w[i].val != OW[i][1]) {
						printf("MISMATCH %s write %u: %x=%x vs %x=%x\n", fname[f], i, t32o_log.w[i].reg, t32o_log.w[i].val, OW[i][0], OW[i][1]);
						return 1;
					}
			} else if (t32o_log.n) { printf("MISMATCH %s: register writes %u\n", fname[f], t32o_log.n); return 1; }
			np = f == F_MALLOC ? KNULL : f == F_FBF ? TBUF == 0 : 0;
			if (t32o_log.printfs != np) { printf("MISMATCH %s printf count %u expect %u\n", fname[f], t32o_log.printfs, np); return 1; }
			for (i = 0; i < 4; i++) {	/* slots: decoys unchanged, the written one = the arena block (offset compare) */
				u32 a = SV[i][0], b2 = SO[i][0], c2 = SV[i][1], d2 = SO[i][1];

				if (f == F_FBF && i == CH) a = b2 = 0;	/* the e35d0 slot is the cfg input */
				if (f == F_MALLOC && i == CH) { a -= (u32)(uintptr_t)VA; b2 -= (u32)(uintptr_t)OA; }
				if (f == F_FBF && i == CH) { c2 -= (u32)(uintptr_t)VA; d2 -= (u32)(uintptr_t)OA; }
				if (f != F_FACE && (a != b2 || c2 != d2)) {
					printf("MISMATCH %s case %u: slot %u: %x %x vs %x %x\n", fname[f], c, i, a, c2, b2, d2);
					return 1;
				}
				if (f == F_MALLOC && i != CH && (SV[i][0] != 0x11110000u + i || SV[i][1] != 0x22220000u + i)) { printf("MISMATCH decoy slot\n"); return 1; }
			}
			if (memcmp(VA, OA, TOT)) {
				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", fname[f], c, i, CH, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo9b_oracle: %u cases compared (3 functions), 0 mismatches\n", compared);
	return 0;
}
