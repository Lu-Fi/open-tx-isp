/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host test of the T33 per-frame AE front (driver/t32/tx_isp_t33_aealg.h).
 * Without arguments: the flat zone / histogram load into the T32 places,
 * the tag counters, the start parameters, the zone sizes.  With T33_IQ_BIN
 * (a cv2005 PRJ008 bin): tisp_ae_init's state on the AE section and a
 * closed loop of T33_AE_FRAMES (default 400) frames on a model sensor
 * (zone luma proportional to the exposure, clipped): the up steps stay at
 * or below 1.25x, the down steps at or above 0.75x, the fraction of a
 * line goes to the analog gain, and the loop settles in the stable band.
 * T33_AE_PRINT=1 prints every frame.
 */
#define TX_ISP_T33
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
#include "../driver/t32/tx_isp_t32_rcfg33.h"
#include "../driver/t32/tx_isp_t32_iq.h"
#include "../driver/t32/tx_isp_t33_aealg.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static uint8_t bin[0x40000], zb[4 * T33_AE_ZBUF_BYTES], hb[4 * T33_AE_HBUF_BYTES];
static u32 it_set, ag_set;

static void put32(uint8_t *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t *p, u32 v) { p[0] = v; p[1] = v >> 8; }

/* zone buffer k: every zone the three sums s, counts lo / hi, tag t; histogram all in bin b */
static void fill(int k, u32 s, u32 lo, u32 hi, u32 t, u32 b, u32 n)
{
	uint8_t *z = zb + k * T33_AE_ZBUF_BYTES, *h = hb + k * T33_AE_HBUF_BYTES;
	u32 i;

	memset(z, 0, T33_AE_ZBUF_BYTES);
	memset(h, 0, T33_AE_HBUF_BYTES);
	put16(z, t);
	put16(z + T33_AE_ZTAG, t);
	for (i = 0; i < 225; i++) {
		uint8_t *p = z + T33_STAT_HEAD + 16 * i;

		put32(p, s); put32(p + 4, s + 1); put32(p + 8, s + 2);
		put16(p + 12, lo); put16(p + 14, hi);
	}
	put16(h, t);
	put16(h + T33_AE_HTAG, t);
	put32(h + T33_STAT_HEAD + 4 * b, n);
}

static u32 h_rd(u32 ch, u32 reg) { (void)ch; (void)reg; return 0; }
static u32 h_sys(u32 reg) { (void)reg; return 0; }
static u32 h_sensor(u32 off, u32 arg, u8 *info)
{
	if (off == T32_SNS_ALLOC_AGAIN) {
		t32_wr16(info, 4, arg >> 12);
		return arg;		/* any gain: no rounding */
	}
	if (off == T32_SNS_SET_AGAIN)
		ag_set = arg;
	if (off == T32_AEC_SNS_SET_IT)
		it_set = arg;
	return 0;
}
static void h_event(u32 ch, const u32 *ev) { (void)ch; (void)ev; }
static void h_warn(u32 line) { (void)line; }
static void h_done(void *d) { (void)d; }
static const struct t32_aec_ops hops = { h_rd, h_sys, h_sensor, h_event, h_warn, h_done };

static u32 i_div32(u32 sh, u32 a, u32 b) { return t32_aem_div32(sh, a, b); }
static u32 i_mult2_32(u32 sh, u32 a, u32 b) { return t32_aem_mult2_32(sh, a, b); }
static u32 i_mult3_32(u32 sh, u32 a, u32 b, u32 c) { return t32_aem_mult3_32(sh, a, b, c); }
static t32_u64 i_mult2(u32 sh, t32_u64 a, t32_u64 b) { return t32_fixp_mult2(sh, a, b); }
static t32_u64 i_mult3_64(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c) { return t32_aem_mult3_64(sh, a, b, c); }
static u32 i_exp2(u32 v, u32 iq, u32 oq) { return t32_aem_exp2(v, iq, oq); }
static const struct t32_ae_init_ops iops = { i_div32, i_mult2_32, i_mult3_32, i_mult2, i_mult3_64, i_exp2, h_warn };

