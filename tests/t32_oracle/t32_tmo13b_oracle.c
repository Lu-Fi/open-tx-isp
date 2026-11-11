// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, TMO phase 13b: our readable port (driver/t32/tx_isp_t32_tmo.h) against the unmodified vendor function
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc): tisp_tmo_init (with the vendor ref of tmo_params_hard_refresh on both sides' state: ours calls the port).
 * Pointer heavy (records hold 32 bit addresses): the vendor runs on the arena first, its result is saved, the arena is restored and our port runs on
 * the SAME addresses, then the whole arena, the slots, e3580 / e3570 / e3518, and the call logs are compared.  private_vmalloc / private_kmalloc return
 * fixed arena regions by size (log: kind, size, flags compared), the callees that are cross-checked elsewhere (malloc_cfg, size_update (ch and parameter
 * block address), fbf_malloc_cfg, dark_ctr_interp, detail_ev_interp, satcurve_ev_interp, tmoadr_default_reg_refresh, lcac_no_refresh, tmo_default_reg_refresh,
 * modechange_stat_refresh, ramY / ramV vi0 / vi1, adr_gamma_refresh, system_irq_func_set) are recording stubs on the vendor side and hooks on ours, in call
 * order.  tmoadr_default_reg_refresh also scripts st byte 385 (both sides) so that the stat-byte branches of the vendor are reached;
 * tisp_reg_write is the oracle's oa_reg_write (shadow tagged).  Not covered: the vendor's "s7 != 1" branch (ctx byte 10042 was just set to 1: dead) and
 * isp_printf (allocation failures: a NULL vmalloc result is a NULL dereference in the vendor).
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
#define CTXN	11200u

WEAK u32 t32_data_e35e0[4];
WEAK u32 t32_data_e35b0[4];
WEAK u32 t32_data_e3580[2];
WEAK u32 t32_data_e3570[2];
WEAK u16 t32_data_e3518[4];
WEAK u8 t32_data_d03a0[4];
WEAK u8 t32_data_d88d6[32];
WEAK u8 t32_data_c0518[64];
WEAK u8 t32_data_ca38c[64];
u64 tisp_tmo_init(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))

static u32 rs = 0x2d17e3u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static u32 OCTX, OP, OD, OVM[5], OKM[5], TOT, cur, CH, S385;
static u8 D03, IMG[ARN];
static u8 *VA, *SAVE;
static const u32 VSZ[5] = { 64, 1176, 0x8530, 402, 20 }, KSZ[5] = { 15040, 20, 800, 1140, 10056 };

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

