/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE context init: the state part of the vendor tisp_ae_init (0x312f8)
 * with the functions it calls for the AE state, tisp_ae_sensor_par_ctrls_update
 * (0x2aee8), tisp_ae_init_par_calc (0x30fc0), tisp_ae_par_calc (0x30618),
 * tisp_ae_fps_calc_max_exp (0x2c8f0) and tisp_ae_clac_deflicker_cfg (0x30400).
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_aeinit_oracle: the whole vendor tisp_ae_init chain,
 * every byte of the five AE blocks and the register writes of its tail).
 *
 * The vendor blocks of channel ch (the other ported AE functions take the same
 * pointers, see tx_isp_t32_stats.h / aetrig.h / aeinfo.h):
 *   ctx = e15a8[ch]  2020 B: word 0 = T, word 1 = B, word 2 = WDR (sensor info byte 36 != 0)
 *   T   = dd0a4[ch] + 104: the AE tuning section (IQ bin section 1, 3288 B), edited in place
 *   B   = ctx word 1, 12848 B, zeroed: sensor limits, fps, ROI words, deflicker table count
 *   R   = e1588[ch] = d3a40 + ch * 2184: the AE state, .data template t32_ae_state_tmpl()
 *   D   = e15d8[ch], 11768 B, zeroed;  C = e15c8[ch], 16 B = { 20, 30, 14, 5 }
 * The sensor block S is the channel's 232 B entry of t32_data_dd2d4 (sensor
 * attributes of tisp_init: +0 / +4 / +8 the three register-passed words of
 * tisp_ae_sensor_par_ctrls_update, +36 / +40 log2 gains, +56 / +60 / +64
 * ratio words hi:lo, +68 / +72 integration time limits); info is the sensor
 * info of tisp_ae_init's second argument (+0 width, +4 height, +12 fps word,
 * byte +36 WDR).
 *
 * Not here: the register side of tisp_ae_init (tisp_ae_malloc_cfg,
 * tisp_ae_set_hardware_param, tisp_ae_roi_point, system_reg_set_ae_trig: our
 * t32_ae_stats_init in tx_isp_t32_stats.h), the AOV-only
 * tisp_ae_set_riscv_param (t32_data_f0900[ch] == 1) and the interrupt / event
 * callbacks (the caller's wiring).  WDR (info byte 36 != 0: the short-frame
 * tisp_ae_short_sensor_par_ctrls_update and the 256 B copy T+908 -> R+928) is
 * refused (-EINVAL): the T32 front end of this driver has no WDR.
 *
 * Vendor quirks kept: with the T byte 1254 / 1255 / 1257 limit switch on and
 * the sensor maximum not below the tuned one, the maximum falls back to the
 * tuned *minimum* word (T+448 / +452 / +460) when the tuned maximum is below
 * the sensor minimum; a zero hi half of S+60 / S+64 is replaced by 5:1 /
 * 25:1 with a printk (warn hook), S+56 == 0 by 25:1 silently; the final
 * register write goes to 0x473b (decimal 18235) for ch != 0.  The fixed point
 * maths are hooks (struct t32_ae_init_ops).
 */
#ifndef TX_ISP_T32_AEINIT_H
#define TX_ISP_T32_AEINIT_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <stdint.h>
#include <string.h>
#endif
#include "tx_isp_t32_stats.h"

#define T32_AEI_CTX_BYTES	2020U	/* e15a8[ch] */
#define T32_AEI_B_BYTES		12848U	/* e15a8[ch] word 1 */
#define T32_AEI_R_BYTES		2184U	/* e1588[ch], d3a40 + ch * 2184 */
#define T32_AEI_D_BYTES		11768U	/* e15d8[ch] */
#define T32_AEI_C_BYTES		16U	/* e15c8[ch] */
#define T32_AEI_T_OFF		104U	/* T = dd0a4[ch] + 104 */
#define T32_AEI_T_BYTES		3288U
#define T32_AEI_S_BYTES		232U	/* dd2d4 entry */

