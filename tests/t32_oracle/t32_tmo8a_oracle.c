// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 8a: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_adr_faceae_refresh, tmo_fbf_interrupt_calc,
 * tmo_tmo_soft_process, tmo_adr_soft_process.  The callees (stat_calc, ev_func, tgain_func, algorithm, reg_write_every,
 * default_reg_refresh, stat_limit, ramY / ramV refreshes, tmo_soft_process of fbf_interrupt_calc) are recording stubs on the
 * vendor side and hooks on ours: the sequence of (callee, channel) is compared.  Canary framed blob for the face ae tables,
 * four channels (context, stat, record), the globals.  The adr_soft_process counter t32_data_e3516 (u16, only read) is random
 * (the dropped isp_printf is a stub in env.c).  Return values: fbf_interrupt_calc's 1, the rest
 * leftover registers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_tmo.h"

#define WEAK __attribute__((weak))
#define CAN 0xc5

WEAK u32 t32_data_e35e0[4];
WEAK u8 t32_data_e3500;
WEAK u8 t32_data_e3510[4];
WEAK u32 t32_data_e3504;
WEAK u32 t32_data_d8b34;
WEAK u16 t32_data_e3516;
WEAK char t32_data_ca418[8];
WEAK u8 t32_data_d03a0[4];
u8 t32_fblob[64 + 24 + 64 + 672 + 64];
__asm__(".weak t32_data_e34e8\n.set t32_data_e34e8, t32_fblob+64\n.weak t32_data_e3248\n.set t32_data_e3248, t32_fblob+152\n");

u64 tisp_adr_faceae_refresh(u32, u32, u32, u32);
u64 tisp_tmo_fbf_interrupt_calc(u32, u32, u32, u32);
u64 tisp_tmo_tmo_soft_process(u32, u32, u32, u32);
u64 tisp_tmo_adr_soft_process(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_FACE, F_FBF, F_TSP, F_ASP, F_N };
static const char *const fname[F_N] = { "adr_faceae_refresh", "fbf_interrupt_calc", "tmo_soft_process", "adr_soft_process" };

static u32 VL[16], VN, OL[16], ON;
static u32 CH, FLAG;
u64 tisp_tmo_adr_stat_calc(u32 ch) { if (VN < 16) VL[VN++] = 0x000 | ch; return 0; }
u64 tisp_tmo_adr_ev_func(u32 ch) { if (VN < 16) VL[VN++] = 0x100 | ch; return 0; }
u64 tisp_tmo_adr_tgain_func(u32 ch) { if (VN < 16) VL[VN++] = 0x200 | ch; return 0; }
u64 tisp_tmo_adr_algorithm(u32 ch) { if (VN < 16) VL[VN++] = 0x300 | ch; return 0; }
u64 tisp_tmo_adr_reg_write_every(u32 ch) { if (VN < 16) VL[VN++] = 0x400 | ch; return 0; }
u64 tisp_tmo_tmo_default_reg_refresh(u32 ch) { if (VN < 16) VL[VN++] = 0x000 | ch; return 0; }
u64 tisp_tmo_tmo_stat_limit(u32 ch) { if (VN < 16) VL[VN++] = 0x100 | ch; return 0; }
u64 tisp_tmo_ramY_vi1_reg_refresh(u32 ch) { if (VN < 16) VL[VN++] = 0x200 | ch; return 0; }
u64 tisp_tmo_ramV_vi1_reg_refresh(u32 ch) { if (VN < 16) VL[VN++] = 0x300 | ch; return 0; }
u64 tisp_tmo_ramY_vi0_reg_refresh(u32 ch) { if (VN < 16) VL[VN++] = 0x400 | ch; return 0; }
u64 tisp_tmo_ramV_vi0_reg_refresh(u32 ch) { if (VN < 16) VL[VN++] = 0x500 | ch; return 0; }
static void hk(void *c, int id, int ch);
static void hk_soft(void *c, u32 ch);

static void hk(void *c, int id, int ch) { (void)c; if (ON < 16) OL[ON++] = (u32)id << 8 | (u32)ch; }

static u32 rs = 0x4be7d1u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

struct S {
	u8 e3500, e3510[4], d03a0;
	u32 e3504, d8b34;
	u16 e3516;
	u8 fblob[sizeof(t32_fblob)];
	u8 ctx[4][2600], st[4][400], rec[4][64];
} SV, SO;
static u8 FEN[24], FTAB[672];

static void hk_soft(void *c, u32 ch)
{
	(void)c;
	t32_tmo_tmo_soft_process(&SO.e3510[ch], SO.ctx[ch], SO.st[ch], SO.d03a0, hk, 0, (int)ch);
}