static void offline(void)
{
	struct t32_aec a;
	struct t33_ae_front f = { 2, 1, 0, 0 };
	static u8 T[T32_AEI_T_BYTES], B[T32_AEI_B_BYTES], R[T32_AEI_R_BYTES];

	memset(&a, 0, sizeof(a));
	a.b.T = T; a.b.B = B; a.b.R = R;
	put16(T + 2770, 15); put16(T + 2774, 15);
	fill(2, 1000, 7, 9, 0x55, 0, 0);
	fill(1, 0, 0, 0, 0x66, 37, 12345);
	t33_ae_load(&a, &f, zb + 2 * T33_AE_ZBUF_BYTES, hb + 1 * T33_AE_HBUF_BYTES);
	CHECK(t32_ld32(B, 2048 + 60 * 14 + 4 * 14) == 1000);
	CHECK(t32_ld32(B, 2948 + 60 * 3 + 4 * 5) == 1001);
	CHECK(t32_ld32(B, 3848) == 1002);
	CHECK(t32_ld32(B, 4748 + 60 * 7) == 7);
	CHECK(t32_ld32(B, 5648 + 4 * 9) == 9);
	CHECK(t32_ld32(B, 4 * 37) == 12345 && t32_ld32(B, 4 * 36) == 0);
	CHECK(f.zbad == 0 && f.hbad == 0);
	put16(zb + 2 * T33_AE_ZBUF_BYTES + T33_AE_ZTAG, 0x56);
	put16(hb + 1 * T33_AE_HBUF_BYTES + T33_AE_HTAG, 0x67);
	t33_ae_load(&a, &f, zb + 2 * T33_AE_ZBUF_BYTES, hb + 1 * T33_AE_HBUF_BYTES);
	CHECK(f.zbad == 1 && f.hbad == 1);
	t33_ae_set_start_param(&a, 0x10, 0x400, 0x400, 0x4000);
	CHECK(t32_ld32(R, 576) == 0x10 && t32_ld32(R, 580) == 0x400 && t32_ld32(R, 588) == 0x400);
	CHECK(t32_ld32(R, 208 + 36) == 0x10 && t32_ld32(R, 248 + 36) == 0x400 && t32_ld32(R, 288 + 36) == 0x400);
	CHECK(t32_ld64(R, 72) == 0x10ULL << 10 && t32_ld32(R, 328 + 36) == 0x400);
	CHECK(t32_ld32(T, 368) == 0x10 && t32_ld32(T, 372) == 0);
	put16(B, 0); put16(B + 8548, 1920); put16(B + 8550, 1080);
	put16(T + 2772, 0); put16(T + 2768, 0);
	t33_ae_zone_sizes(&a);
	CHECK(t32_st16(B, 11796) == 64 && t32_st16(B, 11796 + 28) == 64);
	CHECK(t32_st16(B, 11826) == 36 && t32_st16(B, 11826 + 28) == 36);
}