/* T (tuning) words */
#define T32_AET_IT_MIN		448	/* tuned limits: min it / again / dgain, max it / again / dgain */
#define T32_AET_AG_MIN		452
#define T32_AET_DG_MIN		460
#define T32_AET_IT_MAX		464
#define T32_AET_AG_MAX		468
#define T32_AET_DG_MAX		476
#define T32_AET_EXPLIST		588	/* 16 x { time, again, dgain, - } from +592 */
#define T32_AET_FLICKER_HZ	1660
#define T32_AET_RATIO_A		1668
#define T32_AET_RATIO_B		1672
#define T32_AET_FRAC		1736	/* u16 fraction bits of the fixed point maths */
#define T32_AET_MODE		1740
#define T32_AET_NOCLAMP		1744
/* B words */
#define T32_AEB_SNS		7536	/* 8 words: sensor it / ag / - / dg min, it / ag / - / dg max */
#define T32_AEB_FPS_GAIN	8680
#define T32_AEB_MAX_EXP		8684
#define T32_AEB_IT_OFS		8688
#define T32_AEB_FPS		8692
#define T32_AEB_IT_SPAN		8696
#define T32_AEB_VTS		8554	/* u16 */
#define T32_AEB_FLICKER_N	12842	/* u8 */
/* R words */
#define T32_AER_LIM		608	/* 8 words: the limits after the clamps (608 it min .. 636 dg max) */
#define T32_AER_EV64		80	/* 16 x u64 exposure products */
#define T32_AER_ENTRY		676	/* 16 x { it, ag, dg, - } */
#define T32_AER_RATIO		1260	/* 3 words + copy at 1272 */
#define T32_AER_FLICKER		1700	/* 120 words */

struct t32_ae_init_ops {
	u32 (*div32)(u32 sh, u32 a, u32 b);				/* fix_point_div_32 */
	u32 (*mult2_32)(u32 sh, u32 a, u32 b);				/* fix_point_mult2_32 */
	u32 (*mult3_32)(u32 sh, u32 a, u32 b, u32 c);			/* fix_point_mult3_32 */
	t32_u64 (*mult2)(u32 sh, t32_u64 a, t32_u64 b);			/* fix_point_mult2 */
	t32_u64 (*mult3_64)(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c);	/* fix_point_mult3_64 */
	u32 (*exp2)(u32 v, u32 in_q, u32 out_q);			/* tisp_math_exp2 */
	void (*warn)(u32 line);						/* printk (vendor source line 698 / 705) */
};

struct t32_ae_blk {
	u8 *ctx, *T, *B, *R, *D, *C;
};

/*
 * The vendor .data template of the AE state (d3a40, data.c:22589..23134,
 * channel 0; tisp_ae_init copies channel 0's live block for ch > 0).
 */
static inline void t32_ae_state_tmpl(u8 *R)
{
	u32 o;

	memset(R, 0, T32_AEI_R_BYTES);
	for (o = 0; o <= 152; o += 8)
		t32_wr32(R, o, 0x400);
	for (o = 208; o <= 564; o += 4)
		t32_wr32(R, o, 0x400);
	t32_wr32(R, 576, 0xa);
	t32_wr32(R, 580, 0x400); t32_wr32(R, 584, 0x400); t32_wr32(R, 588, 0x400);
	t32_wr32(R, 592, 0xa);
	t32_wr32(R, 596, 0x400); t32_wr32(R, 600, 0x400); t32_wr32(R, 604, 0x400);
	t32_wr32(R, 608, 0x5);
	t32_wr32(R, 612, 0x400); t32_wr32(R, 616, 0x400); t32_wr32(R, 620, 0x400);
	t32_wr32(R, 624, 0x190);
	t32_wr32(R, 628, 0x1000); t32_wr32(R, 632, 0x1000); t32_wr32(R, 636, 0x1000);
	t32_wr32(R, 640, 0x5);
	t32_wr32(R, 644, 0x400); t32_wr32(R, 648, 0x400); t32_wr32(R, 652, 0x400);
	t32_wr32(R, 656, 0x190);
	t32_wr32(R, 660, 0x1000); t32_wr32(R, 664, 0x1000); t32_wr32(R, 668, 0x1000);
	t32_wr32(R, 1260, 0x6400);
	t32_wr32(R, 1264, 0x1400);
	t32_wr32(R, 1268, 0x6400);
	t32_wr32(R, 1272, 0x6400);
	t32_wr32(R, 1296, 0x3c);
	t32_wr32(R, 1500, 0x64); t32_wr32(R, 1504, 0x64);
	t32_wr32(R, 1508, 0xc8); t32_wr32(R, 1512, 0xc8);
	t32_wr32(R, 1536, 0x1);
	t32_wr32(R, 1540, 0xf);
	t32_wr32(R, 1696, 0x101);
}

