// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, WDR phase 7: our readable port (driver/t32/tx_isp_t32_wdr.h)
 * against the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc): tisp_wdr_get_data, interrupt_static, process (with the vendor's
 * params_hard_refresh, softout_reg_refresh and log2 as callees).
 *
 * One random arena on both sides holds the 4 channels' parameter block,
 * extension, hardware image and work area, the 8 process and 6 LUT buffers
 * and the 4 statistics buffers; it is compared byte for byte after the call,
 * with the records, the WDR globals and every register write.  The
 * Tiziano_wdr_deghost_fpga / fusion_fpga calls (not part of this block) are
 * stubs on the vendor side and hooks on ours that log the channel and change
 * the parameter block (+494 .. +517) / the extension (+16 .. +95) the same
 * way, so the order of reads around them is checked.  The event (13, channel)
 * and our cache sync arguments are checked (the vendor's env stub does not
 * record the sync; ours is compared with the buffer the vendor reads).
 * Exit 0 = all equal.
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
#define ARN	(1u << 17)

WEAK u32 t32_data_e3b40[4];
WEAK u8 t32_data_e3a98[4], t32_data_e3a99[4], t32_data_e3a9a[4], t32_data_e3a80[4], t32_data_e3ad0[4];
WEAK u32 t32_data_e3aa0[1], t32_data_e3aa4[1], t32_data_e3ab0[4], t32_data_e3ac0[4], t32_data_e3b0c[1];
WEAK u32 t32_data_d8b40[1];
WEAK u32 t32_data_e3ad4[1], t32_data_e3ad8[1], t32_data_e3adc[1], t32_data_e3ae0[1], t32_data_e3ae4[1],
	t32_data_e3ae8[1], t32_data_e3aec[1], t32_data_e3af0[1];
WEAK u32 t32_data_e3af4[1], t32_data_e3af8[1], t32_data_e3afc[1], t32_data_e3b00[1], t32_data_e3b04[1], t32_data_e3b08[1];
static u32 *const VPROC[8] = { t32_data_e3ad4, t32_data_e3ad8, t32_data_e3adc, t32_data_e3ae0, t32_data_e3ae4,
	t32_data_e3ae8, t32_data_e3aec, t32_data_e3af0 };
static u32 *const VLUT[6] = { t32_data_e3af4, t32_data_e3af8, t32_data_e3afc, t32_data_e3b00, t32_data_e3b04,
	t32_data_e3b08 };

u64 tisp_wdr_get_data(u32, u32, u32, u32);
u64 tisp_wdr_interrupt_static(u32, u32, u32, u32);
u64 tisp_wdr_process(u32, u32, u32, u32);

#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_GET, F_IRQ, F_PRO, F_N };
static const char *const fname[F_N] = { "get_data", "interrupt_static", "process" };

static u32 rs = 0x9b05688u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rb(u32 n) { return rnd() % n; }

static u32 PAR[4], EXT[4], HW[4], WRK[4], PRO[8], LUT[6], BUF, BUF2, SRC, TOT, CH, RDK, cur;
static u8 IMG[ARN], OA[ARN] __attribute__((aligned(16)));
static u8 *VA;

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

/* the deghost / fusion stand-ins: a fixed change of the parameter block / extension, logged */
static u32 VLOG[4], OLOG[4], NVL, NOL;
static void scribble(u8 *base, u32 ch, int fusion)
{
	u8 *p = base + (fusion ? EXT[ch] + 16 : PAR[ch] + 494);
	u32 k, n = fusion ? 80 : 24;

	for (k = 0; k < n; k++)
		p[k] ^= (u8)(k * 7 + ch + (fusion ? 0x31 : 0x5c));
}
u64 Tiziano_wdr_deghost_fpga(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (NVL < 4) VLOG[NVL] = 0x100 | a0;
	NVL++;
	scribble(VA, a0, 0);
	return 0;
}
u64 Tiziano_wdr_fusion_fpga(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (NVL < 4) VLOG[NVL] = 0x200 | a0;
	NVL++;
	scribble(VA, a0, 1);
	return 0;
}

