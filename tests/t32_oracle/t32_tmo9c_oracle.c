// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 9c: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_deinit and tisp_tmo_interrupt_static (dn_params_refresh is in
 * t32_tmo9a_oracle, it shares the refresh tail with the setters).  Canary framed arena, four channels of slots.
 * deinit: private_vfree / private_kfree are recording stubs on both sides (kind and address, arena addresses as offsets, in order);
 * the slots (e35d0 / e35c0 / e35e0 / e35b0 of the channel written, decoys unchanged) and the allocation records (members cleared) are compared.
 * interrupt_static: system_reg_read (28952, scripted value), private_dma_cache_sync (arguments), the unported callees
 * tmoadr_get_statistics (with its buffer argument), tmo2fbf_input_prep, tmo2fbf_process and adr_soft_process are recording stubs on the
 * vendor side and hooks on ours; the counters e3516 / e350c / e3504 and the record fields +44 / +46 / +50 (all channels) are compared.
 * The vendor's returns: deinit 0 (compared), interrupt_static 1 (compared).
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
#define ARN	(1u << 13)

WEAK u32 t32_data_e35e0[4];
WEAK u32 t32_data_e35d0[4];
WEAK u32 t32_data_e35c0[4];
WEAK u32 t32_data_e35b0[4];
WEAK u16 t32_data_e3516[2];
WEAK u32 t32_data_e350c[2];
WEAK u32 t32_data_e3504[2];
extern u32 t32o_dma_n, t32o_dma_a[4];

u64 tisp_tmo_deinit(u32, u32, u32, u32);
u64 tisp_tmo_interrupt_static(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_DEINIT, F_INT, F_N };
static const char *const fname[F_N] = { "deinit", "interrupt_static" };

static u32 rs = 0x3c17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 oBUFV, VE16, VE0C, VE04;
static u32 OP[4], OR[4], OB[4], OST[4], OBUF, TOT, cur, CH, RDV, KX, SLOT[4][4], E3516, E350C, E3504;
static u16 RECF[4][3], RECV[4][3];
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
static void w32(u32 o, u32 v) { memcpy(IMG + o, &v, 4); }
static u32 fake(void) { return rb(3) ? 0x30000000u + (rnd() & 0xffffff0u) : 0; }

/* logs: free stubs (kind, normalised address), unported callees (id << 8 | ch, arg) */
static u32 VL[24][2], VLN, OL[24][2], OLN, CL[8][2], CLN, HL[8][2], HLN, RDN_V, RDN_O, VDN, VDA, ODN, ODA;
static u8 *VBASE, *OBASE;
static u32 norm(u32 v, u8 *base) { return v >= (u32)(uintptr_t)base && v < (u32)(uintptr_t)base + ARN ? v - (u32)(uintptr_t)base + 0x100u : v; }
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; if (VLN < 24) { VL[VLN][0] = 1; VL[VLN][1] = norm(a0, VBASE); } VLN++; return 0; }
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; if (VLN < 24) { VL[VLN][0] = 2; VL[VLN][1] = norm(a0, VBASE); } VLN++; return 0; }
static void o_vfree(void *c, u32 v) { (void)c; if (OLN < 24) { OL[OLN][0] = 1; OL[OLN][1] = norm(v, OBASE); } OLN++; }
static void o_kfree(void *c, u32 v) { (void)c; if (OLN < 24) { OL[OLN][0] = 2; OL[OLN][1] = norm(v, OBASE); } OLN++; }
static void crec(u32 id, u32 ch, u32 arg) { if (CLN < 8) { CL[CLN][0] = id << 8 | ch; CL[CLN][1] = arg; } CLN++; }
u64 tisp_tmo_tmoadr_get_statistics(u32 ch, u32 buf) { crec(0, ch, buf); return 0; }
u64 tisp_tmo_tmo_tmo2fbf_input_prep(u32 ch) { crec(1, ch, 0); return 0; }
u64 tisp_tmo_tmo_tmo2fbf_process(u32 ch) { crec(2, ch, 0); return 0; }
u64 tisp_tmo_adr_soft_process(u32 ch) { crec(3, ch, 0); return 0; }
static u32 rdhook(u32 reg) { RDN_V = reg; return RDV; }
static void h_int(void *c, int id, int ch, u32 arg) { (void)c; if (HLN < 8) { HL[HLN][0] = (u32)id << 8 | (u32)ch; HL[HLN][1] = arg; } HLN++; }
static u32 o_rd(void *c, u32 reg) { (void)c; RDN_O = reg; return RDV; }
static void o_dma(void *c, u32 a, u32 n) { (void)c; ODN += n == 16384 ? 1 : 100; ODA = a; }