static inline u32 t32_aei_frac(const struct t32_ae_blk *b)
{
	return t32_st16(b->T, T32_AET_FRAC);
}

/* hi:lo ratio word -> fixed point hi / lo */
static inline u32 t32_aei_ratio(const struct t32_ae_init_ops *o, u32 sh, u32 v)
{
	return o->div32(sh, (v >> 16) << (sh & 31), (v & 0xffff) << (sh & 31));
}

/* tisp_ae_fps_calc_max_exp (0x2c8f0): longest integration time for fps (0 = 1) */
static inline u32 t32_ae_fps_calc_max_exp(const struct t32_ae_blk *b, const struct t32_ae_init_ops *o, u32 fps)
{
	u32 sh = t32_aei_frac(b), vts = t32_st16(b->B, T32_AEB_VTS);
	u32 span = vts - t32_ld32(b->R, T32_AER_LIM + 16);
	u32 t;

	t = o->mult2_32(sh, vts << (sh & 31), t32_ld32(b->R, T32_AER_RATIO));
	t = o->div32(sh, t, fps ? fps : 1) >> (sh & 31);
	return t < span ? 0 : t - span;
}

/* the vendor's limit merge: lo = max(sensor lo, tuned lo); hi (when enabled) as described in the header */
static inline void t32_aei_limit(u8 *T, u8 *R, const u8 *B, u32 k, u32 tmin, u32 tmax)
{
	u32 lo_o = T32_AER_LIM + 4 * k, hi_o = lo_o + 16;
	u32 snslo = t32_ld32(B, T32_AEB_SNS + 4 * k), snshi = t32_ld32(B, T32_AEB_SNS + 16 + 4 * k);
	u32 hi;

	if (T[1250 + k] == 1)
		t32_wr32(R, lo_o, tmin < snslo ? snslo : tmin);
	if (T[1254 + k] == 1)
		t32_wr32(R, hi_o, snshi < tmax ? snshi : (tmax < snslo ? tmin : tmax));
	hi = t32_ld32(R, hi_o);
	if (hi < t32_ld32(R, lo_o))
		t32_wr32(R, lo_o, hi);
}

/* tisp_ae_sensor_par_ctrls_update (0x2aee8), long frame; S = the dd2d4 entry */
static inline void t32_ae_sensor_par_ctrls_update(const struct t32_ae_blk *b, const u8 *S,
						  const struct t32_ae_init_ops *o)
{
	u8 *T = b->T, *B = b->B, *R = b->R;
	u32 itmin = t32_ld32(T, T32_AET_IT_MIN), agmin = t32_ld32(T, T32_AET_AG_MIN);
	u32 dgmin = t32_ld32(T, T32_AET_DG_MIN), itmax = t32_ld32(T, T32_AET_IT_MAX);
	u32 agmax = t32_ld32(T, T32_AET_AG_MAX), dgmax = t32_ld32(T, T32_AET_DG_MAX);
	u32 sh = t32_aei_frac(b), ra = t32_ld32(S, 56), rb = t32_ld32(S, 60), rc = t32_ld32(S, 64);
	int k;

	if (t32_ld32(T, T32_AET_MODE) == 1)
		itmax = t32_ld32(S, 72);
	t32_wr32(B, T32_AEB_SNS, t32_ld32(S, 68));
	t32_wr32(B, T32_AEB_SNS + 4, 1024);
	t32_wr32(B, T32_AEB_SNS + 8, 1024);
	t32_wr32(B, T32_AEB_SNS + 12, 1024);
	t32_wr32(B, T32_AEB_SNS + 16, t32_ld32(S, 72));
	t32_wr32(B, T32_AEB_SNS + 20, o->exp2(t32_ld32(S, 36), 16, 10));
	t32_wr32(B, T32_AEB_SNS + 24, o->exp2(t32_ld32(S, 40), 16, 10));
	t32_wr32(B, T32_AEB_SNS + 28, 32767);
	for (k = 0; k < 8; k++)
		t32_wr32(R, T32_AER_LIM + 4 * k, t32_ld32(B, T32_AEB_SNS + 4 * k));
	t32_aei_limit(T, R, B, 0, itmin, itmax);
	t32_wr32(T, T32_AET_IT_MIN, t32_ld32(R, T32_AER_LIM));
	t32_wr32(T, T32_AET_IT_MAX, t32_ld32(R, T32_AER_LIM + 16));
	t32_aei_limit(T, R, B, 1, agmin, agmax);
	t32_wr32(T, T32_AET_AG_MIN, t32_ld32(R, T32_AER_LIM + 4));
	t32_wr32(T, T32_AET_AG_MAX, t32_ld32(R, T32_AER_LIM + 20));
	t32_aei_limit(T, R, B, 3, dgmin, dgmax);
	t32_wr32(T, T32_AET_DG_MIN, t32_ld32(R, T32_AER_LIM + 12));
	t32_wr32(T, T32_AET_DG_MAX, t32_ld32(R, T32_AER_LIM + 28));
	t32_wr16(B, T32_AEB_VTS, t32_st16(S, 2));
	t32_wr16(B, T32_AEB_VTS + 2, t32_st16(S, 0));
	if (!(rc >> 16)) {
		o->warn(698);
		rc = 0x190001;
	}
	if (!(rb >> 16)) {
		o->warn(705);
		rb = 0x50001;
	}
	if (!ra)
		ra = 0x190001;
	t32_wr32(R, T32_AER_RATIO, t32_aei_ratio(o, sh, ra));
	t32_wr32(R, T32_AER_RATIO + 4, t32_aei_ratio(o, sh, rb));
	t32_wr32(R, T32_AER_RATIO + 8, t32_aei_ratio(o, sh, rc));
	t32_wr32(T, T32_AET_RATIO_A, ((rc << 1) & 0xffff0000U) + 2);
	t32_wr32(T, T32_AET_RATIO_B, ((rb << 1) & 0xffff0000U) + 2);
}