static void loop(const char *bp)
{
	FILE *fp = fopen(bp, "rb");
	u32 bytes, off, len, k, frames = getenv("T33_AE_FRAMES") ? (u32)atoi(getenv("T33_AE_FRAMES")) : 400;
	int print = getenv("T33_AE_PRINT") != NULL, settled = 0, frac_ag = 0;
	static struct t32_aec a;
	struct t33_ae_front f = { 0, 0, 0, 0 };
	struct t32_stats_write w[8];
	u8 S[232] = { 0 }, info[64] = { 0 };
	double ev = 1320, prev = 0;

	if (!fp) {
		printf("cannot open %s\n", bp);
		bad++;
		return;
	}
	bytes = (u32)fread(bin, 1, sizeof(bin), fp);
	fclose(fp);
	off = t32_iq_section(bin, bytes, 1, &len);
	CHECK(off && len >= T32_AEI_T_BYTES);
	if (!off)
		return;
	a.b.T = calloc(1, T32_AEI_T_BYTES); a.b.ctx = calloc(1, T32_AEI_CTX_BYTES);
	a.b.B = calloc(1, T32_AEI_B_BYTES); a.b.R = calloc(1, T32_AEI_R_BYTES);
	a.b.D = calloc(1, T32_AEI_D_BYTES); a.b.C = calloc(1, T32_AEI_C_BYTES);
	a.hist = calloc(1, T32_AEC_HIST_BYTES); a.comp = calloc(1, T32_AEC_COMP_BYTES);
	a.lum = calloc(1, T32_AEC_HIST_BYTES);
	memcpy(a.b.T, bin + off, T32_AEI_T_BYTES);
	/* the QEMU cv2005 block: 1106 x 1320, again up to 32x (log2 5.0), it 1 .. 1310, 30 fps */
	t32_wr16(S, 0, 1106); t32_wr16(S, 2, 1320);
	t32_wr32(S, 36, 5 << 16);
	t32_wr32(S, 56, 0x1e0001); t32_wr32(S, 60, 0x10001); t32_wr32(S, 64, 0x1e0001);
	t32_wr32(S, 68, 1); t32_wr32(S, 72, 1310);
	S[116] = 2; S[117] = 2; S[123] = 1;
	t32_ae_state_tmpl(a.b.R);
	t32_wr32(info, 0, 1920); t32_wr32(info, 4, 1080); t32_wr32(info, 12, 25 << 16 | 1);
	CHECK(t32_ae_ctx_init(0, &a.b, S, info, 1, &iops, w) >= 0);
	a.S = S; a.dcw = 1; a.ops = &hops;
	t32_aec_bind(&a);
	t33_ae_set_start_param(&a, T33_AE_FST_IT, T33_AE_FST_AG, T33_AE_FST_DG, T33_AE_START_EV);
	t33_ae_zone_sizes(&a);
	for (k = 0; k < frames; k++) {
		/* model: zone mean over its 64 x 36 area = 0.1 * exposure, clipped at 4000 (12 bit) */
		double l = ev * 0.1;
		u32 s = (u32)(l > 4000 ? 4000 : l) * 64 * 36 / 3, cur;

		f.zidx = f.hidx = k & 3;
		fill(k & 3, s, 0, 0, k & 0xffff, (u32)(l > 4000 ? 255 : l / 16), 100000);
		t33_ae_frame(&a, &f, zb, hb);
		t33_ae_calc(&a, &f, f.zbad, f.hbad);
		cur = t32_ld32(a.b.R, 576);
		if (a.mode == 1 && it_set) {
			double out = (double)cur * t32_ld32(a.b.R, 580) / 1024.0 * t32_ld32(a.b.R, 588) / 1024.0;

			/* the sensor shows what the last allocation applied (B + 7616 / 7620 / 7628) */
			ev = (double)t32_ld32(a.b.B, 7616) * t32_ld32(a.b.B, 7620) / 1024.0 * t32_ld32(a.b.B, 7628) / 1024.0;
			if (prev > 0 && out > prev * 1.26 + 0.01)
				CHECK(!"up step above 1.25x");
			if (prev > 0 && out < prev * 0.74 - 0.01 && out > 1.01)
				CHECK(!"down step below 0.75x");
			prev = out;
			if (cur < 50 && t32_ld32(a.b.R, 580) > 1024 && t32_ld32(a.b.R, 588) == 1024)
				frac_ag = 1;
		}
		if (print)
			printf("frame %3u mean %3u target %3u it %4u ag %5u dg %5u ev %.1f\n", k,
			       t32_st16(a.b.B, 12366), t32_st16(a.b.B, 8570), cur, t32_ld32(a.b.R, 580),
			       t32_ld32(a.b.R, 588), ev);
		if (t32_st16(a.b.B, 8568) == 1)
			settled = 1;
	}
	CHECK(settled || frames < 300);
	CHECK(frac_ag);
}

int main(void)
{
	const char *bp = getenv("T33_IQ_BIN");

	offline();
	if (bp)
		loop(bp);
	printf("tx_isp_t33_ae_test: %s\n", bad ? "FAIL" : "PASS");
	return bad ? 1 : 0;
}
