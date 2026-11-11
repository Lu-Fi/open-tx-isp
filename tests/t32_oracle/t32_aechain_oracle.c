// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_aechain.h, the per-frame AE chain of the driver
 * (t32_aec_irq = tisp_ae_interrupt_static with tisp_ae_long_ev_alloc,
 * t32_aec_calc = tisp_ae_calc_process with par_update_trig / algorithm /
 * long_process) against the whole vendor chain (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc, every callee a reference incl. the fixed point maths).
 *
 * Per case: the AE section of an IQ bin (as is, or with the fields the chain
 * branches on set at random: vary()), a sensor entry dd2d4[0] (232 B) whose function words
 * +124 / +168 / +184 / +212 point at one sensor model (alloc again: 1/16 EV
 * steps up to 2^6; it / again / fps setters logged), the vendor tisp_ae_init
 * (state as in t32_aeinit_oracle), our blocks copied from it.  Then F frames:
 * synthetic zone statistics (4 x 4 KiB zone buffers, the current one picked
 * by system_reg_read(0x1708c) bits 5:4) and a 256-bin histogram in B from a
 * scene brightness (ramp up / down, random steps) times the exposure the
 * vendor chain set (R +576 * +580 * +588), the same input for both sides;
 * the AE interrupt on both, then the AE process (event 1) on both.
 * Compared per frame: every event (channel + 8 words), every sensor call
 * (entry, value, info word 0), every register write (addr / value / order),
 * every byte of T, B, R, D, C, the context words 2..504, the
 * api_ae_long_process blocks (d0950 / d2a30 / d19c0, channel 0), e1510,
 * e1578.  T32O_TRACE=1 prints the per-frame AE of the first bin case.
 *
 * The vendor R block is moved into four zeroed channel blocks after
 * tisp_ae_init: the exposure list mode reads st +1696 + 4 * k up to k = 255,
 * past channel 0's 2184 B into channel 1's (zero with one pipeline; the 1 %
 * first-frame mismatch of openimp #333 read env.c globals there).  The IQ
 * bins run in name order (the random stream must not depend on readdir
 * order: openimp #333, a mutant was missed in another checkout).  Before the bins, 400 synthetic
 * sections run (synth_cases), so the oracle and its mutation check do not depend on T32_IQ_DIR
 * (#333 reopened: without bins no chain case ran).  The attribute trigger is not
 * applied before frame 1 (a trigger before the first tune step divides by
 * the zero exposure clamps).
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_aechain.h"



#define WEAKD __attribute__((weak))
extern u32 t32_data_d3a40[600], t32_data_e15a8[4], t32_data_e15c8[4], t32_data_e15d8[4], t32_data_e1588[4];
extern u32 t32_data_e1598[4];
WEAKD u32 t32_data_e15b8[4];
WEAKD u8 t32_data_e1518[4], t32_data_e151c[4], t32_data_e1520[4], t32_data_e1524[4];	/* AE ROI bytes (.bss) */
/* the vendor's selector jump tables of tisp_ae_ev_alloc_calc (.rodata + 0x1f20 / 0x1f3c) */
u32 t32_data_bf340[8] = { 0x2bb78, 0x2bcd4, 0x2be10, 0x2bf0c, 0x2c034, 0x2c128, 0x2c254, 0 };
u32 t32_data_bf35c[8] = { 0x2cb60, 0x2cd50, 0x2cf7c, 0x2d154, 0x2d37c, 0x2d558, 0x2d768, 0x2e094 };
/* debug strings of calc_process's printk block (not reached: e1500 = 0) */
WEAKD u8 t32_data_c85e4[64], t32_data_c85f8[64], t32_data_c8604[64], t32_data_c860c[64];
WEAKD u8 t32_data_c8618[64], t32_data_c8628[64], t32_data_c8638[64], t32_data_c8644[64];
WEAKD u8 t32_data_c8654[64], t32_data_c8664[64], t32_data_c8670[64], t32_data_c8680[64];
extern u8 t32_data_dd2d4[0x400];
WEAKD u8 t32_data_e1510[4];
WEAKD u32 t32_data_e1578[4], t32_data_e1500[4], t32_data_e14f0[4], t32_data_d9008[1];
WEAKD u8 t32_data_d0950[4 * 1052], t32_data_d2a30[4 * 1028], t32_data_d19c0[4 * 1052];
u64 tisp_ae_interrupt_static(u32, u32, u32, u32), tisp_ae_calc_process(u32, u32, u32, u32);
u64 tisp_ae_sort_H2L(u32, u32, u32, u32);
WEAKD u64 lib_tisp_api_g_runningMode(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAKD u64 tisp_ae_api_get_luma(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAKD u64 tisp_ae_short_set_sensor_integration_time(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAKD u64 tisp_set_ae_short_ag(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAKD u64 tisp_ae_short_ev_alloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAKD u64 tisp_ae_short_sensor_par_ctrls_update(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

static u32 rs = 0x3c6ef372u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* ---- one sensor model, logged per side ---- */
#define NLOG 64
struct slog { u32 n, e[NLOG][3]; };
static struct slog sl[2];
static int side;
static void slog_add(u32 off, u32 arg, const u8 *info)
{
	struct slog *l = &sl[side];

	if (l->n < NLOG) {
		l->e[l->n][0] = off; l->e[l->n][1] = arg; l->e[l->n][2] = t32_ld32(info, 0);
	}
	l->n++;
}
static u32 sns_model(u32 off, u32 arg, u8 *info)
{
	u32 r = 0;

	slog_add(off, arg, info);
	if (off == T32_SNS_ALLOC_AGAIN) {	/* log2 gain Q16 -> 1/16 EV steps, at most 2^6 */
		r = arg & ~0xfffU;
		if (r > 0x60000)
			r = 0x60000;
		t32_wr16(info, 4, (u16)(r >> 12));
	}
	return r;
}
static u64 v_alloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sns_model(T32_SNS_ALLOC_AGAIN, a0, T32O_HOST(a1)); }
static u64 v_again(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sns_model(T32_SNS_SET_AGAIN, a0, T32O_HOST(a1)); }
static u64 v_it(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sns_model(T32_AEC_SNS_SET_IT, a0, T32O_HOST(a1)); }
static u64 v_fps(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sns_model(T32_SNS_SET_FPS, a0, T32O_HOST(a1)); }

/* ---- register reads: a deterministic function of the register and the frame ---- */
static u32 frame;
static u32 rd_model(u32 reg)
{
	if (reg == T32_AE_CUR)
		return (frame & 3) << 4;
	return (reg * 0x9e3779b1u) ^ (frame * 0x10001u);
}
WEAKD u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a2; (void)a3; return rd_model(a1); }

/* ---- our hooks ---- */
static struct { u32 n, w[16][9]; } oev;
static unsigned int odone;
static u32 o_rd(u32 ch, u32 reg) { (void)ch; return rd_model(reg); }
static u32 o_sys(u32 reg) { return rd_model(reg); }
static u32 o_sns(u32 off, u32 arg, u8 *info) { return sns_model(off, arg, info); }
static void o_ev(u32 ch, const u32 *ev)
{
	if (oev.n < 16) {
		oev.w[oev.n][0] = ch;
		memcpy(&oev.w[oev.n][1], ev, 32);
	}
	oev.n++;
}
static unsigned int owarn;
static void o_warn(u32 line) { (void)line; owarn++; }
static void o_done(void *d) { (void)d; odone++; }
static const struct t32_aec_ops OPS = { o_rd, o_sys, o_sns, o_ev, o_warn, o_done };

static struct t32_aec A;
static int o_which;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	if (o_which == 0)
		t32_aec_irq(&A);
	else
		t32_aec_calc(&A);
	return 0;
}

static unsigned int compared, frames, failed;

static int cmpb(const char *what, const u8 *v, const u8 *o, u32 n, const char *tag, u32 f)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (v[i] != o[i]) {
			if (failed++ < 6)
				printf("t32_aechain_oracle: FAIL %s frame %u: %s byte %u vendor %02x ours %02x\n", tag, f,
				       what, i, v[i], o[i]);
			return 1;
		}
	return 0;
}

/* the vendor's writes (t32o_log) against ours (A.w) and the events, sensor calls of one call */
static int cmp_call(const char *what, const char *tag, u32 f)
{
	u32 i, nv = t32o_ev_n;

	if (t32o_log.n != A.n) {
		if (failed++ < 6)
			printf("t32_aechain_oracle: FAIL %s frame %u %s: writes %u / %u\n", tag, f, what, t32o_log.n, A.n);
		return 1;
	}
	for (i = 0; i < A.n; i++)
		if (t32o_log.w[i].reg != A.w[i].reg || t32o_log.w[i].val != A.w[i].val) {
			if (failed++ < 6)
				printf("t32_aechain_oracle: FAIL %s frame %u %s: write %u vendor %x=%x ours %x=%x\n", tag, f,
				       what, i, t32o_log.w[i].reg, t32o_log.w[i].val, A.w[i].reg, A.w[i].val);
			return 1;
		}
	if (nv != oev.n || memcmp(t32o_evlog, oev.w, sizeof(u32) * 9 * (nv < 16 ? nv : 16))) {
		if (failed++ < 6)
			printf("t32_aechain_oracle: FAIL %s frame %u %s: events %u / %u\n", tag, f, what, nv, oev.n);
		return 1;
	}
	if (sl[0].n != sl[1].n || memcmp(sl[0].e, sl[1].e, sizeof(u32) * 3 * (sl[0].n < NLOG ? sl[0].n : NLOG))) {
		if (failed++ < 6)
			printf("t32_aechain_oracle: FAIL %s frame %u %s: sensor calls %u / %u\n", tag, f, what, sl[0].n,
			       sl[1].n);
		return 1;
	}
	return 0;
}

static void begin_call(void)
{
	t32o_log.n = 0;
	t32o_ev_n = 0;
	memset(t32o_evlog, 0, sizeof(t32o_evlog));
	memset(&oev, 0, sizeof(oev));
	memset(sl, 0, sizeof(sl));
}

/* the scene: zone sums, under / over counts and the histogram from one brightness */
static void stats(u8 *zones, u8 *vB, u8 *oB, const u8 *T, u32 lum, u32 noise, u32 flick)
{
	u32 nx = t32_st16(T, 2774), rows = t32_st16(T, 2770), row, k, i, bin;
	u8 *z = zones + ((frame & 3) << 12);
	static u32 h[256];

	memset(z, 0, T32_AEC_ZONE_BYTES);
	memset(h, 0, sizeof(h));
	t32_wr32(z, 12, (rnd() & 3) << 28);
	for (row = 0; row < rows && row < 15; row++)
		for (k = 0; k < nx && k < 15; k++) {
			u8 *q = z + 16 * nx * row + 16 * k;
			u32 l = lum + (noise ? rnd() % noise : 0), s;

			if (flick)	/* banding: a row pattern that moves from frame to frame */
				l = l * (64 + (row * 37 + frame * flick) % 41) / 64;
			s = l * 64;
			u32 q0, q1, q2, q3;

			if (s > 0x3fffff)
				s = 0x3fffff;
			q0 = (s & 0x3fffff) | (s & 0x3ff) << 22;
			q1 = (s >> 10) | (s & 0xfff) << 12;
			q2 = (s >> 12) << 0 | ((l > 1000 ? 300 : rnd() & 15) << 2) | ((l < 8 ? 300 : rnd() & 15) << 16);
			q3 = rnd();
			t32_wr32(q, 8, q0); t32_wr32(q, 12, q1); t32_wr32(q, 16, q2); t32_wr32(q, 20, q3);
			bin = l > 255 ? 255 : l;
			h[bin] += 100;
		}
	for (i = 0; i < 256; i++) {
		t32_wr32(vB, 4 * i, h[i]);
		t32_wr32(oB, 4 * i, h[i]);
	}
}

static void sns_entries(u8 *S)
{
	t32_wr32(S, T32_SNS_ALLOC_AGAIN, T32O_PTR((void *)v_alloc));
	t32_wr32(S, T32_SNS_SET_AGAIN, T32O_PTR((void *)v_again));
	t32_wr32(S, T32_AEC_SNS_SET_IT, T32O_PTR((void *)v_it));
	t32_wr32(S, T32_SNS_SET_FPS, T32O_PTR((void *)v_fps));
}

static int run_case(const char *tag, const u8 *sec, int c, int trace)
{
	static u8 S[232], info[64];
	u8 *base, *vctx, *vT, *vB, *zones, *zs;
	u32 i, f, nfr = 24 + rnd() % 40, lum0 = 4 + rnd() % 200, ramp = rnd() % 4, noise = rnd() & 1 ? 0 : 1 + rnd() % 40;
	u32 flick = rnd() & 1 ? 0 : 1 + rnd() % 31;
	int bad = 0;
	u32 dcw, trig_f = 1 + rnd() % 24;

	for (i = 0; i < sizeof(S); i++)
		S[i] = (u8)rnd();
	memset(info, 0, sizeof(info));
	t32_wr32(info, 0, 1920); t32_wr32(info, 4, 1080); t32_wr32(info, 12, 25 << 16 | 1);
	/* plausible sensor limits: it 2 .. vts, gains log2 Q16 up to 2^6, fps 25/1 */
	t32_wr16(S, 0, 2200); t32_wr16(S, 2, 1125);
	t32_wr32(S, 36, 0x60000); t32_wr32(S, 40, (rnd() & 1) ? 0x30000 : 0);
	t32_wr32(S, 56, 25 << 16 | 1); t32_wr32(S, 60, 0x50001); t32_wr32(S, 64, 0x190001);
	t32_wr32(S, 68, 2 + rnd() % 4); t32_wr32(S, 72, 1000 + rnd() % 200);
	S[123] = 1 + rnd() % 3;
	if (c & 1)
		for (i = 0; i < 24; i++)	/* some random words too */
			S[rnd() % 124] = (u8)rnd();
	sns_entries(S);

	/* vendor init */
	t32o_reset();
	memset(t32_data_e1510, 0, sizeof(t32_data_e1510));
	memset(t32_data_e1578, 0, sizeof(t32_data_e1578));
	memset(t32_data_d0950, 0, sizeof(t32_data_d0950));
	memset(t32_data_d2a30, 0, sizeof(t32_data_d2a30));
	memset(t32_data_d19c0, 0, sizeof(t32_data_d19c0));
	{
		static u32 d3a40_init[600], have;

		if (!have) { memcpy(d3a40_init, t32_data_d3a40, sizeof(d3a40_init)); have = 1; }
		memcpy(t32_data_d3a40, d3a40_init, sizeof(d3a40_init));
	}
	base = t32o_alloc(0x2000);
	memcpy(base + T32_AEI_T_OFF, sec, T32_AEI_T_BYTES);
	t32_data_dd0a4[0] = T32O_PTR(base);
	memcpy(t32_data_dd2d4, S, sizeof(S));
	t32_data_d03a0[0] = 0;
	dcw = rnd() % 4 ? 1 : rnd() % 3;	/* dc284 + 900: the pipeline count (1 = one sensor) */
	t32_wr32(t32_data_dc284, 900, dcw);
	t32o_call((t32o_fn)(void (*)(void))tisp_ae_init, 0, T32O_PTR(info), 0, 0, 0);
	{	/* the vendor's four channel blocks d3a40 + ch * 2184 (env.c has a 2400 B window): channel 0 in front of
		 * zeroed channel blocks, as in the vendor .data (data.c:23135..) with one pipeline */
		u8 *r4 = t32o_alloc(4 * T32_AEI_R_BYTES);

		memset(r4, 0, 4 * T32_AEI_R_BYTES);
		memcpy(r4, T32O_HOST(t32_data_e1588[0]), T32_AEI_R_BYTES);
		t32_data_e1588[0] = T32O_PTR(r4);
	}
	vctx = T32O_HOST(t32_data_e15a8[0]);
	vT = base + T32_AEI_T_OFF;
	vB = T32O_HOST(t32_ld32(vctx, 4));
	zones = t32o_alloc(4 * T32_AEC_ZONE_BYTES);
	zs = t32o_alloc(16);
	t32_wr32(zs, 4, T32O_PTR(zones));
	t32_data_e1598[0] = T32O_PTR(zs);
	t32_data_e15b8[0] = 0;
	memset(t32_data_e1500, 0, 16); memset(t32_data_e14f0, 0, 16); t32_data_d9008[0] = 0;

	/* ours: copies of the vendor's state after tisp_ae_init (its equality: t32_aeinit_oracle) */
	memset(&A, 0, sizeof(A));
	A.b.T = t32o_alloc(T32_AEI_T_BYTES); memcpy(A.b.T, vT, T32_AEI_T_BYTES);
	A.b.B = t32o_alloc(T32_AEI_B_BYTES); memcpy(A.b.B, vB, T32_AEI_B_BYTES);
	A.b.R = t32o_alloc(T32_AEI_R_BYTES); memcpy(A.b.R, T32O_HOST(t32_data_e1588[0]), T32_AEI_R_BYTES);
	A.b.D = t32o_alloc(T32_AEI_D_BYTES); memcpy(A.b.D, T32O_HOST(t32_data_e15d8[0]), T32_AEI_D_BYTES);
	A.b.C = t32o_alloc(T32_AEI_C_BYTES); memcpy(A.b.C, T32O_HOST(t32_data_e15c8[0]), T32_AEI_C_BYTES);
	A.b.ctx = t32o_alloc(T32_AEI_CTX_BYTES); memcpy(A.b.ctx, vctx, T32_AEI_CTX_BYTES);
	t32_wr32(A.b.ctx, 0, T32O_PTR(A.b.T));
	t32_wr32(A.b.ctx, 4, T32O_PTR(A.b.B));
	A.S = t32o_alloc(232); memcpy(A.S, t32_data_dd2d4, 232);
	A.zones = zones;
	A.hist = t32o_alloc(T32_AEC_HIST_BYTES);
	A.comp = t32o_alloc(T32_AEC_COMP_BYTES);
	A.lum = t32o_alloc(T32_AEC_HIST_BYTES);
	A.mode = t32_data_e1510[0];
	A.dcw = dcw;
	A.ops = &OPS;
	if (!A.b.ctx || !A.lum) {
		printf("t32_aechain_oracle: pool exhausted\n");
		exit(2);
	}
	compared++;
	for (f = 0; f < nfr; f++) {
		const u8 *R = T32O_HOST(t32_data_e1588[0]);
		unsigned long long ev = (unsigned long long)(t32_ld32(R, 576) & 0xffff) * t32_ld32(R, 580) / 1024 *
					t32_ld32(R, 588) / 1024;
		u32 lum = lum0;

		frame = f;
		switch (ramp) {	/* scene: constant, ramp up, ramp down, steps */
		case 1: lum = lum0 + f * 6; break;
		case 2: lum = lum0 * (nfr - f) / nfr + 1; break;
		case 3: lum = (f / 8) & 1 ? lum0 * 4 : lum0 / 2 + 1; break;
		}
		lum = (u32)((unsigned long long)lum * (ev ? ev : 1) / 400);
		stats(zones, vB, A.b.B, vT, lum, noise, flick);
		if (rnd() % 23 == 0)	/* the stop flag (algorithm library paused) now and then */
			t32_data_e15b8[0] = A.stop = rnd() & 1;
		if (f == trig_f || (f && rnd() % 29 == 0)) {	/* not before the first tune step (see the header) */
			/* new sensor attributes / tuning limits and the trigger (lib_tisp_sync_sensor_attr +
			 * tisp_ae_par_sensor_trig, or an AE attribute API) on both sides */
			u8 *vS = t32_data_dd2d4, *vR = T32O_HOST(t32_data_e1588[0]);

			t32_wr32(vS, 68, 2 + rnd() % 8); t32_wr32(vS, 72, 300 + rnd() % 1000);
			t32_wr32(vS, 36, 0x20000 + rnd() % 0x50000);
			t32_wr32(vS, 56, (rnd() & 1 ? 25 : 15 + rnd() % 15) << 16 | 1);
			for (i = 0; i < 6; i++)
				if (rnd() & 1)
					t32_wr32(vT, T32_AET_IT_MIN + 4 * i, rnd() & 0xfff);
			for (i = 0; i < 9; i++)	/* the manual / freeze switches of the AE attributes: 0 / 1 */
				vB[12833 + i] = (u8)(rnd() % 3 == 0);
			vR[1285] = 1;
			memcpy(A.S, vS, 232);
			memcpy(A.b.T + T32_AET_IT_MIN, vT + T32_AET_IT_MIN, 24);
			memcpy(A.b.B + 12833, vB + 12833, 9);
			A.b.R[1285] = 1;
		}
		if (rnd() % 11 == 0) {	/* R +1696 = 0: the AE holds the exposure (calc_process' short path) */
			u8 *vR = T32O_HOST(t32_data_e1588[0]);

			vR[1696] = A.b.R[1696] = (u8)(rnd() & 1);
		}

		begin_call();
		side = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_ae_interrupt_static, 0, 0, 0, 0, 0);
		side = 1; o_which = 0;
		t32o_call((t32o_fn)(void (*)(void))ours, 0, 0, 0, 0, 0);
		bad |= cmp_call("irq", tag, f);
		bad |= cmpb("R after irq", R, A.b.R, T32_AEI_R_BYTES, tag, f);
		bad |= cmpb("B after irq", vB, A.b.B, T32_AEI_B_BYTES, tag, f);

		begin_call();
		side = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_ae_calc_process, 0, 0, 0, 0, 0);
		side = 1; o_which = 1;
		t32o_call((t32o_fn)(void (*)(void))ours, 0, 0, 0, 0, 0);
		bad |= cmp_call("calc", tag, f);

		bad |= cmpb("T", vT, A.b.T, T32_AEI_T_BYTES, tag, f);
		bad |= cmpb("B", vB, A.b.B, T32_AEI_B_BYTES, tag, f);
		bad |= cmpb("R", R, A.b.R, T32_AEI_R_BYTES, tag, f);
		bad |= cmpb("D", T32O_HOST(t32_data_e15d8[0]), A.b.D, T32_AEI_D_BYTES, tag, f);
		bad |= cmpb("C", T32O_HOST(t32_data_e15c8[0]), A.b.C, T32_AEI_C_BYTES, tag, f);
		bad |= cmpb("ctx", vctx + 8, A.b.ctx + 8, T32_AEI_CTX_BYTES - 8, tag, f);
		bad |= cmpb("hist", t32_data_d0950, A.hist, T32_AEC_HIST_BYTES, tag, f);
		bad |= cmpb("comp", t32_data_d2a30, A.comp, T32_AEC_COMP_BYTES, tag, f);
		bad |= cmpb("lum", t32_data_d19c0, A.lum, T32_AEC_HIST_BYTES, tag, f);
		if (t32_data_e1510[0] != A.mode || t32_data_e1578[0] != A.frame_cnt) {
			if (failed++ < 6)
				printf("t32_aechain_oracle: FAIL %s frame %u: e1510 %u / %u\n", tag, f, t32_data_e1510[0], A.mode);
			bad = 1;
		}
		if (trace)
			printf("t32_aechain_oracle: trace f%02u scene %4u mean %4u target %4u it %5u again %5u dgain %5u\n",
			       f, lum, t32_st16(A.b.B, 12366), t32_st16(A.b.B, 8570), t32_ld32(A.b.R, 576),
			       t32_ld32(A.b.R, 580), t32_ld32(A.b.R, 588));
		frames++;
		if (bad)
			return 1;
	}
	return 0;
}

/*
 * tisp_ae_sort_H2L on its own: inside the chain only word 0 of each flicker
 * difference row is non-zero, so the order of the others is not visible there.
 */
static unsigned int sort_cases(unsigned int n)
{
	static u32 vv[15], vi[15];
	u32 ov[15], oi[15], c, i;
	unsigned int bad = 0;

	for (c = 0; c < n; c++) {
		for (i = 0; i < 15; i++) {
			u32 m = rnd() % 4;

			vv[i] = m == 0 ? rnd() : m == 1 ? rnd() % 5 - 2 : m == 2 ? 0x80000000u : rnd() % 2001 - 1000;
			if (c & 1)
				vv[i] = vv[rnd() % (i + 1)];	/* ties */
			vi[i] = rnd();
			ov[i] = vv[i]; oi[i] = vi[i];
		}
		t32o_call((t32o_fn)(void (*)(void))tisp_ae_sort_H2L, T32O_PTR(vv), T32O_PTR(vi), 0, 0, 0);
		t32_aec_sort_h2l(ov, oi);
		if (memcmp(ov, vv, sizeof(ov)) || memcmp(oi, vi, sizeof(oi))) {
			if (bad++ < 3)
				printf("t32_aechain_oracle: FAIL sort case %u\n", c);
		}
	}
	return bad;
}

/* bin variants: fields of the AE section the chain branches on, set at random */
static void vary(u8 *sec)
{
	t32_wr16(sec, T32_AET_FRAC, 6 + rnd() % 10);
	t32_wr32(sec, T32_AET_MODE, rnd() % 3);
	t32_wr32(sec, 1748, rnd() & 1);	/* flicker detection */
	t32_wr32(sec, 1684, rnd() % 3);	/* zone window mode */
	t32_wr16(sec, 2770, 1 + rnd() % 15);
	t32_wr16(sec, 2774, 1 + rnd() % 15);
	t32_wr32(sec, T32_AET_FLICKER_HZ, (rnd() & 1) ? 50 : 60);
	t32_wr32(sec, 1696, rnd() % 257);	/* histogram limits of the tune result */
	t32_wr32(sec, 1688, rnd() % 256);
	t32_wr16(sec, 2920, rnd() % 3);
	t32_wr32(sec, 1700, rnd() % 4000);	/* dark / bright histogram sums of the tune result */
	t32_wr32(sec, 1692, rnd() % 4000);
}

/*
 * Synthetic AE sections, independent of T32_IQ_DIR and of the IQ-bin directory
 * contents (openimp #333 reopened: without bins no chain case ran, so every
 * chain mutant, e.g. T32_AEC_B_VAL + 2, passed in a checkout without them).
 * Zero section, random weights / tuning tables from +2900 (random bytes from
 * +2776 make the vendor chain run for minutes), the vary() fields, zone window
 * w / h, the convergence factors +1644 / +1648 (else the zone ratios are not
 * used), an exposure list in every second case.  Fixed seed, fixed count
 * (T32O_NSYNTH, default 400, ~0.2 s).
 */
#define SYNTH_FROM	2900U
static unsigned int synth_cases(unsigned int n)
{
	static u8 sec[T32_AEI_T_BYTES];
	unsigned int c, k;

	for (c = 0; c < n; c++) {
		memset(sec, 0, sizeof(sec));
		for (k = SYNTH_FROM; k < sizeof(sec); k++)	/* weights / tuning tables */
			sec[k] = (u8)rnd();
		vary(sec);
		t32_wr16(sec, 2940, 1 + rnd() % 64);	/* zone window w / h (window mode 1) */
		t32_wr16(sec, 2942, 1 + rnd() % 64);
		t32_wr16(sec, 2944, 1 + rnd() % 64);
		t32_wr16(sec, 1644, 1 + rnd() % 256);	/* convergence speed factors (use the zone ratios) */
		t32_wr16(sec, 1648, 1 + rnd() % 256);
		if (c & 1)	/* an exposure list (list mode reads it): rising time / again */
			for (k = 0; k < 16; k++) {
				t32_wr32(sec, T32_AET_EXPLIST + 4 + 16 * k, 2 + 64 * k + rnd() % 64);
				t32_wr32(sec, T32_AET_EXPLIST + 8 + 16 * k, 1024 + 256 * k);
				t32_wr32(sec, T32_AET_EXPLIST + 12 + 16 * k, 1024);
			}
		if (run_case("synthetic", sec, (int)c, 0))
			break;	/* the first mismatch ends the run (a diverged mutant may not end) */
	}
	return n;
}

static int bin_cases(const char *dir, int trace, unsigned int nvar)
{
	struct dirent **ents, *e;
	int nbins = 0, ne = scandir(dir, &ents, NULL, alphasort), ie;

	if (ne < 0)
		return 0;
	for (ie = 0; ie < ne; ie++) {
		char path[1024];
		static u8 b[1u << 20], sec[T32_AEI_T_BYTES];
		FILE *fp;
		size_t bytes, l;
		u32 off, len = 0, r, k;

		e = ents[ie];
		l = strlen(e->d_name);

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		fp = fopen(path, "rb");
		if (!fp)
			continue;
		bytes = fread(b, 1, sizeof(b), fp);
		fclose(fp);
		off = t32_iq_section(b, (u32)bytes, T32_IQ_SEC_AE, &len);
		if (!off || len < T32_AEI_T_BYTES)
			continue;
		nbins++;
		for (r = 0; r < nvar; r++) {
			memcpy(sec, b + off, T32_AEI_T_BYTES);
			if (r >= 2)
				vary(sec);
			for (k = 0; k < (r == 5 ? 4u : 0u); k++)	/* a few bytes of the tuning tables */
				sec[2960 + rnd() % 300] = (u8)rnd();
			if (run_case(e->d_name, sec, (int)r, trace && nbins == 1 && r == 0))
				goto out;	/* the first mismatch ends the run */
		}
	}
out:
	for (ie = 0; ie < ne; ie++)
		free(ents[ie]);
	free(ents);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	int trace = getenv("T32O_TRACE") && atoi(getenv("T32O_TRACE"));
	unsigned int nvar = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 8;
	int nbins;

	t32o_rd_hook = rd_model;
	failed += sort_cases(20000);
	if (!failed)
		synth_cases(getenv("T32O_NSYNTH") ? (unsigned)atoi(getenv("T32O_NSYNTH")) : 400);
	nbins = failed ? 0 : bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32", trace, nvar);
	printf("t32_aechain_oracle: IQ bins %d, %u cases, %u frames, %u mismatches\n", nbins, compared, frames, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_aechain_oracle: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