static struct { u32 reg, val; } W[1024];
static u32 NW, EVN, EVCH, EVW[8], SYNC_N;
static const u8 *SYNC_P;
static u32 rd(u32 reg) { return reg == T32_WDR_REG_DMA_CTL ? (RDK & 3) : (reg * 2654435761u) ^ RDK; }
static void o_wr(u32 reg, u32 val) { if (NW < 1024) { W[NW].reg = reg; W[NW].val = val; } NW++; }
static void o_sync(const u8 *p, u32 size) { SYNC_P = p; SYNC_N = size; }
static void o_push(u32 ch, const u32 *ev) { EVN++; EVCH = ch; memcpy(EVW, ev, sizeof(EVW)); }
static void o_deghost(u32 ch) { if (NOL < 4) OLOG[NOL] = 0x100 | ch; NOL++; scribble(OA, ch, 0); }
static void o_fusion(u32 ch) { if (NOL < 4) OLOG[NOL] = 0x200 | ch; NOL++; scribble(OA, ch, 1); }
static const struct t32_wdr_ops OPS = { o_wr, rd, 0, 0, 0, 0, 0, 0, o_sync, o_push, o_deghost, o_fusion };

static struct t32_wdr_st ST;
static struct t32_wdr_rec REC[4];
static u32 *VREC[4];

static void gen(int f)
{
	u32 i;

	lay_begin();
	for (i = 0; i < 4; i++) {
		PAR[i] = lay(T32_WDR_PAR_SZ + 8);
		EXT[i] = lay(T32_WDR_EXT_SZ);
		HW[i] = lay(T32_WDR_HW_SZ);
		WRK[i] = lay(T32_WDR_WORK_SZ);
	}
	for (i = 0; i < 8; i++)
		PRO[i] = lay(t32_wdr_proc_sz[i]);
	for (i = 0; i < 6; i++)
		LUT[i] = lay(T32_WDR_LUT_SZ);
	BUF = lay(4 * T32_WDR_BUF_SZ);
	BUF2 = lay(4 * T32_WDR_BUF_SZ);	/* the other channels' buffer set (never read by the vendor) */
	SRC = BUF + T32_WDR_BUF_SZ * rb(4);
	CH = rb(4);
	if (f == F_PRO)
		CH = rb(2);
	RDK = rnd();
	for (i = 0; i < 4; i++) {
		u8 *p = IMG + PAR[i];
		u32 k;

		if (rb(2)) p[1831] = 1;
		if (rb(2)) p[1832] = 1;
		if (f == F_PRO) {	/* the deghost strength inputs, scribbled by the deghost stand-in afterwards */
			u16 v[12];
			for (k = 0; k < 12; k++) v[k] = (u16)(rb(3) ? rb(3) : rnd());
			if (rb(2)) v[10] = (u16)rb(5);	/* +514 near the short gain */
			memcpy(p + 494, v, 24);
			for (k = 0; k < 24; k++) p[494 + k] ^= (u8)(k * 7 + i + 0x5c);	/* undone by the stand-in */
		}
	}
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
		VREC[i][1] = T32O_PTR(VA + (i ? BUF2 : BUF));
		VREC[i][3] = T32O_PTR(VA + PAR[i]);
		VREC[i][4] = T32O_PTR(VA + EXT[i]);
		VREC[i][5] = T32O_PTR(VA + HW[i]);
		VREC[i][6] = T32O_PTR(VA + WRK[i]);
		t32_data_e3b40[i] = T32O_PTR(VREC[i]);
		REC[i].nbuf = VREC[i][0];
		REC[i].buf = OA + (i ? BUF2 : BUF);
		REC[i].buf_phys = VREC[i][2];
		REC[i].par = OA + PAR[i];
		REC[i].ext = OA + EXT[i];
		REC[i].hw = OA + HW[i];
		REC[i].work = OA + WRK[i];
		ST.rec[i] = &REC[i];
	}
	for (i = 0; i < 8; i++) {
		*VPROC[i] = T32O_PTR(VA + PRO[i]);
		ST.proc[i] = OA + PRO[i];
	}
	for (i = 0; i < 6; i++) {
		*VLUT[i] = T32O_PTR(VA + LUT[i]);
		ST.lut[i] = OA + LUT[i];
	}
	t32_data_e3a98[0] = ST.fusion_on = (u8)rnd();
	t32_data_e3a99[0] = ST.lut_on = (u8)rnd();
	t32_data_e3a9a[0] = ST.ev_hold = rb(3) == 0 ? 0xff : (u8)rb(2);
	t32_data_e3a80[0] = ST.inited = rb(2) ? 1 : (u8)rnd();
	t32_data_e3b0c[0] = ST.frame = rb(3) == 0 ? 29 : rb(3) == 0 ? rnd() : rb(30);
	t32_data_d8b40[0] = ST.soft_y = rnd();
	ST.short_tgain = rb(2) ? rb(5) : (u64)rnd() << 32 | rnd();
	if (rb(3) == 0) ST.short_tgain = (u64)rb(2) << 32 | rb(5);
	t32_data_e3aa0[0] = (u32)ST.short_tgain;
	t32_data_e3aa4[0] = (u32)(ST.short_tgain >> 32);
	for (i = 0; i < 2; i++) {
		ST.short_ev[i] = (u64)rnd() << 32 | rnd();
		ST.long_ev[i] = (u64)rnd() << 32 | rnd();
		t32_data_e3ab0[2 * i] = (u32)ST.short_ev[i]; t32_data_e3ab0[2 * i + 1] = (u32)(ST.short_ev[i] >> 32);
		t32_data_e3ac0[2 * i] = (u32)ST.long_ev[i]; t32_data_e3ac0[2 * i + 1] = (u32)(ST.long_ev[i] >> 32);
	}
	for (i = 0; i < 4; i++)
		t32_data_e3ad0[i] = ST.ev_changed[i] = rb(2) ? 1 : (u8)rnd();
	NW = NVL = NOL = EVN = SYNC_N = 0;
	SYNC_P = 0;
	t32o_ev_n = 0;
	t32o_rd_hook = rd;
}

