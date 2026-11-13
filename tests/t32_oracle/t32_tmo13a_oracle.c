// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 13a: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor function
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_adr_algorithm, with the vendor tisp_tmo_adr_interp1_short ref against our port.
 * Four channels, canary framed arena, the x curve (ctx + 9272, 129 s16, ascending / random), the vendor tables t32_data_c04b8 (7 s16) and d87a0
 * (129 s16) random; tisp_tmo_adr_fpga is a recording stub (vendor) / hook (ours: channel compared).  The vendor's printk (t32_data_e3516 % 1000
 * == 0) is dropped in ours: its count is compared with that condition.  The whole arena and the e3516 value are compared.
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
WEAK u8 t32_data_c04b8[16];
WEAK u8 t32_data_d87a0[260];
WEAK u16 t32_data_e3516[2];
WEAK u8 t32_data_ca408[64];
u64 tisp_tmo_adr_algorithm(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

static u32 VH, OH;
u64 tisp_tmo_adr_fpga(u32 ch) { VH = ch + 1; return 0; }
static void h_fpga(void *c, int ch) { (void)c; OH = (u32)ch + 1; }

static u32 rs = 0x8c17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 OC[4], TOT, cur, CH, E16;
static u8 TC[16], TD[260];
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

static void gen(void)
{
	u32 i, k, v;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		v = rb(2000);
		for (k = 0; k < 129; k++) {	/* x curve, mostly ascending with duplicates / garbage */
			p16(OC[i] + 9272 + 2 * k, rb(20) == 0 ? rnd() : v);
			v += rb(3) ? rb(40) : 0;
		}
	}
	for (k = 0; k < 7; k++) {
		v = rb(4) ? rb(5200) : rnd();
		memcpy(TC + 2 * k, &(u16){ (u16)v }, 2);
	}
	for (k = 0; k < 129; k++) {
		v = rb(5) ? rb(4096) : rnd();
		memcpy(TD + 2 * k, &(u16){ (u16)v }, 2);
	}
	CH = rb(4);
	E16 = rb(3) == 0 ? 1000u * rb(66) : rb(2) ? rb(65536) : 1000u * rb(65) + rb(3) - 1;
	E16 &= 0xffff;
}

static u64 run_vendor(void)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), i;

	for (i = 0; i < 4; i++) {
		rec[i * 16] = T32O_PTR(b + OC[i]);
		t32_data_e35e0[i] = T32O_PTR(&rec[i * 16]);
	}
	memcpy(t32_data_c04b8, TC, 14);
	memcpy(t32_data_d87a0, TD, 258);
	t32_data_e3516[0] = (u16)E16;
	VH = 0;
	return t32o_call(FN(tisp_tmo_adr_algorithm), CH, 0, 0, 0, 0);
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, compared = 0;

	for (c = 0; c < n; c++) {
		u32 i, pf;

		t32o_reset();
		t32o_log.printfs = 0;
		VA = t32o_alloc(ARN);
		gen();
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		if ((u32)run_vendor() != 0) { printf("vendor returned != 0\n"); return 1; }
		pf = t32o_log.printfs;
		OH = 0;
		t32_tmo_adr_algorithm(OA + OC[CH], (const s16 *)TC, (const s16 *)TD, h_fpga, 0, (int)CH);
		compared++;
		if (VH != OH || pf != (E16 % 1000 == 0)) { printf("MISMATCH case %u: fpga hook %u / %u, printk %u (e3516 %u)\n", c, VH, OH, pf, E16); return 1; }
		if (memcmp(VA, OA, TOT)) {
			for (i = 0; i < TOT && VA[i] == OA[i]; i++)
				;
			printf("MISMATCH case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", c, i, CH, VA[i], OA[i]);
			return 1;
		}
	}
	printf("t32_tmo13a_oracle: %u cases compared (adr_algorithm), 0 mismatches\n", compared);
	return 0;
}