/* tisp_ae_init_par_calc (0x30fc0) */
static inline void t32_ae_init_par_calc(const struct t32_ae_blk *b, const struct t32_ae_init_ops *o)
{
	u32 sh = t32_aei_frac(b), fps = t32_ld32(b->R, T32_AER_RATIO);

	t32_wr32(b->B, T32_AEB_MAX_EXP,
		 t32_ae_fps_calc_max_exp(b, o, t32_aei_ratio(o, sh, t32_ld32(b->T, T32_AET_RATIO_B))));
	t32_wr32(b->B, T32_AEB_FPS, fps);
	t32_wr32(b->B, T32_AEB_IT_SPAN, t32_st16(b->B, T32_AEB_VTS) - t32_ld32(b->B, T32_AEB_IT_OFS));
}

/* tisp_ae_par_calc (0x30618) */
static inline void t32_ae_par_calc(const struct t32_ae_blk *b, const struct t32_ae_init_ops *o)
{
	u8 *T = b->T, *B = b->B, *R = b->R;
	u32 sh = t32_aei_frac(b), us = 1000000U << (sh & 31), itmin = t32_ld32(R, T32_AER_LIM);
	u32 maxexp, k, idx, it, x, lo, hi;
	t32_u64 p;

	p = o->mult2(sh, t32_ld32(R, T32_AER_RATIO), (t32_u64)(t32_st16(B, T32_AEB_VTS) << (sh & 31)));
	t32_wr32(B, T32_AEB_FPS_GAIN, o->div32(sh, us, (u32)p));
	t32_wr32(R, T32_AER_ENTRY, t32_ld32(R, T32_AER_LIM));
	t32_wr32(R, T32_AER_ENTRY + 4, t32_ld32(R, T32_AER_LIM + 4));
	t32_wr32(R, T32_AER_ENTRY + 8, t32_ld32(R, T32_AER_LIM + 12));
	if (!t32_ld32(T, T32_AET_RATIO_B))
		t32_wr32(T, T32_AET_RATIO_B, 1);
	maxexp = t32_ae_fps_calc_max_exp(b, o, t32_aei_ratio(o, sh, t32_ld32(T, T32_AET_RATIO_B)));
	for (k = 1; k < 16; k++) {	/* the tuned exposure list: time (us) -> lines, gains as tuned */
		u32 to = T32_AET_EXPLIST + 4 + 16 * k, ro = T32_AER_ENTRY + 16 * k;

		if (!t32_ld32(T, to))
			t32_wr32(T, to, 1);
		it = t32_ae_fps_calc_max_exp(b, o, o->div32(sh, us, t32_ld32(T, to) << (sh & 31)));
		t32_wr32(R, ro, it < itmin ? itmin : (maxexp < it ? maxexp : it));
		t32_wr32(R, ro + 4, t32_ld32(T, to + 4));
		t32_wr32(R, ro + 8, t32_ld32(T, to + 8));
	}
	for (k = 0; k < 16; k++) {
		u32 ro = T32_AER_ENTRY + 16 * k;

		p = o->mult3_64(sh, (t32_u64)t32_ld32(R, ro) << (sh & 63), t32_ld32(R, ro + 4), t32_ld32(R, ro + 8));
		t32_wr64(R, T32_AER_EV64 + 8 * k, p);
	}
	/* first entry equal to its successor (64 bit), else 14 / 15 by the last pair */
	for (idx = 0; idx < 14; idx++)
		if (t32_ld64(R, T32_AER_EV64 + 8 * idx) == t32_ld64(R, T32_AER_EV64 + 8 * idx + 8))
			break;
	if (idx == 14 && t32_ld64(R, T32_AER_EV64 + 8 * 14) != t32_ld64(R, T32_AER_EV64 + 8 * 15))
		idx = 15;
	t32_wr16(B, 8576, idx);
	if (t32_ld32(T, T32_AET_NOCLAMP))
		return;
	lo = t32_ld32(R, T32_AER_LIM + 4);
	hi = t32_ld32(R, T32_AER_LIM + 12);
	for (k = 0; k < 16; k++) {	/* clamp the entries to the limits */
		u32 ro = T32_AER_ENTRY + 16 * k;

		if (!t32_ld32(T, T32_AET_MODE)) {
			x = t32_ld32(R, ro);
			if (x < t32_ld32(R, T32_AER_LIM))
				t32_wr32(R, ro, t32_ld32(R, T32_AER_LIM));
			else if (t32_ld32(R, T32_AER_LIM + 16) < x)
				t32_wr32(R, ro, t32_ld32(R, T32_AER_LIM + 16));
		}
		x = t32_ld32(R, ro + 4);
		if (x < lo)
			t32_wr32(R, ro + 4, lo);
		else if (t32_ld32(R, T32_AER_LIM + 20) < x)
			t32_wr32(R, ro + 4, t32_ld32(R, T32_AER_LIM + 20));
		x = t32_ld32(R, ro + 8);
		if (x < hi)
			t32_wr32(R, ro + 8, hi);
		else if (t32_ld32(R, T32_AER_LIM + 28) < x)
			t32_wr32(R, ro + 8, t32_ld32(R, T32_AER_LIM + 28));
	}
}

