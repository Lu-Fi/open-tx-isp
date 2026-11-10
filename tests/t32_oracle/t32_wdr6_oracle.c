// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, WDR phase 6: our readable port (driver/t32/tx_isp_t32_wdr.h)
 * against the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc): tisp_wdr_softout_reg_refresh, hardware_reg_refresh,
 * dn_params_refresh, wake_up (with params_hard_refresh, default / direct
 * register refresh, log2 as vendor callees).
 *
 * Both sides get the same random arena (parameter block, extension, hardware
 * image of all 4 channels, output) made of canary framed regions; the whole
 * arena, the channel records (28 bytes, words compared), the register write
 * log (address, value, order), the saved DMA words (e3a84) and the return
 * value (functions that return one) are compared.  Register reads return a
 * per case random function of the address.  The fusion switch (par + 1831)
 * is 1 in half of the cases, the WDR switch of wake_up (f0918) in half;
 * the soft output level (d8b40) is compared.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_wdr.h"

#define WEAK __attribute__((weak))
#define FR	64u
#define CAN	0xc5
#define ARN	(1u << 15)

WEAK u32 t32_data_e3b40[4];
WEAK u32 t32_data_e3a84[5];
WEAK u8 t32_data_e3a98[4];
WEAK u32 t32_data_e3ac0[4];
WEAK u32 t32_data_d8b40[1], t32_data_f0918[4];
/* .rodata default curve (data.c:6037..6057) */
WEAK const u32 t32_data_c2dc8[14] = { 0x001f0000U, 0x005f003fU, 0x00bf007fU, 0x017f00ffU, 0x027f01ffU, 0x037f02ffU,
	0x047f03ffU, 0x057f04ffU, 0x067f05ffU, 0x077f06ffU, 0x087f07ffU, 0x097f08ffU, 0x0a7f09ffU, 0x00000affU };
WEAK const u32 t32_data_c2e00[7] = { 0x05050505U, 0x07070606U, 0x07070707U, 0x07070707U, 0x07070707U, 0x07070707U,
	0x00000707U };

u64 tisp_wdr_softout_reg_refresh(u32, u32, u32, u32);
u64 tisp_wdr_hardware_reg_refresh(u32, u32, u32, u32);
u64 tisp_wdr_dn_params_refresh(u32, u32, u32, u32);
u64 tisp_wdr_wake_up(u32, u32, u32, u32);

#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_SOFT, F_HW, F_DN, F_WAKE, F_N };
static const char *const fname[F_N] = { "softout_reg_refresh", "hardware_reg_refresh", "dn_params_refresh", "wake_up" };
static const int has_ret[F_N] = { 1, 1, 0, 0 };

static u32 rs = 0x510e52u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rb(u32 n) { return rnd() % n; }

static u32 OFF[4], PAR[4], EXT[4], HW[4], TOT, CH, RDK, KNULL, MODE;
static u8 IMG[ARN], OA[ARN] __attribute__((aligned(16)));
static u8 *VA;
static u32 cur;

static void lay_begin(void) { cur = 0; memset(IMG, CAN, sizeof(IMG)); }
static u32 lay(u32 n)
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

static u32 rd(u32 reg) { return (reg * 2654435761u) ^ RDK; }

/* private_kmalloc: the vendor's buffer, kept for our hook; NULL when KNULL */
static u8 *kbuf;
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	kbuf = KNULL ? 0 : t32o_alloc(a0);
	return T32O_PTR(kbuf);
}

/* our side: write log, hooks */
static struct { u32 reg, val; } W[1024];
static u32 NW;
static void o_wr(u32 reg, u32 val) { if (NW < 1024) { W[NW].reg = reg; W[NW].val = val; } NW++; }
static u8 *o_kmalloc(u32 size) { (void)size; return kbuf; }
static u32 o_v2p(const u8 *p) { return T32O_PTR(p) + 0x80000000u; }
static const struct t32_wdr_ops OPS = { o_wr, rd, o_kmalloc, o_v2p, 0, 0, 0, 0 };

static struct t32_wdr_st ST;
static struct t32_wdr_rec REC[4];
static u32 *VREC[4];

static void gen(int f)
{
	u32 i;

	(void)f;
	lay_begin();
	for (i = 0; i < 4; i++) {
		PAR[i] = lay(T32_WDR_PAR_SZ + 8);
		EXT[i] = lay(T32_WDR_EXT_SZ);
		HW[i] = lay(1036);
	}
	OFF[0] = lay(T32_WDR_PAR_SZ + T32_WDR_EXT_SZ + 8);
	OFF[1] = lay(4);
	CH = rb(4);
	for (i = 0; i < 4; i++) {
		u8 *p = IMG + PAR[i], *h = IMG + HW[i];
		u32 k;

		if (rb(2)) p[1831] = 1;
		else if (rb(2)) p[1831] = (u8)rb(3);
		if (rb(3) == 0) for (k = 0; k < 5; k++) { u16 v = (u16)(rb(2) ? rb(4096) : rnd()); memcpy(h + 754 + 2 * k, &v, 2); }
		if (rb(4) == 0) for (k = 0; k < 5; k++) { u16 v = (u16)(k * 300 + rb(50)); memcpy(h + 754 + 2 * k, &v, 2); }
		if (rb(3)) for (k = 0; k < 5; k++) { u16 v = (u16)(rb(4) ? rb(256) : rb(65536)); memcpy(p + 1516 + 2 * k, &v, 2); }
		if (rb(5) == 0) for (k = 0; k < 5; k++) { u16 v = (u16)rb(3); memcpy(p + 1516 + 2 * k, &v, 2); }
		if (rb(3)) { u16 v = (u16)rb(rb(3) ? 257 : 65536); memcpy(p + 288, &v, 2); }
		if (rb(3)) for (k = 0; k < 27; k++) { u16 v = (u16)(rb(6) == 0 ? 0 : rb(3) ? rb(300) : rnd()); memcpy(h + 700 + 2 * k, &v, 2); }
		(void)f;
	}
	MODE = rb(3) == 0 ? rnd() : rb(4);
	RDK = rnd();
	KNULL = rb(8) == 0;
}

