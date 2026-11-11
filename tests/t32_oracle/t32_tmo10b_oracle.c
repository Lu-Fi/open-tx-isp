// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 10b: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor function
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_tmo_stat_limit.  The tone buffer pair (15040 bytes) is filled with values
 * around every clamp band edge (and full random u16), the context fields (mode 1 / 2 / other, blend on / off, weight incl. > 16, record byte 53)
 * are random; private_dma_cache_sync (count and last arguments) and the whole arena (canary framed, inputs and decoy channels) are compared.
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
#define ARN	(1u << 18)
#define CTXN	3600u

WEAK u32 t32_data_e35e0[4];
extern u32 t32o_dma_n, t32o_dma_a[4];
u64 tisp_tmo_tmo_stat_limit(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

static u32 rs = 0x4b17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 OC[4], OT[4], TOT, cur, CH, R53[4];
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

static u32 SN, SA0, SA1, SA2, SA3, SL;
static void sync_cb(u8 *buf, u32 len) { SN++; SA1 = (u32)(buf - OA); SA2 = len; }

static void gen(void)
{
	static const u32 e[] = { 0, 410, 819, 1229, 1638, 2048, 2457, 2867, 3276, 3686, 4095, 4096 };
	u32 i, k;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 4; i++) {
		OC[i] = lay(CTXN);
		OT[i] = lay(15040 + 16);
		p16(OC[i] + 3382, rb(5) ? 1 + rb(2) : rb(2) ? 0 : rnd());
		p16(OC[i] + 3366, rb(4) ? 1 : rnd());
		p16(OC[i] + 3368, rb(3) ? rb(17) : rnd());
		R53[i] = rb(4) ? 1 : rb(256);
		for (k = 0; k < 15040; k += 2) {
			u32 v;

			switch (rb(5)) {
			case 0: v = rnd(); break;
			case 1: v = e[rb(12)] + rb(3) - 1; break;
			case 2: v = e[rb(12)]; break;
			default: v = rb(4200); break;
			}
			p16(OT[i] + k, v);
		}
	}
	CH = rb(4);
}

static void run_vendor(void)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), i;

	for (i = 0; i < 4; i++) {
		rec[i * 16] = T32O_PTR(b + OC[i]);
		rec[i * 16 + 5] = T32O_PTR(b + OT[i]);
		((u8 *)&rec[i * 16])[53] = (u8)R53[i];
		t32_data_e35e0[i] = T32O_PTR(&rec[i * 16]);
	}
	t32o_dma_n = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_tmo_tmo_stat_limit, CH, 0, 0, 0, 0);
	SL = ((u8 *)&rec[CH * 16])[53];
	SA0 = t32o_dma_n;
	SA3 = t32o_dma_a[1] - T32O_PTR(VA);
	SA2 = t32o_dma_a[2];
	if (t32o_dma_a[0] != 0 || t32o_dma_a[3] != 0)
		SA0 += 1000;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, compared = 0;

	for (c = 0; c < n; c++) {
		u32 i, vn, va, vl, vl53;
		u8 r53;

		t32o_reset();
		VA = t32o_alloc(ARN);
		gen();
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		run_vendor();
		vn = SA0; va = SA3; vl = SA2; vl53 = SL;
		r53 = (u8)R53[CH];
		SN = 0; SA1 = 0; SA2 = 0;
		t32_tmo_stat_limit(OA + OT[CH], OA + OC[CH], &r53, sync_cb);
		compared++;
		if (vn != SN || (SN && (va != SA1 || vl != SA2)) || vl53 != r53) {
			printf("MISMATCH case %u: dma %u / %u (%u %u / %u %u), rec53 %u / %u\n", c, vn, SN, va, vl, SA1, SA2, vl53, r53);
			return 1;
		}
		if (memcmp(VA, OA, TOT)) {
			for (i = 0; i < TOT && VA[i] == OA[i]; i++)
				;
			printf("MISMATCH case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", c, i, CH, VA[i], OA[i]);
			return 1;
		}
	}
	printf("t32_tmo10b_oracle: %u cases compared (tmo_stat_limit), 0 mismatches\n", compared);
	return 0;
}