static int differ(int f)
{
	u32 i;

	if (memcmp(VA, OA, TOT))
		return 1;
	if (NW != t32o_log.n)
		return 2;
	for (i = 0; i < NW; i++)
		if (W[i].reg != t32o_log.w[i].reg || W[i].val != t32o_log.w[i].val)
			return 3;
	if (NVL != NOL || memcmp(VLOG, OLOG, sizeof(VLOG)))
		return 4;
	if (t32_data_e3a98[0] != ST.fusion_on || t32_data_e3a99[0] != ST.lut_on || t32_data_e3a9a[0] != ST.ev_hold ||
	    t32_data_e3a80[0] != ST.inited || t32_data_e3b0c[0] != ST.frame || t32_data_d8b40[0] != ST.soft_y)
		return 5;
	if (EVN != t32o_ev_n || (EVN && (EVCH != t32o_ev_ch || EVW[2] != t32o_ev_w[2])))
		return 6;
	if (f == F_IRQ && (SYNC_N != T32_WDR_BUF_SZ || SYNC_P != OA + BUF + T32_WDR_BUF_SZ * (RDK & 3)))
		return 7;
	return 0;
}

int main(void)
{
	static u32 hit[F_N];
	u32 n, cases = 0, bad = 0;
	int f;

	for (n = 0; n < 6000; n++) {
		u64 rv, ro;
		int d;

		f = (int)(n % F_N);
		gen(f);
		t32o_reset();
		VA = t32o_alloc(ARN);
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		setup();
		t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
		switch (f) {
		case F_GET:
			rv = t32o_call(FN(tisp_wdr_get_data), CH, T32O_PTR(VA + SRC), 0, 0, 0);
			t32_wdr_get_data(&ST, CH, OA + SRC);
			ro = rv;
			break;
		case F_IRQ:
			rv = t32o_call(FN(tisp_wdr_interrupt_static), CH, 0, 0, 0, 0);
			ro = (u32)t32_wdr_interrupt_static(&ST, CH);
			break;
		default:
			rv = t32o_call(FN(tisp_wdr_process), CH, 0, 0, 0, 0);
			ro = (u32)t32_wdr_process(&ST, CH);
			break;
		}
		cases++;
		hit[f]++;
		d = differ(f);
		if (!d && (u32)rv != (u32)ro)
			d = 20;
		if (d && bad++ < 5)
			fprintf(stderr, "t32_wdr7_oracle: %s case %u differs (%d; writes %u / %u)\n", fname[f], n, d,
				t32o_log.n, NW);
	}
	t32o_rd_hook = 0;
	for (f = 0; f < F_N; f++)
		if (!hit[f])
			bad++;
	printf("t32_wdr7_oracle: %u cases, %u mismatches\n", cases, bad);
	return bad ? 1 : 0;
}