static void setup(void)
{
	u32 i, k;

	memset(&ST, 0, sizeof(ST));
	ST.ops = &OPS;
	for (i = 0; i < 4; i++) {
		VREC[i] = t32o_alloc(32);
		for (k = 0; k < 7; k++)
			VREC[i][k] = rnd();
		VREC[i][3] = T32O_PTR(VA + PAR[i]);
		VREC[i][4] = T32O_PTR(VA + EXT[i]);
		VREC[i][5] = T32O_PTR(VA + HW[i]);
		t32_data_e3b40[i] = T32O_PTR(VREC[i]);
		REC[i].nbuf = VREC[i][0];
		REC[i].buf = T32O_HOST(VREC[i][1]);
		REC[i].buf_phys = VREC[i][2];
		REC[i].par = OA + PAR[i];
		REC[i].ext = OA + EXT[i];
		REC[i].hw = OA + HW[i];
		REC[i].work = T32O_HOST(VREC[i][6]);
		ST.rec[i] = &REC[i];
	}
	for (i = 0; i < 5; i++)
		t32_data_e3a84[i] = ST.dma_save[i] = rnd();
	t32_data_e3a98[0] = ST.fusion_on = (u8)rnd();
	t32_data_d8b40[0] = ST.soft_y = rnd();
	for (i = 0; i < 4; i++)
		t32_data_f0918[i] = ST.wdr_en[i] = rb(2) ? 0 : rb(3) ? 1 : rnd();
	for (i = 0; i < 2; i++) {
		u64 e = rb(3) == 0 ? ((u64)rnd() << 32 | rnd()) : rb(5000);
		ST.long_ev[i] = e;
		t32_data_e3ac0[2 * i] = (u32)e;
		t32_data_e3ac0[2 * i + 1] = (u32)(e >> 32);
	}
	NW = 0;
	t32o_rd_hook = rd;
}

static int rec_differ(void)
{
	u32 i;

	for (i = 0; i < 4; i++) {
		const u32 *v = VREC[i];
		const struct t32_wdr_rec *r = &REC[i];

		if (v[0] != r->nbuf || v[1] != T32O_PTR(r->buf) || v[2] != r->buf_phys ||
		    v[3] != T32O_PTR(VA + (r->par - OA)) || v[4] != T32O_PTR(VA + (r->ext - OA)) ||
		    v[5] != T32O_PTR(VA + (r->hw - OA)) || v[6] != T32O_PTR(r->work))
			return 1;
	}
	return memcmp(t32_data_e3a84, ST.dma_save, sizeof(ST.dma_save)) != 0 || t32_data_e3a98[0] != ST.fusion_on ||
	       t32_data_d8b40[0] != ST.soft_y;
}

static u64 run_vendor(int f)
{
	u32 P0 = T32O_PTR(VA + OFF[0]), P1 = T32O_PTR(VA + OFF[1]);

	t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
	switch (f) {
	case F_SOFT: return t32o_call(FN(tisp_wdr_softout_reg_refresh), CH, 0, 0, 0, 0);
	case F_HW: return t32o_call(FN(tisp_wdr_hardware_reg_refresh), CH, 0, 0, 0, 0);
	case F_DN: return t32o_call(FN(tisp_wdr_dn_params_refresh), CH, 0, 0, 0, 0);
	case F_WAKE: return t32o_call(FN(tisp_wdr_wake_up), CH, 0, 0, 0, 0);
	}
	(void)P0;
	(void)P1;
	return 0;
}

static u64 run_ours(int f)
{
	switch (f) {
	case F_SOFT: return (u32)t32_wdr_softout_reg_refresh(&ST, CH);
	case F_HW: return (u32)t32_wdr_hardware_reg_refresh(&ST, CH);
	case F_DN: t32_wdr_dn_params_refresh(&ST, CH); return 0;
	case F_WAKE: t32_wdr_wake_up(&ST, CH); return 0;
	}
	return 0;
}

int main(void)
{
	static u32 hit[F_N];
	u32 n, cases = 0, bad = 0, i;
	int f;

	for (n = 0; n < 16000; n++) {
		u64 rv, ro;
		int d;

		f = (int)(n % F_N);
		gen(f);
		t32o_reset();
		VA = t32o_alloc(ARN);
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		setup();
		rv = run_vendor(f);
		ro = run_ours(f);
		cases++;
		hit[f]++;
		d = (has_ret[f] && (u32)rv != (u32)ro) || memcmp(VA, OA, TOT) || rec_differ() || NW != t32o_log.n;
		for (i = 0; !d && i < NW; i++)
			d = W[i].reg != t32o_log.w[i].reg || W[i].val != t32o_log.w[i].val;
		if (d && bad++ < 5)
			fprintf(stderr, "t32_wdr6_oracle: %s case %u differs (ret %08x / %08x, writes %u / %u)\n", fname[f], n,
				(u32)rv, (u32)ro, t32o_log.n, NW);
	}
	t32o_rd_hook = 0;
	for (f = 0; f < F_N; f++)
		if (!hit[f])
			bad++;
	printf("t32_wdr6_oracle: %u cases, %u mismatches\n", cases, bad);
	return bad ? 1 : 0;
}