static void gen(void)
{
	u32 i, k;

	memset(&SV, 0, sizeof(SV));
	memset(SV.fblob, CAN, sizeof(SV.fblob));
	for (i = 0; i < 24; i++) SV.fblob[64 + i] = (u8)rnd();
	for (i = 0; i < 672; i++) SV.fblob[152 + i] = (u8)rnd();
	for (i = 0; i < 4; i++) {
		for (k = 0; k < 2600; k++) SV.ctx[i][k] = (u8)rnd();
		for (k = 0; k < 400; k++) SV.st[i][k] = (u8)rnd();
		for (k = 40; k < 64; k++) SV.rec[i][k] = (u8)rnd();
		SV.st[i][385] = rb(2) ? 1 : (rb(2) ? 0 : (u8)rnd());
		SV.e3510[i] = rb(2) ? 0 : (rb(2) ? 1 : (u8)rnd());
		if (rb(3) == 0) *(u16 *)&SV.rec[i][46] = rb(2) ? 0 : (u16)(1 + rb(2));
		if (rb(5) == 0) *(u16 *)&SV.rec[i][46] = 0xffff;
	}
	SV.e3500 = (u8)rnd();
	SV.d03a0 = rb(3) ? (u8)rb(2) : (u8)rnd();
	SV.e3504 = rb(4);
	SV.d8b34 = rnd();
	SV.e3516 = rb(3) == 0 ? (u16)(1000 * rb(60)) : (u16)rnd();
	CH = rb(4);
	FLAG = rnd();
	for (i = 0; i < 24; i++) FEN[i] = (u8)rnd();
	for (i = 0; i < 672; i++) FTAB[i] = (u8)rnd();
	SO = SV;
}

static void run_vendor(int f)
{
	u32 *recp = t32o_alloc(4 * 64), i;

	for (i = 0; i < 4; i++) {
		recp[i * 16] = T32O_PTR(SV.ctx[i]);
		recp[i * 16 + 1] = T32O_PTR(SV.st[i]);
		memcpy((u8 *)&recp[i * 16] + 40, SV.rec[i] + 40, 24);
		t32_data_e35e0[i] = T32O_PTR(&recp[i * 16]);
	}
	t32_data_e3500 = SV.e3500;
	memcpy(t32_data_e3510, SV.e3510, 4);
	t32_data_e3504 = SV.e3504;
	t32_data_d8b34 = SV.d8b34;
	t32_data_e3516 = SV.e3516;
	t32_data_d03a0[0] = SV.d03a0;
	memcpy(t32_fblob, SV.fblob, sizeof(t32_fblob));
	VN = 0;
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
	switch (f) {
	case F_FACE: t32o_call(FN(tisp_adr_faceae_refresh), 0, FLAG, T32O_PTR(FEN), T32O_PTR(FTAB), 0); break;
	case F_FBF: {
		u64 r = t32o_call(FN(tisp_tmo_fbf_interrupt_calc), CH, 0, 0, 0, 0);

		if ((u32)r != 1) { printf("MISMATCH fbf return %u\n", (u32)r); exit(1); }
		break;
	}
	case F_TSP: t32o_call(FN(tisp_tmo_tmo_soft_process), CH, 0, 0, 0, 0); break;
	default: t32o_call(FN(tisp_tmo_adr_soft_process), CH, 0, 0, 0, 0); break;
	}
	SV.e3500 = t32_data_e3500;
	memcpy(SV.e3510, t32_data_e3510, 4);
	SV.d8b34 = t32_data_d8b34;
	memcpy(SV.fblob, t32_fblob, sizeof(t32_fblob));
	for (i = 0; i < 4; i++)
		memcpy(SV.rec[i] + 40, (u8 *)&recp[i * 16] + 40, 24);
}

static void run_ours(int f)
{
	ON = 0;
	switch (f) {
	case F_FACE: t32_adr_faceae_refresh(&SO.e3500, FLAG, SO.fblob + 64, FEN, (s16 *)(SO.fblob + 152), (const s16 *)FTAB); break;
	case F_FBF: {
		int r = t32_tmo_fbf_interrupt_calc(SO.st[CH], SO.rec[SO.e3504], &SO.d8b34, hk_soft, 0, SO.e3504);

		if (r != 1) { printf("ours fbf return %d\n", r); exit(1); }
		break;
	}
	case F_TSP: t32_tmo_tmo_soft_process(&SO.e3510[CH], SO.ctx[CH], SO.st[CH], SO.d03a0, hk, 0, (int)CH); break;
	default: t32_tmo_adr_soft_process(hk, 0, (int)CH); break;
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int f, c, compared = 0;

	for (f = 0; f < F_N; f++)
		for (c = 0; c < n; c++) {
			gen();
			t32o_reset();
			run_vendor((int)f);
			run_ours((int)f);
			compared++;
			if (VN != ON || memcmp(VL, OL, 4 * ON)) {
				printf("MISMATCH %s case %u: callee sequence vendor n %u ours n %u (%x / %x)\n", fname[f], c, VN, ON, VL[0], OL[0]);
				return 1;
			}
			if (memcmp(&SV, &SO, sizeof(SV))) {
				u8 *x = (u8 *)&SV, *y = (u8 *)&SO;
				u32 i;

				for (i = 0; i < sizeof(SV) && x[i] == y[i]; i++)
					;
				printf("MISMATCH %s case %u: state byte %u vendor %02x ours %02x (ch %u st385 %u cur %u)\n", fname[f], c, i, x[i], y[i], CH, SV.st[CH][385], SV.e3504);
				return 1;
			}
		}
	printf("t32_tmo8a_oracle: %u cases compared (4 functions), 0 mismatches\n", compared);
	return 0;
}
