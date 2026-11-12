/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) per-frame AE: the vendor tisp_ae_interrupt_static /
 * tisp_normal_ae_calc_process flow on the T32 port of the AE algorithm
 * (tx_isp_t32_ae*.h / tx_isp_t32_aestats.h, from Matt Davis' recovered C,
 * PR #15), with the T33 statistics front.
 *
 * What the T33 module does differently from the T32 one (Ghidra project
 * t33, vendor tx-isp-t33.ko; the T32 port keeps the T32 block layout, the
 * T33 one packs the B block into stAeInfo at other offsets and keeps the T
 * (tuning) layout and the R (ae_cache) words up to +1286):
 *  - no tisp_ae_get_statistics / tisp_ae_get_hist unpack: tisp_ae_weight_mean
 *    reads the flat zone records (tx_isp_t33_stats.h) and the histogram
 *    buffer itself.  t33_ae_load puts the three luma sums and the two
 *    counts of a zone where t32_ae_get_statistics leaves them (B + 2048 /
 *    2948 / 3848 / 4748 / 5648 + 60 * row + 4 * col) and the 256 bins at
 *    B + 0 (the T32 weight_mean's histogram), so the T32 weight_mean runs
 *    unchanged (same sums, area, weights, the histogram tail).
 *  - weight_mean compares the closing tags of the zone (+0xe18) and the
 *    histogram (+0x408) buffer with their head tags; a mismatch counts
 *    (stAeInfo + 0x4de0 / 0x4de4) and sets ae_cache + 0x6aa, which makes
 *    tisp_ae_tune skip its EV allocation of that frame.  Counted here
 *    (t33_ae_front.zbad / hbad); the skip is not ported (the T32 tune has
 *    no such flag; 0 tag errors in QEMU so far).
 *  - interrupts: system irq 4 -> tisp_ae_interrupt_static ->
 *    tisp_normal_ae_interrupt_hist: tisp_ae_long_ev_alloc when the AE
 *    result is ready (ae_cache + 0x6c4 == 1, the T32 e1510 mode), the
 *    histogram index (0x1708c bits 0..1), event 1 (calc_process); irq 5 ->
 *    tisp_ae_interrupt_hist -> tisp_normal_ae_interrupt_static: the zone
 *    index (0x1708c bits 4..5).
 *  - tisp_ae_long_ev_alloc writes the DG into module list 5 slots 3 / 4
 *    (0x4060 / 0x4064, (dg & 0xffff) | dg << 16) and runs list 5 when its
 *    DMA is idle, instead of 0x3058 / 0x305c (/ 0x3208 / 0x320c), 0x3010,
 *    0x26048, 0x2603c; it checks 0x4060 >> 16 against the DG (printk
 *    only).  The T32 port's 0x3058 word is that value (t33_ae_dg_word).
 *  - tisp_normal_ae_init: the R template (ae_cache_default, 0x6d0 B: the
 *    T32 template words up to +1540, no face words 1500..1512) and then
 *    tisp_ae_set_start_param from the first-frame defaults
 *    (fstframe_defaultp without RISC-V: EV 0x4000, again 0x400, dgain
 *    0x400, integration time 0x10): IT / AG / DG into R + 576 / 580 / 588
 *    and the ten-entry history tables (R + 208 / 248 / 288, products at
 *    R + 0 (u64) / 328), the start EV >> 10 into T + 368 / 372
 *    (t33_ae_set_start_param).  Zone sizes (normal_ae_reg_calc): all 15
 *    column / row entries (width - x0 + 1) / 30, (height - y0 + 1) / 30
 *    (t33_ae_zone_sizes; T32: half the span / zones, the same for 15
 *    zones).
 *  - tisp_normal_ae_calc_process: no DG register read-back and no
 *    "frozen" DG path (the T32 calc_process with R + 1696 = 1, as both
 *    templates leave it, takes the same steps: trigger update, algorithm,
 *    long process).
 */
#ifndef TX_ISP_T33_AEALG_H
#define TX_ISP_T33_AEALG_H

#include "tx_isp_t32_aestats.h"
#include "tx_isp_t32_aechain.h"
#include "tx_isp_t33_stats.h"

#define T33_AE_FST_EV		0x4000U		/* fstframe_defaultp words 0 / 1 (u64) */
#define T33_AE_FST_AG		0x400U		/* word 3 (info + 0x3a4) */
#define T33_AE_FST_DG		0x400U		/* word 4 (info + 0x3a8) */
#define T33_AE_FST_IT		0x10U		/* word 5 (info + 0x3ac) */
#define T33_AE_DG_REG		0x3058U		/* the T32 port's DG word */
/*
 * The first AE step starts from that EV (16 lines at 1x): in QEMU with a
 * deterministic guest clock (-icount shift=0) the vendor's first steps are
 * 15/1083, 15/1078, 19/1065, ... from frame 3, as ours.  The earlier
 * "1 line, 3 frames later" vendor runs were host-speed artefacts: the
 * vendor event thread (AE calc, AWB, the gain / EV block refreshes in one
 * FIFO) lagged the frames under plain TCG (qemu/ae-compare-ic.txt).
 */

struct t33_ae_front {
	u32 zidx, hidx;		/* stat_num[ch]: zone / histogram buffer of the last interrupts */
	u32 zbad, hbad;		/* stAeInfo + 0x4de0 / 0x4de4 */
};

/* tisp_ae_set_start_param (no RISC-V first frame) on the T32 blocks */
static inline void t33_ae_set_start_param(struct t32_aec *a, u32 it, u32 ag, u32 dg, t32_u64 ev)
{
	u8 *R = a->b.R;
	unsigned int k;

	t32_wr32(R, 576, it);
	t32_wr32(R, 580, ag);
	t32_wr32(R, 588, dg);
	for (k = 0; k < 10; k++) {
		t32_wr32(R, 208 + 4 * k, it);
		t32_wr32(R, 248 + 4 * k, ag);
		t32_wr32(R, 288 + 4 * k, dg);
		t32_wr64(R, 8 * k, t32_aem_mult3_64(10, (t32_u64)it << 10, ag, dg));
		t32_wr32(R, 328 + 4 * k, t32_aem_mult2_32(10, ag, dg));
	}
	t32_wr32(a->b.T, 368, (u32)ev >> 10 | (u32)(ev >> 32) << 22);
	t32_wr32(a->b.T, 372, (u32)(ev >> 32) >> 10);
}

/* normal_ae_reg_calc's zone sizes: all 15 entries, width / height span over 30 */
static inline void t33_ae_zone_sizes(struct t32_aec *a)
{
	u32 zx = (u32)((int)(t32_st16(a->b.B, 8548) - t32_st16(a->b.T, 2772) + 1) / 0x1e);
	u32 zy = (u32)((int)(t32_st16(a->b.B, 8550) - t32_st16(a->b.T, 2768) + 1) / 0x1e);
	unsigned int i;

	for (i = 0; i < 15; i++) {
		t32_wr16(a->b.B, 11796 + 2 * i, zx);
		t32_wr16(a->b.B, 11826 + 2 * i, zy);
	}
}

/*
 * The flat zone buffer zb (0x1000 B) and histogram buffer hb (0x800 B) into
 * the places the T32 weight_mean reads; the tag checks of the T33
 * weight_mean.  rows / cols: T + 2770 / 2774 (at most 15).
 */
static inline void t33_ae_load(struct t32_aec *a, struct t33_ae_front *f, const u8 *zb, const u8 *hb)
{
	u32 rows = t32_st16(a->b.T, 2770), cols = t32_st16(a->b.T, 2774), r, c, i;
	u8 *B = a->b.B;

	if (rows > T33_AE_GRID)
		rows = T33_AE_GRID;
	if (cols > T33_AE_GRID)
		cols = T33_AE_GRID;
	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++) {
			const u8 *p = zb + T33_STAT_HEAD + (r * T33_AE_GRID + c) * T33_AE_ZONE_BYTES;

			i = 60 * r + 4 * c;
			t32_wr32(B, 2048 + i, t33_st_le32(p));
			t32_wr32(B, 2948 + i, t33_st_le32(p + 4));
			t32_wr32(B, 3848 + i, t33_st_le32(p + 8));
			t32_wr32(B, 4748 + i, t33_st_le16(p + 12));
			t32_wr32(B, 5648 + i, t33_st_le16(p + 14));
		}
	if (t33_st_le16(zb) != t33_st_le16(zb + T33_AE_ZTAG))
		f->zbad++;
	for (i = 0; i < 256; i++)
		t32_wr32(B, 4 * i, t33_st_le32(hb + T33_STAT_HEAD + 4 * i));
	if (t33_st_le16(hb) != t33_st_le16(hb + T33_AE_HTAG))
		f->hbad++;
}

/*
 * One AE frame as the T33 runs it at system irq 4: tisp_ae_long_ev_alloc
 * once the tune ran (its writes in a->w[0 .. a->n)), then (event 1)
 * tisp_normal_ae_calc_process on the zone / histogram buffers of the last
 * interrupts.  The sensor, event and register hooks are the chain's
 * (struct t32_aec_ops).  Returns the writes of the allocation (the DG word
 * among them, t33_ae_dg_word).
 */
static inline unsigned int t33_ae_frame(struct t32_aec *a, struct t33_ae_front *f, const u8 *zbufs,
					const u8 *hbufs)
{
	unsigned int n;

	t32_aec_bind(a);
	a->n = 0;
	if (!a->stop && a->mode == 1)
		t32_aec_long_alloc(0);
	n = a->n;
	t33_ae_load(a, f, zbufs + f->zidx * T33_AE_ZBUF_BYTES, hbufs + f->hidx * T33_AE_HBUF_BYTES);
	return n;
}

/*
 * tisp_ae_ev_alloc_calc as the T33 module has it: the T32 port
 * (t32_aev_ev_alloc_calc), except in the automatic split (no manual
 * selection, T + 2920 != 1) when the EV fits the integration time range:
 * the T32 gives the fraction of the whole lines to the digital gain and
 * leaves the analog gain at 1024; the T33 (vendor 0x3b4d4, default case)
 * gives it to the analog gain (up to its maximum ratio, the rest to the
 * digital gain) with the digital gain at 1.0.
 */
static inline void t33_aev_ev_alloc_calc(const u8 *T, u8 *B, u8 *R, u32 a2, u32 a3, u32 *o_a, u32 *o_b,
					 u32 *o_c, u32 k50, const struct t32_aev_ops *ops)
{
	u32 sh = t32_st16(T, 1736), sx = sh & 31, sel, m, amax, lo, hi, s0, f, s1, s2;
	u32 sA = t32_ld32(R, 608), sB = t32_ld32(R, 612), sC = t32_ld32(R, 620), sD = t32_ld32(R, 628);
	u32 sE = t32_ld32(R, 624), sF = t32_ld32(R, 636);
	t32_u64 xx;

	t32_aev_ev_alloc_calc(T, B, R, a2, a3, o_a, o_b, o_c, k50, ops);
	sel = (u32)R[1277] << 1 | (u32)R[1279] << 2 | R[1276];
	if (R[1284] == 1 || (sel - 1 < 7) || t32_st16(T, 2920) == 1)
		return;
	m = ops->mult2_32(sh, sB, sC);
	amax = sA << sx;
	if (a3 == 0 && !(ops->mult2_32(sh, m, amax) < a2))
		return;
	xx = ops->div64(sh, (t32_u64)a3 << 32 | a2, m);
	lo = (u32)xx;
	hi = (u32)(xx >> 32);
	if (hi == 0 && lo < amax)
		lo = amax;
	s0 = sE << sx;
	if (hi != 0 || s0 < lo)
		return;
	s0 = t32_aev_lsr(lo, sh) << sx;
	f = (u32)ops->div64(sh, lo, s0);
	s2 = ops->div32(sh, sD, sB);
	s1 = ops->div32(sh, sF, sC);
	if (f <= s2) {
		s2 = f;
		s1 = 1U << sx;
	} else if (f <= ops->mult2_32(sh, s2, s1)) {
		s1 = ops->div32(sh, f, s2);
	}
	*o_b = ops->mult2_32(sh, s2, sB);
	*o_c = ops->mult2_32(sh, s1, sC);
}

/*
 * tisp_ae_tune as the T33 module has it (T32 port:
 * t32_ae_tune in tx_isp_t32_aefu.h).  Same tables, first-frame EV,
 * stable band, convergence speed and EV allocation; the T33 differences
 * (disassembly of the T33 module, Ghidra project t33):
 *  - the target comes from tisp_ae_long_target at fix_point_mult2_64(sh,
 *    X, q), the EV scaled by the old target / mean ratio q (T32: at X; its
 *    mult2_64 result is unused there);
 *  - up: the step W (H196 * r (^2) + 1) is capped at 1.25 (d1 + 2^(sh+8))
 *    before the minimum step; down: W is floored at 0.75 (d1 - 2^(sh+8));
 *  - a statistics tag error of the frame (ae_cache + 0x6aa, tagerr) keeps
 *    the EV (no W * X);
 *  - the EV split: t33_aev_ev_alloc_calc;
 *  - no load check tail (T32 t32_aeu_result, B + 8574): the T33 function
 *    returns nothing; the stable / no-trigger exits return at once;
 *  - with T + 2854 == 1 the T33 takes another, history based step (R + 0x6ac
 *    .. 0x6c0); not ported: the cv2005 PRJ008 bins have 50 / 55 there
 *    (t33_ae_tune returns -1 without touching the blocks).
 * Returns 0, 1 for the early exits, -1 for the unported step.
 */
static inline int t33_ae_tune(u8 *T, u8 *B, u8 *R, u8 *eflag, u32 dcw, u32 ch, u32 total, const u8 *p,
			      int tagerr, const struct t32_aeu_ops *o)
{
	u32 sh = t32_st16(T, 1736), sx = sh & 31, sh2 = sh << 1;
	u32 k256 = t32_st16(T, 2920), tgt, q, r, up, nR, cur, i, sum, avg, o192, o188, o184;
	t32_u64 d1 = t32_aeu_shl(1024, sh), one = t32_aeu_shl(1, sh2), q4 = t32_aeu_shl(1, sh + 8);
	t32_u64 W, X, U, Lo, G, N, V, R64, H;
	u8 tab[120], y[30], h198[2], h196[2];

	if (t32_st16(T, 2854) == 1)
		return -1;
	X = o->ev->mult3_64(sh, t32_ld32(B, 7620), t32_ld32(B, 7628), t32_aeu_shl(t32_ld32(B, 7616), sh));
	t32_wr16(h198, 0, t32_st16(T, 2852));
	t32_wr16(h196, 0, t32_st16(T, 2854));
	t32_aeu_tables(T, tab, y);
	for (i = 0; i < 255; i++)
		t32_wr16(B, 11856 + 2 * i, t32_st16(B, 11858 + 2 * i));
	t32_wr16(B, 12366, total);
	if (t32_st16(B, 8566) == 0) {
		t32_u64 lim = sx == 31 ? 0xffffffff80000000ull : (t32_u64)1 << sx;

		X = t32_aeu_shl(t32_ld64(T, 368), 10);
		if (X < lim)
			X = lim;
		t32_wr16(B, 8566, 1);
		t32_wr16(B, 8568, 0);
		t32_wr16(B, 8570, t32_ae_long_target(X, tab, y, sh, o->ae));
	}
	q = o->ev->div32(sh, t32_st16(B, 8570) << sx, total << sx);
	if (t32_st16(B, 8568) == 1 && !(q < t32_st16(T, 2862) && t32_st16(T, 2864) < q))
		t32_wr16(B, 8568, 0);
	t32_wr16(B, 8572, total);
	cur = t32_st16(B, 8570);
	if (t32_st16(B, 8568) != 0 && R[1285] == 0)
		return 1;
	if (!(total < cur - t32_st16(T, 2866)) && !(cur + t32_st16(T, 2868) < total) && R[1285] == 0) {
		t32_wr16(B, 8568, 1);
		return 1;
	}
	if (R[1297] != 0) {
		if (R[1296] < 10)
			R[1296] = 10;
		else if (!(R[1296] < 201))
			R[1296] = 200;
		tgt = R[1296];
	} else {
		tgt = t32_ae_long_target(o->ev->mult2_64(sh, X, q), tab, y, sh, o->ae);
	}
	t32_wr16(B, 8570, tgt);
	up = total < tgt;
	r = o->ev->div32(sh, (up ? tgt - total : total - tgt) << sx, total << sx);
	t32_ae_calc_convergence_speed(p, h198, h196, t32_st16(T, k256 == 1 ? 1644 : 1648), sh, sh << 10, o->ae);
	R64 = t32_aeu_shl(r, sh);
	if (up) {
		H = t32_aeu_shl(t32_st16(h196, 0), sh);
		if (t32_st16(T, 2858) < r)
			W = o->ev->mult2_64(sh2, H, R64) + one;
		else
			W = o->ev->mult3_64(sh2, H, R64, R64) + one;
		if (d1 + q4 < W)
			W = d1 + q4;
	} else {
		H = t32_aeu_shl(t32_st16(h198, 0), sh);
		if (0U - t32_st16(T, 2860) < 0U - r) {
			t32_u64 d = o->ev->div64(sh2, o->ev->mult3_64(sh2, H, R64, R64), t32_aeu_shl(1024 - r, sh));

			W = one < d ? d1 - q4 : one - d;
		} else {
			t32_u64 e = o->ev->mult2_64(sh2, H, R64);

			W = one < e ? d1 - q4 : one - e;
		}
		if (W < d1 - q4)
			W = d1 - q4;
	}
	G = t32_aeu_shl(t32_st16(T, 2856), sh);
	U = G + d1;
	Lo = d1 - G;
	if (W < U && one < W)
		W = U;
	else if (Lo < W && W < one)
		W = Lo;
	N = tagerr ? X : o->ev->mult2_64(sh2, W, t32_aeu_shl(X, sh)) >> (sh & 63);
	nR = t32_ld32(R, 1540);
	for (i = 0; i < 29; i++)
		t32_wr32(R, 1380 + 4 * i, t32_ld32(R, 1384 + 4 * i));
	t32_wr32(R, 1496, R[1297]);
	for (i = 0, sum = 0; i < 30; i++)
		sum += t32_ld32(R, 1380 + 4 * i);
	for (i = 0; i + 1 < nR; i++)
		t32_wr32(R, 1300 + 4 * i, t32_ld32(R, 1304 + 4 * i));
	t32_wr32(R, 1300 + 4 * nR - 4, (u32)N);
	for (i = 0, avg = 0; i < nR; i++)
		avg += t32_ld32(R, 1300 + 4 * i) / nR;
	V = sum < 16 ? N : (t32_u64)avg;
	if (V < t32_aeu_shl(1, sh))
		V = t32_aeu_shl(1, sh);
	if (t32_ld32(T, 1740) == 1 && dcw == 1)
		t32_aev_list_alloc_calc(T, B, R, ch, (u32)V, (u32)(V >> 32), &o192, &o188, &o184, tgt, o->ev);
	else
		t33_aev_ev_alloc_calc(T, B, R, (u32)V, (u32)(V >> 32), &o192, &o188, &o184, tgt, o->ev);
	t32_wr32(R, 576, o192);
	t32_wr32(R, 580, o188);
	t32_wr32(R, 588, o184);
	*eflag = 1;
	return 0;
}

/* tisp_ae_algorithm with the T33 tune; tagerr: the frame's statistics tags differed */
static int t33_ae_tagerr;

static void t33_aec_cp_alg(u32 ch)
{
	struct t32_aec *a = t32_aec_cur;
	struct t32_ae_ctx c = { a->b.T, a->b.B, 0, 0, 0, 0 };
	u32 mean = 0, ratio[2] = { 0, 0 }, wsum[2] = { 0, 0 }, ww = 1, wh = 1;
	u8 rp[8];

	ch &= 0xff;
	t32_wr32(a->b.D, T32_AEC_D_VAL, t32_st16(a->b.B, T32_AEC_B_VAL));
	if (t32_ld32(a->b.T, T32_AEC_T_WMODE) == 1) {
		ww = t32_st16(a->b.T, T32_AEC_T_WIN_W);
		wh = t32_st16(a->b.T, T32_AEC_T_WIN_H);
	}
	t32_ae_weight_mean(&c, a->b.D, a->b.R, ch, &mean, ratio, ww, wh, wsum, &t32_aec_aeops);
	t32_wr32(rp, 0, ratio[0]);
	t32_wr32(rp, 4, ratio[1]);
	t33_ae_tune(a->b.T, a->b.B, a->b.R, &a->mode, a->dcw, ch, mean, rp, t33_ae_tagerr, &t32_aec_uops);
}

static const struct t32_ae_cp_ops t33_aec_cpops = {
	t32_aec_cp_rd, t33_aec_cp_alg, t32_aec_cp_trig, t32_aec_cp_long, t32_aec_cp_done,
};

/* event 1: tisp_normal_ae_calc_process with the T33 algorithm */
static inline unsigned int t33_ae_calc(struct t32_aec *a, const struct t33_ae_front *f, u32 zbad0, u32 hbad0)
{
	struct t32_ae_ctx c = { a->b.T, a->b.B, 0, 0, 0, 0 };
	struct t32_ae_cp_glob g = { &a->stop, &a->frame_cnt, 0, 0, a->trace };

	t32_aec_bind(a);
	t33_ae_tagerr = f->zbad != zbad0 || f->hbad != hbad0;
	a->n = t32_ae_calc_process(0, &c, a->b.R, t32_ld32(a->b.ctx, 8), &g, a->b.ctx + T32_AEC_CTX_DONE,
				   &t33_aec_cpops, a->w);
	return a->n;
}

/* the DG list word of the allocation's writes; false when it wrote none */
static inline bool t33_ae_dg_word(const struct t32_aec *a, unsigned int n, u32 *v)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		if (a->w[i].reg == T33_AE_DG_REG) {
			*v = a->w[i].val;
			return true;
		}
	return false;
}

#endif /* TX_ISP_T33_AEALG_H */