/* logs */
static u32 VL[64][3], VLN, OL[64][3], OLN, (*LOG)[3] = VL, *LN = &VLN;
static u32 REGW[4][2], REGN;
static void lg(u32 a, u32 b, u32 c) { if (*LN < 64) { LOG[*LN][0] = a; LOG[*LN][1] = b; LOG[*LN][2] = c; } (*LN)++; }
static u32 pick(const u32 *sz, u32 *regs, u32 n)
{
	u32 i;

	for (i = 0; i < 5; i++)
		if (sz[i] == n)
			return regs[i];
	return 0;
}
u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; lg(1, a0, 0); return T32O_PTR(VA + pick(VSZ, OVM, a0)); }
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; lg(2, a0, a1); return T32O_PTR(VA + pick(KSZ, OKM, a0)); }
static void scr(u32 ch) { if (ch < 4) { u32 *rec = (u32 *)(uintptr_t)t32_data_e35e0[ch]; ((u8 *)(uintptr_t)rec[1])[385] = (u8)S385; } }
/* ours: the channel record lives in the slot of ours */
static u32 *CURSLOT;
static void scr_o(u32 ch) { if (ch < 4) { u32 *rec = (u32 *)(uintptr_t)*CURSLOT; ((u8 *)(uintptr_t)rec[1])[385] = (u8)S385; } }
u64 tisp_tmo_malloc_cfg(u32 ch) { lg(10, ch, 0); return 0; }
u64 tisp_tmo_tmo_size_update(u32 ch, u32 p) { lg(11, ch, p); return 0; }
u64 tisp_tmo_fbf_malloc_cfg(u32 ch) { lg(12, ch, 0); return 0; }
u64 tisp_tmo_tmo_dark_ctr_interp(u32 ch) { lg(13, ch, 0); return 0; }
u64 tisp_tmo_tmo_detail_ev_interp(u32 ch) { lg(14, ch, 0); return 0; }
u64 tisp_tmo_tmo_satcurve_ev_interp(u32 ch) { lg(15, ch, 0); return 0; }
u64 tisp_tmo_tmoadr_default_reg_refresh(u32 ch) { lg(16, ch, 0); scr(ch); return 0; }
u64 tisp_lcac_no_refresh(u32 ch) { lg(17, ch, 0); return 0; }
u64 tisp_tmo_tmo_default_reg_refresh(u32 ch) { lg(18, ch, 0); return 0; }
u64 tisp_tmo_modechange_stat_refresh(u32 ch) { lg(19, ch, 0); return 0; }
u64 tisp_tmo_ramY_vi1_reg_refresh(u32 ch) { lg(20, ch, 0); return 0; }
u64 tisp_tmo_ramV_vi1_reg_refresh(u32 ch) { lg(21, ch, 0); return 0; }
u64 tisp_tmo_ramY_vi0_reg_refresh(u32 ch) { lg(22, ch, 0); return 0; }
u64 tisp_tmo_ramV_vi0_reg_refresh(u32 ch) { lg(23, ch, 0); return 0; }
u64 tisp_tmo_adr_gamma_refresh(u32 ch) { lg(24, ch, 0); return 0; }
u64 tisp_tmo_interrupt_static(u32 ch) { (void)ch; return 0; }
u64 tisp_tmo_fbf_interrupt_calc(u32 ch) { (void)ch; return 0; }
u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	lg(25, a0 | a1 << 8, a2 == (u32)(uintptr_t)tisp_tmo_interrupt_static ? 1 : a2 == (u32)(uintptr_t)tisp_tmo_fbf_interrupt_calc ? 2 : 99);
	return 0;
}

static void h_init(void *c, int id, int ch)
{
	static const u8 map[17] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 25 };

	(void)c;
	if (id == 1) { lg(11, (u32)ch, T32O_PTR(VA + OP)); return; }
	if (id == 15) { lg(25, (u32)ch | 9u << 8, 1); return; }
	if (id == 16) { lg(25, (u32)ch | 31u << 8, 2); return; }
	lg(map[id], (u32)ch, 0);
	if (id == 6)
		scr_o((u32)ch);
}
static void *o_vm(void *c, u32 n) { (void)c; lg(1, n, 0); return VA + pick(VSZ, OVM, n); }
static void *o_km(void *c, u32 n, u32 fl) { (void)c; lg(2, n, fl); return VA + pick(KSZ, OKM, n); }
static void o_regw(void *c, int ch, u32 reg, u32 val)
{
	(void)c;
	if (REGN < 4) { REGW[REGN][0] = D03 == 1 ? 0x80000000u | ((u32)ch & 7) << 24 | reg : reg; REGW[REGN][1] = val; }
	REGN++;
}

static void gen(void)
{
	u32 i;

	memset(IMG, CAN, sizeof(IMG));
	cur = 0;
	OCTX = lay(CTXN);
	OP = lay(64);
	OD = lay(32);
	for (i = 0; i < 5; i++) {
		OVM[i] = lay(VSZ[i] + 8);
		OKM[i] = lay(KSZ[i] + 8);
	}
	CH = rb(4);
	S385 = rb(3) == 0 ? 1 : rb(2) ? rb(3) : rnd() & 0xff;
	D03 = rb(3) ? (u8)rb(2) : (u8)rnd();
	IMG[OCTX + 10042] = (u8)rnd();
}