static void gen(void)
{
	u32 i, k;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	for (i = 0; i < 4; i++) {
		OP[i] = lay(16);
		OR[i] = lay(32);
		OB[i] = lay(16);
		OST[i] = lay(400);
		for (k = 1; k < 4; k++)
			w32(OP[i] + 4 * (k - 1), fake());	/* P: words 0..2 */
		for (k = 0; k < 8; k++)
			w32(OR[i] + 4 * k, fake());
		for (k = 0; k < 3; k++)
			w32(OB[i] + 4 * k, fake());
		IMG[OST[i] + 385] = rb(3) ? (u8)rb(2) : (u8)rnd();
	}
	OBUF = lay(16);
	CH = rb(4);
	RDV = rb(2) ? rb(8) : rnd();
	KX = rb(3);
	for (i = 0; i < 4; i++) {
		RECF[i][0] = rb(3) ? (u16)rb(4) : (u16)rnd();
		RECF[i][1] = (u16)rnd();
		RECF[i][2] = (u16)rnd();
	}
	{ static const u32 e16[] = { 0, 199, 200, 201, 0xfffe, 0xffff }, e0c[] = { 0, 599, 600, 601, 0x7fffffff, 0x80000000u, 0xffffffffu };
	  E3516 = rb(2) ? e16[rb(6)] : rb(300); E350C = rb(2) ? e0c[rb(7)] : rb(700); }
	E3504 = rnd();
	for (i = 0; i < 4; i++) {
		SLOT[i][0] = rb(4) ? 1 : 0;	/* e35d0 / e35c0 / e35e0 / e35b0 slot present */
		SLOT[i][1] = rb(4) ? 1 : 0;
		SLOT[i][2] = rb(4) ? 1 : 0;
		SLOT[i][3] = rb(4) ? 1 : 0;
	}
}

static u32 NS[4][4], NSO[4][4];

static void run_vendor(int f)
{
	u8 *b = VA;
	u32 *rec = t32o_alloc(4 * 64), *dr = t32o_alloc(4 * 16), i;

	VBASE = b;
	for (i = 0; i < 4; i++) {
		u32 *r = &rec[i * 16];

		if (f == F_DEINIT) {
			t32_data_e35d0[i] = SLOT[i][0] ? T32O_PTR(b + OP[i]) : 0;
			t32_data_e35c0[i] = SLOT[i][1] ? 0x40000000u + i : 0;
			t32_data_e35e0[i] = SLOT[i][2] ? T32O_PTR(b + OR[i]) : 0;
			t32_data_e35b0[i] = SLOT[i][3] ? T32O_PTR(b + OB[i]) : 0;
		} else {
			r[1] = T32O_PTR(b + OST[i]);
			((u16 *)r)[22] = RECF[i][0];
			((u16 *)r)[23] = RECF[i][1];
			((u16 *)r)[25] = RECF[i][2];
			t32_data_e35e0[i] = T32O_PTR(r);
			dr[i * 4 + 1] = 0;
			t32_data_e35d0[i] = T32O_PTR(&dr[i * 4]);
		}
	}
	if (f == F_INT) {
		dr[CH * 4 + 1] = oBUFV;
		t32_data_e3516[0] = (u16)E3516;
		t32_data_e350c[0] = E350C;
		t32_data_e3504[0] = E3504;
	}
	t32o_rd_hook = rdhook;
	t32o_dma_n = 0;
	VLN = CLN = RDN_V = 0;
	if (f == F_DEINIT) {
		u64 r = t32o_call(FN(tisp_tmo_deinit), CH, 0, 0, 0, 0);

		if ((u32)r != 0) { printf("deinit returned %u\n", (u32)r); exit(1); }
	} else {
		u64 r = t32o_call(FN(tisp_tmo_interrupt_static), CH, 0, 0, 0, 0);

		if ((u32)r != 1) { printf("interrupt_static returned %u\n", (u32)r); exit(1); }
		VDN = t32o_dma_n + (t32o_dma_a[0] != 0 || t32o_dma_a[2] != 16384 || t32o_dma_a[3] != 0 ? 1000 : 0);
		VDA = t32o_dma_a[1];
	}
	for (i = 0; i < 4; i++) {
		NS[i][0] = norm(t32_data_e35d0[i], b);
		NS[i][1] = t32_data_e35c0[i];
		NS[i][2] = norm(t32_data_e35e0[i], b);
		NS[i][3] = norm(t32_data_e35b0[i], b);
		if (f == F_INT) {
			RECV[i][0] = ((u16 *)&rec[i * 16])[22];
			RECV[i][1] = ((u16 *)&rec[i * 16])[23];
			RECV[i][2] = ((u16 *)&rec[i * 16])[25];
		}
	}
	if (f == F_INT) {
		VE16 = t32_data_e3516[0];
		VE0C = t32_data_e350c[0];
		VE04 = t32_data_e3504[0];
	}
}