/* tisp_ae_clac_deflicker_cfg (0x30400): the integration times of the flicker periods */
static inline void t32_ae_clac_deflicker_cfg(const struct t32_ae_blk *b, const struct t32_ae_init_ops *o)
{
	u8 *B = b->B, *R = b->R;
	u32 sh = t32_aei_frac(b), per = (t32_ld32(b->T, T32_AET_FLICKER_HZ) << 1) << (sh & 31);
	u32 fpsr = t32_ld32(R, T32_AER_RATIO + 4), ratio = t32_ld32(R, T32_AER_RATIO);
	u32 vts = t32_st16(B, T32_AEB_VTS), n, k;
	u32 half = (u32)((int)(1U << (sh & 31)) / 2);

	n = (o->div32(sh, per, fpsr ? fpsr : 1) >> (sh & 31)) & 0xff;
	B[T32_AEB_FLICKER_N] = n > 120 ? 120 : (n ? n : 1);
	for (k = 1; k <= B[T32_AEB_FLICKER_N]; k = (k + 1) & 0xff) {
		u32 d = o->div32(sh, vts << (sh & 31), per);

		t32_wr32(R, T32_AER_FLICKER + 4 * (k - 1),
			 (o->mult3_32(sh, k << (sh & 31), ratio, d) + half) >> (sh & 31));
	}
	for (k = B[T32_AEB_FLICKER_N]; k < 120; k++)
		t32_wr32(R, T32_AER_FLICKER + 4 * k, t32_ld32(R, T32_AER_FLICKER + 4 * (B[T32_AEB_FLICKER_N] - 1)));
	B[T32_AEB_FLICKER_N]--;
}

/*
 * The zone sizes tisp_ae_set_hardware_param (0x2a608) keeps in B: half the
 * frame size past the window start, divided by the zone count (signed C
 * division as the vendor), one u16 per zone column (B+11796) / row (B+11826).
 * The vendor tables hold 15 entries; a count above 15 makes the vendor write
 * on into the following words: ours stops at the end of B.
 */