static u64 run_vendor(void)
{
	u32 i;
	u64 r;

	for (i = 0; i < 4; i++) {
		t32_data_e35e0[i] = 0x11110000u + i;
		t32_data_e35b0[i] = 0x22220000u + i;
		t32_data_dd0a4[i] = i == CH ? T32O_PTR(VA + OCTX) - 11976u : 0x33330000u;
	}
	memcpy(t32_data_d88d6, VA + OD, 28);
	t32_data_d03a0[0] = D03;
	t32_data_e3518[0] = 0x7777;
	t32_data_e3580[0] = t32_data_e3570[0] = 0;
	t32o_log.n = 0;
	t32o_log.printfs = 0;
	VLN = 0;
	LOG = VL; LN = &VLN;
	r = t32o_call(FN(tisp_tmo_init), CH, T32O_PTR(VA + OP), 0, 0, 0);
	return r;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 1500;
	unsigned int c, compared = 0;

	SAVE = malloc(ARN);
	for (c = 0; c < n; c++) {
		u32 i, vs35e0, vs35b0, v3580, v3570;
		u16 v3518;
		u32 s35e0, s35b0, o3580 = 0, o3570 = 0, rsave;
		u16 o3518 = 0x7777;
		struct t32_tmo_init_env e;

		t32o_reset();
		VA = t32o_alloc(ARN);
		gen();
		memcpy(VA, IMG, TOT);
		s35e0 = 0x11110000u + CH;
		s35b0 = 0x22220000u + CH;
		run_vendor();
		vs35e0 = t32_data_e35e0[CH]; vs35b0 = t32_data_e35b0[CH];
		v3580 = t32_data_e3580[0]; v3570 = t32_data_e3570[0]; v3518 = t32_data_e3518[0];
		rsave = t32o_log.printfs;
		if (rsave) { printf("MISMATCH case %u: isp_printf %u\n", c, rsave); return 1; }
		for (i = 0; i < 4; i++)
			if (i != CH && (t32_data_e35e0[i] != 0x11110000u + i || t32_data_e35b0[i] != 0x22220000u + i)) { printf("MISMATCH decoy slot\n"); return 1; }
		memcpy(SAVE, VA, TOT);
		memcpy(VA, IMG, TOT);
		OLN = 0; LOG = OL; LN = &OLN;
		REGN = 0;
		memset(&e, 0, sizeof(e));
		e.vmalloc = o_vm; e.kmalloc = o_km; e.hook = h_init; e.regw = o_regw; e.cb = 0;
		e.e35e0 = &s35e0; e.e35b0 = &s35b0; CURSLOT = &s35e0;
		e.ctx = T32O_PTR(VA + OCTX);
		e.e3580 = &o3580; e.e3570 = &o3570; e.e3518 = &o3518;
		e.d88d6 = VA + OD;
		e.param = (const u32 *)(VA + OP);
		e.d03a0 = D03;
		t32_tmo_init(&e, (int)CH);
		compared++;
		if (VLN != OLN || memcmp(VL, OL, 12 * (OLN < 64 ? OLN : 64))) {
			u32 q;

			for (q = 0; q < VLN && q < OLN && !memcmp(VL[q], OL[q], 12); q++)
				;
			{ u32 z; for (z = 0; z < VLN && z < 30; z++) printf("%u:%u/%u/%u ", z, VL[z][0], VL[z][1], VL[z][2]); printf("\n"); for (z = 0; z < OLN && z < 30; z++) printf("%u:%u/%u/%u ", z, OL[z][0], OL[z][1], OL[z][2]); printf("\n"); }
			printf("MISMATCH case %u: call logs %u / %u first diff %u: %x %x %x vs %x %x %x\n", c, VLN, OLN, q, VL[q][0], VL[q][1], VL[q][2], OL[q][0], OL[q][1], OL[q][2]);
			return 1;
		}
		if (s35e0 != vs35e0 || s35b0 != vs35b0 || o3580 != v3580 || o3570 != v3570 || o3518 != v3518) {
			printf("MISMATCH case %u: slots %x %x / %x %x, e3580 %x %x, e3570 %x %x, e3518 %x %x\n", c, vs35e0, vs35b0, s35e0, s35b0, v3580, o3580, v3570, o3570, v3518, o3518);
			return 1;
		}
		if (REGN != 1 || REGW[0][1] != 1 || t32o_log.n != 1 || t32o_log.w[0].reg != REGW[0][0] || t32o_log.w[0].val != 1) {
			printf("MISMATCH case %u: register write %u (%x) vs %u\n", c, t32o_log.n, t32o_log.w[0].reg, REGN);
			return 1;
		}
		if (memcmp(SAVE, VA, TOT)) {
			for (i = 0; i < TOT && SAVE[i] == VA[i]; i++)
				;
			printf("MISMATCH case %u: arena byte %u (ch %u) vendor %02x ours %02x\n", c, i, CH, SAVE[i], VA[i]);
			return 1;
		}
	}
	printf("t32_tmo13b_oracle: %u cases compared (tmo_init), 0 mismatches\n", compared);
	return 0;
}