static u32 oE3516, oE350C, oE3504;
static u16 oREC[4][3];

static void run_ours(int f)
{
	u32 sl[4][4], i;

	OBASE = OA;
	OLN = HLN = ODN = RDN_O = 0;
	for (i = 0; i < 4; i++) {
		sl[i][0] = SLOT[i][0] ? (u32)(uintptr_t)(OA + OP[i]) : 0;
		sl[i][1] = SLOT[i][1] ? 0x40000000u + i : 0;
		sl[i][2] = SLOT[i][2] ? (u32)(uintptr_t)(OA + OR[i]) : 0;
		sl[i][3] = SLOT[i][3] ? (u32)(uintptr_t)(OA + OB[i]) : 0;
	}
	if (f == F_DEINIT) {
		t32_tmo_deinit(&sl[CH][0], &sl[CH][1], &sl[CH][2], &sl[CH][3], o_vfree, o_kfree, 0);
		for (i = 0; i < 4; i++) {
			NSO[i][0] = norm(sl[i][0], OA);
			NSO[i][1] = sl[i][1];
			NSO[i][2] = norm(sl[i][2], OA);
			NSO[i][3] = norm(sl[i][3], OA);
		}
		return;
	}
	oE3516 = E3516; oE350C = E350C; oE3504 = E3504;
	if (t32_tmo_interrupt_static((u16 *)&oE3516, &oE350C, o_rd, o_dma, oBUFV, OA + OST[CH], &oREC[CH][0], &oREC[CH][1], &oREC[CH][2], &oE3504, h_int, 0, (int)CH) != 1) {
		printf("ours returned != 1\n");
		exit(1);
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			u32 i;

			t32o_reset();
			VA = t32o_alloc(ARN);
			gen();
			memcpy(VA, IMG, TOT);
			memcpy(OA, IMG, TOT);
			oBUFV = 0x50000000u + OBUF * 16;
			memcpy(oREC, RECF, sizeof(oREC));
			run_vendor((int)f);
			run_ours((int)f);
			compared++;
			if (f == F_DEINIT) {
				if (VLN != OLN || memcmp(VL, OL, 8 * (OLN < 24 ? OLN : 24))) { printf("MISMATCH deinit case %u: frees %u / %u\n", c, VLN, OLN); return 1; }
				for (i = 0; i < 4; i++) {
					u32 j;

					for (j = 0; j < 4; j++)
						if (NS[i][j] != NSO[i][j]) { printf("MISMATCH deinit case %u: slot %u/%u %x vs %x\n", c, i, j, NS[i][j], NSO[i][j]); return 1; }
				}
			} else {
				if (CLN != HLN || memcmp(CL, HL, 8 * (HLN < 8 ? HLN : 8)) || RDN_V != 28952 || RDN_O != 28952 || VDN != 1 || ODN != 1 || VDA != ODA || ODA != oBUFV + ((RDV & 3u) << 14)) {
					printf("MISMATCH interrupt_static case %u: calls %u / %u rd %u %u dma %u %u addr %x %x\n", c, CLN, HLN, RDN_V, RDN_O, VDN, ODN, VDA, ODA);
					return 1;
				}
				if (VE16 != (oE3516 & 0xffffu) || VE0C != oE350C || VE04 != oE3504) { printf("MISMATCH interrupt_static case %u: counters %u %u %u vs %u %u %u\n", c, VE16, VE0C, VE04, oE3516 & 0xffffu, oE350C, oE3504); return 1; }
				for (i = 0; i < 4; i++) {
					u32 q;

					for (q = 0; q < 3; q++)
						if (RECV[i][q] != oREC[i][q]) { printf("MISMATCH interrupt_static case %u: rec %u field %u: %u vs %u\n", c, i, q, RECV[i][q], oREC[i][q]); return 1; }
				}
			}
			if (memcmp(VA, OA, TOT)) {
				for (i = 0; i < TOT && VA[i] == OA[i]; i++)
					;
				printf("MISMATCH %s case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", fname[f], c, i, CH, VA[i], OA[i]);
				return 1;
			}
		}
	printf("t32_tmo9c_oracle: %u cases compared (2 functions), 0 mismatches\n", compared);
	return 0;
}