static inline void t32_ae_zone_sizes(const struct t32_ae_blk *b)
{
	u32 nx = t32_st16(b->T, 2774), ny = t32_st16(b->T, 2770), i;
	int hw = (int)(t32_st16(b->B, 8548) - t32_st16(b->T, 2772) + 1) / 2;
	int hh = (int)(t32_st16(b->B, 8550) - t32_st16(b->T, 2768) + 1) / 2;

	for (i = 0; i < nx && 11796 + 2 * i + 2 <= T32_AEI_B_BYTES; i++)
		t32_wr16(b->B, 11796 + 2 * i, (u32)(hw / (int)nx));
	for (i = 0; i < ny && 11826 + 2 * i + 2 <= T32_AEI_B_BYTES; i++)
		t32_wr16(b->B, 11826 + 2 * i, (u32)(hh / (int)ny));
}

/* the register tail of tisp_ae_init (direct addresses, t32_data_d03a0 == 0) */
static inline unsigned int t32_ae_init_regs(u32 ch, const u8 *R, struct t32_stats_write *w)
{
	u32 p = t32_ld32(R, 288);
	unsigned int n = 0;

	p = (p & 0xffff) | p << 16;
	n = t32_stats_add(w, n, 0x3054, 1);
	n = t32_stats_add(w, n, 0x3058, p);
	n = t32_stats_add(w, n, 0x305c, p);
	n = t32_stats_add(w, n, 0x3204, 1);
	n = t32_stats_add(w, n, 0x3208, p);
	n = t32_stats_add(w, n, 0x320c, p);
	n = t32_stats_add(w, n, 0x3010, 0xffffffffU);
	n = t32_stats_add(w, n, ch ? 18235 : 0x17004, 0x0134da11);
	return n;
}

/*
 * tisp_ae_init, state part, for channel ch: b holds the caller's blocks (R
 * already the template / the channel 0 copy, ctx / B / D / C any content),
 * T the tuning section copy, S the sensor entry, info the sensor info, pipes
 * word 900 of the channel state dc284[ch].  Returns the number of register
 * writes of the tail in w (<= 8), -EINVAL for a WDR sensor.
 */
static inline int t32_ae_ctx_init(u32 ch, const struct t32_ae_blk *b, const u8 *S, const u8 *info, u32 pipes,
				  const struct t32_ae_init_ops *o, struct t32_stats_write *w)
{
	if (info[36])
		return -EINVAL;
	memset(b->D, 0, T32_AEI_D_BYTES);
	t32_wr32(b->C, 0, 20);
	t32_wr32(b->C, 4, 30);
	t32_wr32(b->C, 8, 14);
	t32_wr32(b->C, 12, 5);
	memset(b->ctx, 0, T32_AEI_CTX_BYTES);
	memset(b->B, 0, T32_AEI_B_BYTES);
	t32_wr32(b->ctx, 0, (u32)(uintptr_t)b->T);
	t32_wr32(b->ctx, 4, (u32)(uintptr_t)b->B);
	t32_wr32(b->ctx, 8, 0);
	t32_wr16(b->B, 8548, t32_ld32(info, 0));
	t32_wr16(b->B, 8550, t32_ld32(info, 4));
	t32_wr16(b->B, 8552, t32_ld32(info, 12));
	t32_wr32(b->B, T32_AEB_IT_OFS, t32_ld32(S, 72));
	t32_ae_sensor_par_ctrls_update(b, S, o);
	t32_wr32(b->R, T32_AER_RATIO + 12, t32_ld32(b->R, T32_AER_RATIO));
	memcpy(b->R + 1544, b->T + 1496, 120);
	memcpy(b->R + 1664, b->T + 2870, 30);
	t32_ae_init_par_calc(b, o);
	t32_ae_par_calc(b, o);
	t32_ae_zone_sizes(b);
	if (pipes >= 2)
		t32_wr32(b->T, T32_AET_MODE, 0);
	t32_ae_clac_deflicker_cfg(b, o);
	t32_wr16(b->B, 8566, 0);
	t32_wr16(b->B, 8568, 0);
	return (int)t32_ae_init_regs(ch, b->R, w);
}

#endif /* TX_ISP_T32_AEINIT_H */
