/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AWB per-frame algorithm (vendor tisp_awb_* in the ISP core, 0x3c514 ..
 * 0x3ed34).  This header holds the AWB context the vendor keeps per channel
 * (data symbol e16b4, one pointer per channel) and the per-frame leaves ported
 * from it, one by one:
 *
 *   t32_awb_set_gain        tisp_awb_set_gain 0x3c514 (with tisp_awb_gain_reg
 *                           0x3c4e0 and the fixed point helper fix_point_mult2_32
 *                           0x1db58)
 *   t32_awb_sat_weight      tisp_awb_sat_weight 0x3d6f4
 *   t32_awb_long_par_update tisp_awb_long_par_update 0x3d158
 *   t32_awb_spec_calculate  tisp_awb_spec_calculate 0x3e5e4
 *
 * Every leaf is checked against the vendor flow (Matt Davis' recovered C, PR #15)
 * by its own oracle in tests/t32_oracle (t32_awbg_oracle for set_gain).
 *
 * The vendor addresses the context through raw offsets (s0 + 3064, s1 + 29792,
 * s2 + 4282, ...).  Here the same memory is described field by field.  Only the
 * fields the ported functions use are named, from how they are used; every field
 * carries its byte offset (decimal, as in the disassembly) and a compile time
 * check.  Unnamed bytes are padding.
 *
 * Context of channel ch (32 bytes, kmalloc'ed by tisp_awb_init):
 *   +0  par   the AWB tuning section copy (dd0a4[ch] + 3392, IQ bin section 2)
 *   +4  st    run state, 0x1d48c bytes, zeroed at init
 *   +8  mode  28 byte block of the mode table (tisp_awb_set_mode)
 *   +12 hw    4292 byte shadow of the values handed to the AWB hardware
 */
#ifndef TX_ISP_T32_AWB_H
#define TX_ISP_T32_AWB_H

#include "tx_isp_t32_stats.h"

#ifndef offsetof
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif

/* A field of type 'decl' at byte offset 'off' inside a union with a raw byte image */
#define T32_AWB_AT(off, decl) struct { u8 pad_##off[off]; decl; }

/* IQ section 2, the vendor's "par" (offsets in the section) */
struct t32_awb_par {
	union {
		u8 raw[6740];
		T32_AWB_AT(4, u32 sat_thr);		/* zones above this reference value are weighted */
		T32_AWB_AT(32, u32 ev_lo);		/* exposure range of the inner weights */
		T32_AWB_AT(36, u32 ev_hi);
		T32_AWB_AT(24, u32 wb_r);	/* measured red balance, scaled by cal_r */
		T32_AWB_AT(28, u32 wb_b);	/* measured blue balance, scaled by cal_b */
		T32_AWB_AT(44, u32 ct);		/* current colour temperature */
		T32_AWB_AT(48, u32 ct_last);		/* copy of ct made by long_par_update */
		T32_AWB_AT(52, u32 cal_r);
		T32_AWB_AT(56, u32 cal_b);
		T32_AWB_AT(60, u32 ct_axis_r[15]);	/* colour temperature grid: red axis (x of the r/g ratio) */
		T32_AWB_AT(120, u32 ct_axis_b[15]);	/* ... blue axis */
		T32_AWB_AT(2164, u32 ct_grid[15][15]);	/* 1e6 / ct at the grid points, row = blue */
		T32_AWB_AT(3448, u16 gw_en[4]);		/* gray world modes 0, 1 enable; [2]: refine pass */
		T32_AWB_AT(5714, u8 zone_flag[1024]);	/* per zone weight, 32 per row */
		T32_AWB_AT(180, u32 ev_set[3][4]);	/* four-value sets for low, mid, high exposure */
		T32_AWB_AT(228, u32 out_id);		/* weight id / weight outside [ev_lo, ev_hi) */
		T32_AWB_AT(232, u32 out_w);		/* (the weight is scaled by out_shift) */
		T32_AWB_AT(236, u32 in_id);		/* ... and inside */
		T32_AWB_AT(240, u32 in_w);
		T32_AWB_AT(5702, u8 sel_a);
		T32_AWB_AT(5703, u8 sel_b);
		T32_AWB_AT(324, u32 spec_ctl);		/* spec_calculate: register word of the current sample */
		T32_AWB_AT(344, u32 spec_slot_ctl[5]);	/* ... and of the five stored samples */
		T32_AWB_AT(3460, u16 spec_r[10]);	/* ratios: [0] current, [5..9] stored samples */
		T32_AWB_AT(3480, u16 spec_b[10]);
		T32_AWB_AT(5704, u8 spec_en);		/* sample enable; 1: use the raw values */
		T32_AWB_AT(5709, u8 spec_slot_en[5]);	/* stored sample used / valid */
		T32_AWB_AT(3064, u32 hot_off[2]);	/* gain offsets (r, b) for ct >= ct_hi */
		T32_AWB_AT(3072, u32 mid_off[2]);	/* ... for ct_lo < ct < ct_hi */
		T32_AWB_AT(3080, u32 cold_off[2]);	/* ... for ct <= ct_lo */
		T32_AWB_AT(3088, u32 ct_lo);
		T32_AWB_AT(3092, u32 ct_hi);
		T32_AWB_AT(5126, u16 ct_lim[20]);	/* ct_detect: 10 entries of (value, unused), those before the first zero count */
		T32_AWB_AT(3186, u16 zones_h);		/* statistics zone grid: rows ... */
		T32_AWB_AT(3190, u16 zones_w);		/* ... and columns (32 per row in the tables) */
		T32_AWB_AT(3306, u16 fp_shift);		/* fixed point shift of the calibration product */
		T32_AWB_AT(3308, u16 out_shift);	/* shift between the product and the gain */
		T32_AWB_AT(3438, u16 sat_mode);		/* 1: weight by the larger zone value, else by the mean */
	};
};

/* Run state (vendor ctx +4) */
struct t32_awb_state {
	union {
		u8 raw[0x1d48c];
		T32_AWB_AT(0, u32 zstat[2][2][1024]);	/* zone statistics [mode][r, b][row * 32 + col] */
		T32_AWB_AT(16384, u32 zw_raw[1024]);	/* ct_detect input: per zone value, normalised to zw_norm */
		T32_AWB_AT(99396, u32 zw_norm[1024]);
		T32_AWB_AT(79228, u32 cell_r[196]);	/* ct_detect: per cell of the 14 x 14 ct grid, sum of the zones' r, */
		T32_AWB_AT(80012, u32 cell_b[196]);	/* ... of their b */
		T32_AWB_AT(80796, u16 cell_n[196]);	/* ... and their count */
		T32_AWB_AT(37992, u16 lim_a[7]);	/* ct_detect: 3 + 4 entries, copied to its working set */
		T32_AWB_AT(38012, u16 lim_b[7]);
		T32_AWB_AT(28672, u32 out_id);		/* copies of the four par values, out_w / in_w scaled */
		T32_AWB_AT(28676, u32 out_w);
		T32_AWB_AT(28680, u32 in_id);
		T32_AWB_AT(28684, u32 in_w);
		T32_AWB_AT(28688, u8 sel_a);
		T32_AWB_AT(28689, u8 sel_b);
		T32_AWB_AT(28706, u8 spec_run);		/* sampling active */
		T32_AWB_AT(28707, u8 spec_cnt);		/* frames sampled, saturates at 2 */
		T32_AWB_AT(28708, u8 spec_flags[10]);	/* copy of par 5704..5713 */
		T32_AWB_AT(28718, u16 spec_r0);		/* calibrated current ratios */
		T32_AWB_AT(28738, u16 spec_b0);
		T32_AWB_AT(28728, u16 spec_rs[5]);	/* calibrated stored ratios */
		T32_AWB_AT(28748, u16 spec_bs[5]);
		T32_AWB_AT(119944, int spec_ring);	/* last stored sample slot */
		T32_AWB_AT(28764, u16 gw_tab[514]);	/* gray world: class by squared distance */
		T32_AWB_AT(87108, u32 gw_map[1024]);	/* gray world: zone class weights, 32 per row */
		T32_AWB_AT(28760, u32 last_ct);		/* ct of the last offset selection */
		T32_AWB_AT(38032, u32 vec[4]);		/* long_par_update: blended four-value set */
		T32_AWB_AT(38048, u32 pair[2]);		/* ... and (id, weight) */
		T32_AWB_AT(29792, u32 off_r);		/* selected gain offsets */
		T32_AWB_AT(29796, u32 off_b);
		T32_AWB_AT(107588, u32 ev);		/* exposure measure of the algorithm */
		T32_AWB_AT(107596, u8 update);		/* force the offset selection, cleared by set_gain */
		T32_AWB_AT(107597, u8 frz);		/* freeze: no register writes */
		T32_AWB_AT(107598, u8 mode);		/* 0 auto, 1..8 fixed ct table, 9 scaled, 10 api ct */
		T32_AWB_AT(107600, u32 man_ct);		/* ct of the fixed modes */
		T32_AWB_AT(107604, u16 man_r);		/* red / blue gain of the fixed modes */
		T32_AWB_AT(107606, u16 man_b);
		T32_AWB_AT(107620, u16 hist[4]);	/* last two (r, b) pairs, newest first */
		T32_AWB_AT(119916, u32 api_ct);		/* ct set through the API (mode 10) */
	};
};

/* Hardware shadow (vendor ctx +12) */
struct t32_awb_hw {
	union {
		u8 raw[4292];
		T32_AWB_AT(144, u16 inv_r4);	/* 4 * inv_r */
		T32_AWB_AT(146, u16 inv_b4);
		T32_AWB_AT(148, u16 ct_prev);	/* ct before this frame's mode handling */
		T32_AWB_AT(4266, u16 fixed);	/* 1: use the fixed values below */
		T32_AWB_AT(4268, u16 fixed_r);
		T32_AWB_AT(4270, u16 fixed_b);
		T32_AWB_AT(4272, u16 fixed_ct);
		T32_AWB_AT(4274, u16 inv_r);	/* 65536 / r */
		T32_AWB_AT(4276, u16 inv_b);
		T32_AWB_AT(4282, u16 gain_r);
		T32_AWB_AT(4284, u16 gain_b);
		T32_AWB_AT(4290, u16 ct_out);
	};
};

struct t32_awb {
	struct t32_awb_par *par;
	struct t32_awb_state *st;
	struct t32_awb_hw *hw;
};

#define T32_AWB_CHK(type, member, off) \
	_Static_assert(offsetof(type, member) == (off), #type "." #member " offset")
T32_AWB_CHK(struct t32_awb_par, sat_thr, 4);
T32_AWB_CHK(struct t32_awb_par, zones_h, 3186);
T32_AWB_CHK(struct t32_awb_par, zones_w, 3190);
T32_AWB_CHK(struct t32_awb_par, sat_mode, 3438);
T32_AWB_CHK(struct t32_awb_par, ev_lo, 32);
T32_AWB_CHK(struct t32_awb_par, ev_hi, 36);
T32_AWB_CHK(struct t32_awb_par, ct_last, 48);
T32_AWB_CHK(struct t32_awb_par, ev_set, 180);
T32_AWB_CHK(struct t32_awb_par, gw_en, 3448);
T32_AWB_CHK(struct t32_awb_par, zone_flag, 5714);
T32_AWB_CHK(struct t32_awb_state, gw_tab, 28764);
T32_AWB_CHK(struct t32_awb_state, gw_map, 87108);
T32_AWB_CHK(struct t32_awb_par, ct_axis_r, 60);
T32_AWB_CHK(struct t32_awb_par, ct_axis_b, 120);
T32_AWB_CHK(struct t32_awb_par, ct_grid, 2164);
T32_AWB_CHK(struct t32_awb_par, out_id, 228);
T32_AWB_CHK(struct t32_awb_par, out_w, 232);
T32_AWB_CHK(struct t32_awb_par, in_id, 236);
T32_AWB_CHK(struct t32_awb_par, in_w, 240);
T32_AWB_CHK(struct t32_awb_par, sel_a, 5702);
T32_AWB_CHK(struct t32_awb_par, sel_b, 5703);
T32_AWB_CHK(struct t32_awb_state, out_id, 28672);
T32_AWB_CHK(struct t32_awb_state, zstat, 0);
T32_AWB_CHK(struct t32_awb_state, zw_raw, 16384);
T32_AWB_CHK(struct t32_awb_state, zw_norm, 99396);
T32_AWB_CHK(struct t32_awb_state, lim_a, 37992);
T32_AWB_CHK(struct t32_awb_state, cell_r, 79228);
T32_AWB_CHK(struct t32_awb_state, cell_b, 80012);
T32_AWB_CHK(struct t32_awb_state, cell_n, 80796);
T32_AWB_CHK(struct t32_awb_state, lim_b, 38012);
T32_AWB_CHK(struct t32_awb_par, ct_lim, 5126);
T32_AWB_CHK(struct t32_awb_state, out_w, 28676);
T32_AWB_CHK(struct t32_awb_state, in_id, 28680);
T32_AWB_CHK(struct t32_awb_state, in_w, 28684);
T32_AWB_CHK(struct t32_awb_state, sel_a, 28688);
T32_AWB_CHK(struct t32_awb_state, sel_b, 28689);
T32_AWB_CHK(struct t32_awb_state, vec, 38032);
T32_AWB_CHK(struct t32_awb_state, pair, 38048);
T32_AWB_CHK(struct t32_awb_state, ev, 107588);
T32_AWB_CHK(struct t32_awb_par, spec_ctl, 324);
T32_AWB_CHK(struct t32_awb_par, spec_slot_ctl, 344);
T32_AWB_CHK(struct t32_awb_par, spec_r, 3460);
T32_AWB_CHK(struct t32_awb_par, spec_b, 3480);
T32_AWB_CHK(struct t32_awb_par, spec_en, 5704);
T32_AWB_CHK(struct t32_awb_par, spec_slot_en, 5709);
T32_AWB_CHK(struct t32_awb_state, spec_run, 28706);
T32_AWB_CHK(struct t32_awb_state, spec_cnt, 28707);
T32_AWB_CHK(struct t32_awb_state, spec_flags, 28708);
T32_AWB_CHK(struct t32_awb_state, spec_r0, 28718);
T32_AWB_CHK(struct t32_awb_state, spec_b0, 28738);
T32_AWB_CHK(struct t32_awb_state, spec_rs, 28728);
T32_AWB_CHK(struct t32_awb_state, spec_bs, 28748);
T32_AWB_CHK(struct t32_awb_state, spec_ring, 119944);
T32_AWB_CHK(struct t32_awb_par, wb_r, 24);
T32_AWB_CHK(struct t32_awb_par, wb_b, 28);
T32_AWB_CHK(struct t32_awb_par, ct, 44);
T32_AWB_CHK(struct t32_awb_par, cal_r, 52);
T32_AWB_CHK(struct t32_awb_par, cal_b, 56);
T32_AWB_CHK(struct t32_awb_par, hot_off, 3064);
T32_AWB_CHK(struct t32_awb_par, mid_off, 3072);
T32_AWB_CHK(struct t32_awb_par, cold_off, 3080);
T32_AWB_CHK(struct t32_awb_par, ct_lo, 3088);
T32_AWB_CHK(struct t32_awb_par, ct_hi, 3092);
T32_AWB_CHK(struct t32_awb_par, fp_shift, 3306);
T32_AWB_CHK(struct t32_awb_par, out_shift, 3308);
T32_AWB_CHK(struct t32_awb_state, last_ct, 28760);
T32_AWB_CHK(struct t32_awb_state, off_r, 29792);
T32_AWB_CHK(struct t32_awb_state, off_b, 29796);
T32_AWB_CHK(struct t32_awb_state, update, 107596);
T32_AWB_CHK(struct t32_awb_state, frz, 107597);
T32_AWB_CHK(struct t32_awb_state, mode, 107598);
T32_AWB_CHK(struct t32_awb_state, man_ct, 107600);
T32_AWB_CHK(struct t32_awb_state, man_r, 107604);
T32_AWB_CHK(struct t32_awb_state, man_b, 107606);
T32_AWB_CHK(struct t32_awb_state, hist, 107620);
T32_AWB_CHK(struct t32_awb_state, api_ct, 119916);
T32_AWB_CHK(struct t32_awb_hw, inv_r4, 144);
T32_AWB_CHK(struct t32_awb_hw, inv_b4, 146);
T32_AWB_CHK(struct t32_awb_hw, ct_prev, 148);
T32_AWB_CHK(struct t32_awb_hw, fixed, 4266);
T32_AWB_CHK(struct t32_awb_hw, fixed_r, 4268);
T32_AWB_CHK(struct t32_awb_hw, fixed_b, 4270);
T32_AWB_CHK(struct t32_awb_hw, fixed_ct, 4272);
T32_AWB_CHK(struct t32_awb_hw, inv_r, 4274);
T32_AWB_CHK(struct t32_awb_hw, inv_b, 4276);
T32_AWB_CHK(struct t32_awb_hw, gain_r, 4282);
T32_AWB_CHK(struct t32_awb_hw, gain_b, 4284);
T32_AWB_CHK(struct t32_awb_hw, ct_out, 4290);

#define T32_AWB_MAX_WRITES	24

/*
 * fix_point_mult2_32 (0x1db58): x * y in fixed point with s fraction bits,
 * assembled from the high and low parts of both factors so that no 64 bit
 * product is needed.  For s == 0 the low mask is all ones (the vendor computes it
 * as 0xffffffff >> (-s & 31)), which this reproduces.
 */
static inline u32 t32_awb_fixmul(u32 s, u32 x, u32 y)
{
	u32 sh = s & 31;
	u32 mask = 0xffffffffU >> ((0U - s) & 31);
	u32 xh = x >> sh, yh = y >> sh;
	u32 xl = x & mask, yl = y & mask;

	return (xh * yh << sh) + (xl * yh + xh * yl) + (xl * yl >> sh);
}

/* tisp_awb_gain_reg (0x3c4e0): a gain as register value, limited to 14 bits */
static inline u32 t32_awb_gain_reg(u32 gain)
{
	return (gain < 16384 ? gain : 16383) | 0x4000000U;
}

/* 65536 / v as the vendor stores it (division by zero: 0xffff) */
static inline u16 t32_awb_inverse(u32 v)
{
	return v ? (u16)(65536 / v) : 0xffff;
}

/*
 * tisp_awb_set_gain: turn the measured balance (par->wb_r / wb_b) into the red
 * and blue gain and program them.  Order of the vendor flow:
 *  1. gain = (wb << out_shift) * (cal << (fp_shift - 10)) in fixed point,
 *     rounded and shifted back by out_shift; remember it in the history and in
 *     the hardware shadow together with the inverses.
 *  2. the mode overrides gain / ct (fixed tables, scaled gains, API ct), then the
 *     fixed values of the hardware shadow override them again.
 *  3. when ct moved by 200 or more (or an update is forced) pick the gain offsets
 *     for the ct band.
 *  4. gain register value = 4 * gain + offset - 1024; unless frozen write it to
 *     the AWB block (0x6040 / 0x6044), trigger, and to the core (0x2a040 ..).
 * w gets the register writes in order, the return value is their number.
 */
static inline unsigned int t32_awb_set_gain(struct t32_awb *a, struct t32_stats_write *w)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	struct t32_awb_hw *hw = a->hw;
	u32 fp = par->fp_shift, osh = par->out_shift & 31;
	u32 round = 1U << ((par->out_shift - 1) & 31);
	u32 r, b, r_reg, b_reg, diff;
	u16 r16, b16;
	unsigned int n = 0;

	r = (t32_awb_fixmul(fp, par->wb_r << osh, par->cal_r << ((fp - 10) & 31)) + round) >> osh;
	b = (t32_awb_fixmul(fp, par->wb_b << osh, par->cal_b << ((fp - 10) & 31)) + round) >> osh;
	r16 = (u16)r;
	b16 = (u16)b;

	st->hist[2] = st->hist[0];
	st->hist[3] = st->hist[1];
	st->hist[0] = r16;
	st->hist[1] = b16;
	hw->gain_r = r16;
	hw->gain_b = b16;
	hw->inv_r = t32_awb_inverse(r16);
	hw->inv_r4 = (u16)(hw->inv_r << 2);
	hw->inv_b = t32_awb_inverse(b16);
	hw->inv_b4 = (u16)(hw->inv_b << 2);
	hw->ct_prev = (u16)par->ct;

	if (st->mode >= 1 && st->mode <= 8) {
		r = st->man_r;
		b = st->man_b;
		par->ct = st->man_ct;
	} else if (st->mode == 9) {
		r = (st->man_r + 32U) * r >> 6;
		b = (st->man_b + 32U) * b >> 6;
	} else if (st->mode == 10) {
		par->ct = st->api_ct;
	}
	if (hw->fixed == 1) {
		r = hw->fixed_r;
		b = hw->fixed_b;
		par->ct = hw->fixed_ct;
	}
	hw->ct_out = (u16)par->ct;

	diff = st->last_ct < par->ct ? par->ct - st->last_ct : st->last_ct - par->ct;
	if (diff >= 200 || st->update == 1) {
		st->last_ct = par->ct;
		if (par->ct >= par->ct_hi) {
			st->off_r = par->hot_off[0];
			st->off_b = par->hot_off[1];
		} else if (par->ct > par->ct_lo) {
			st->off_r = par->mid_off[0];
			st->off_b = par->mid_off[1];
		} else {
			st->off_r = par->cold_off[0];
			st->off_b = par->cold_off[1];
		}
	}

	r_reg = t32_awb_gain_reg(r * 4 + st->off_r - 1024);
	b_reg = t32_awb_gain_reg(b * 4 + st->off_b - 1024);
	if (!st->frz) {
		w[n].reg = 0x6040; w[n++].val = r_reg;
		w[n].reg = 0x6044; w[n++].val = b_reg;
		w[n].reg = 0x6010; w[n++].val = 1;	/* system_reg_set_awb_trig (2, ch) */
		w[n].reg = 0x2a040; w[n++].val = r_reg;
		w[n].reg = 0x2a044; w[n++].val = b_reg;
		w[n].reg = 0x2a010; w[n++].val = 1;
	}
	st->update = 0;
	return n;
}

/*
 * tisp_awb_sat_weight (0x3d6f4): per statistics zone weight, a 16.fp ratio.
 * Zones are numbered row * 32 + column (16 bit wrap as in the vendor).  For a zone
 * with limit[z] == 0, or with ref[z] not above par->sat_thr, the weight is 1.
 * Otherwise p = stat_a[z] * gain[0] >> fp_shift and q = stat_b[z] * gain[1] >>
 * fp_shift, s = min(p, q, limit[z]); s == 0 gives weight 1, else
 *   weight = (s << fp_shift) / d,  d = max(p, q, limit[z])        (sat_mode 1)
 *                                  d = (p + q + limit[z]) / 3     (otherwise)
 * A zero divisor gives 0xffffffff (the vendor's DIVU result as recovered; assumption
 * of the recovered C, the manual leaves it undefined).
 */
static inline u32 t32_awb_udiv(u32 a, u32 b)
{
	return b ? a / b : 0xffffffffU;
}

static inline void t32_awb_sat_weight(const struct t32_awb_par *par, const u32 *stat_a, const u32 *limit,
				      const u32 *stat_b, const u32 *ref, u32 *weight, const u32 gain[2])
{
	u32 sh = par->fp_shift & 31;
	u32 row, col;
	u16 row_first = 0, z;

	for (row = 0; row < par->zones_h; row++) {
		z = row_first;
		for (col = 0; col < par->zones_w; col++, z++) {
			u32 p, q, lim = limit[z], s, d;

			if (!lim || !(par->sat_thr < ref[z])) {
				weight[z] = 1;
				continue;
			}
			p = stat_a[z] * gain[0] >> sh;
			q = stat_b[z] * gain[1] >> sh;
			s = p < q ? p : q;
			if (s > lim)
				s = lim;
			if (!s) {
				weight[z] = 1;
				continue;
			}
			if (par->sat_mode == 1) {
				d = p < q ? q : p;
				if (d < lim)
					d = lim;
			} else {
				d = t32_awb_udiv(p + q + lim, 3);
			}
			weight[z] = t32_awb_udiv(s << sh, d);
		}
		row_first += 32;
	}
}

/*
 * tisp_awb_long_par_update (0x3d158): per frame preparation of the long algorithm's
 * parameters from the exposure measure st->ev.  The vendor also fills a 192 byte
 * table of 47 pointers into par / state / hw for tisp_awb_long_alogrithm; all of
 * them are constants (field addresses) except slot 16, which names the parameter
 * table the algorithm uses.  Ours addresses the fields directly, so only that
 * choice is returned (lp->tbl: byte offset in par, 1264 or 364).
 *
 * What it computes: copies of out_id / out_w / in_id / in_w into the state, the
 * ct copy, the flags sel_a / sel_b, the exposure blend of the three four-value
 * sets into st->vec and the (id, weight) pair st->pair.  The outer values apply below ev_hi, the inner ones
 * from ev_hi on; span = (ev_hi - ev_lo) / 3, is a third of it and the blend zone below ev_hi + span.
 * Several branches overwrite earlier results (copy of the sets, pair) exactly as the
 * vendor does; the order of the assignments is the vendor's.
 */
struct t32_awb_lp {
	u32 tbl;
};

static inline void t32_awb_copy4(u32 *dst, const u32 *src)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		dst[i] = src[i];
}

/* dst = (a * span + (b - a) * w) / span, per element */
static inline void t32_awb_blend4(u32 *dst, const u32 *a, const u32 *b, u32 w, u32 span)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		dst[i] = t32_awb_udiv(a[i] * span + (b[i] - a[i]) * w, span);
}

static inline void t32_awb_long_par_update(struct t32_awb *a, struct t32_awb_lp *lp)
{
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	const u32 *lowset = par->ev_set[0], *midset = par->ev_set[1], *highset = par->ev_set[2];
	u32 ev = st->ev, lo = par->ev_lo, hi = par->ev_hi;
	u32 span, edge, kind_out, kind_in;
	u32 *dst = st->vec, *pair = st->pair;

	par->ct_last = par->ct;
	st->out_id = par->out_id;
	st->out_w = par->out_w << (par->out_shift & 31);
	st->in_id = par->in_id;
	st->in_w = par->in_w << (par->out_shift & 31);
	kind_out = par->out_id & 0xffff;
	kind_in = par->in_id & 0xffff;

	if (ev < lo || ev < hi) {
		st->sel_a = 0;
		st->sel_b = par->sel_b;
		lp->tbl = 1264;
		pair[0] = kind_out;
		pair[1] = st->out_w;
	} else {
		st->sel_a = par->sel_a;
		st->sel_b = par->sel_b;
		lp->tbl = 364;
		pair[0] = kind_in;
		pair[1] = st->in_w;
	}
	span = (u32)((int)(hi - lo) / 3);
	edge = hi + span;

	if (kind_out != 1) {
		if (kind_out)
			return;
		t32_awb_copy4(dst, highset);
		pair[1] = 0x10000;
		if (kind_in != 1)
			return;
		if (ev < hi) {
			if (!(ev < edge))
				pair[1] = st->in_w;
		} else if (!(ev < edge)) {
			pair[1] = st->in_w;
		} else {
			pair[1] = t32_awb_udiv((span << 16) - (0x10000 - st->in_w) * (ev - hi), span);
		}
		return;
	}

	if (ev < lo) {
		t32_awb_copy4(dst, lowset);
	} else if (ev < lo + span) {
		t32_awb_blend4(dst, lowset, midset, ev - lo, span);
	} else if (ev < hi) {
		t32_awb_copy4(dst, midset);
	} else if (!(ev < edge)) {		/* beyond the blend zone */
		t32_awb_copy4(dst, highset);
		if (!kind_in) {
			pair[0] = kind_in;
			t32_awb_copy4(dst, midset);
			pair[1] = 0x10000;
		}
		return;
	} else {				/* hi <= ev < edge */
		t32_awb_blend4(dst, midset, highset, ev - hi, span);
		pair[1] = t32_awb_udiv(span * st->out_w + (st->in_w - st->out_w) * (ev - hi), span);
		if (!kind_in) {
			pair[0] = kind_out;
			t32_awb_copy4(dst, midset);
			pair[1] = t32_awb_udiv(st->out_w * span + (0x10000 - st->out_w) * (ev - hi), span);
		}
		return;
	}

	/* ev below hi (or the zones overlap): the inner weight when the inner id is 0 */
	if (kind_in)
		return;
	if (!(ev < edge)) {
		pair[0] = kind_in;
		t32_awb_copy4(dst, midset);
		pair[1] = 0x10000;
	} else if (!(ev < hi)) {
		pair[0] = kind_out;
		t32_awb_copy4(dst, midset);
		pair[1] = t32_awb_udiv(st->out_w * span + (0x10000 - st->out_w) * (ev - hi), span);
	}
}

/*
 * tisp_awb_spec_calculate (0x3e5e4): the "spec" sample of the AWB hardware.  Reads
 * three statistics registers (0x16190 a, 0x16194 b, 0x16198 c), forms the ratios
 * r = (a << 8) / b and bb = (c << 8) / b (0 when b is 0), keeps up to five samples in a
 * ring (slots 5..9 of par->spec_r / spec_b, valid byte par->spec_slot_en) and writes
 * the sample registers.  Per sample, enable byte 1 writes the raw values, any other
 * non-zero value stores the calibrated values (fixed point, times cal_r / cal_b) in the
 * state.  The slots 6/7 and 8/9 write the same value registers (0x1609c / 0x160b0 and
 * 0x160a0 / 0x160b4) from the same par words, as the vendor does.  The vendor's shadow
 * register variant (data d03a0 != 0) is not ported (always direct writes).
 * rd reads a statistics register.  w gets the register writes in order.
 */
static inline u32 t32_awb_u32at(const u8 *raw, u32 off)
{
	return raw[off] | (u32)raw[off + 1] << 8 | (u32)raw[off + 2] << 16 | (u32)raw[off + 3] << 24;
}

static inline void t32_awb_put16at(u8 *raw, u32 off, u32 v)
{
	raw[off] = (u8)v;
	raw[off + 1] = (u8)(v >> 8);
}

static inline u16 t32_awb_calibrate(u32 ratio, u32 cal)
{
	return (u16)((t32_awb_fixmul(10, ratio << 2, cal) + 2) >> 2);
}

static inline unsigned int t32_awb_spec_calculate(struct t32_awb *a, u32 (*rd)(void *, u32), void *rdp,
						  struct t32_stats_write *w)
{
	static const u32 reg_r[5] = { 0x16098, 0x1609c, 0x1609c, 0x160a0, 0x160a0 };
	static const u32 reg_b[5] = { 0x160ac, 0x160b0, 0x160b0, 0x160b4, 0x160b4 };
	static const u32 reg_ctl[5] = { 0x160cc, 0x160d0, 0x160d4, 0x160d8, 0x160dc };
	static const u32 off_r[5] = { 3468, 3472, 3472, 3476, 3476 };	/* u32 reads of par */
	static const u32 off_b[5] = { 3488, 3492, 3492, 3496, 3496 };
	struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	u32 s5 = 0, s6 = 0, v_a, v_b, v_c, ctl;
	unsigned int n = 0, k;

	memcpy(st->spec_flags, par->raw + 5704, 10);
	if (st->spec_run == 1 && st->spec_cnt < 2)
		st->spec_cnt++;

	if (par->spec_en && st->spec_run == 1 && st->spec_cnt) {
		v_a = rd(rdp, 0x16190);
		v_b = rd(rdp, 0x16194);
		v_c = rd(rdp, 0x16198);
		if (v_b) {
			s5 = (v_a << 8) / v_b;
			s6 = (v_c << 8) / v_b;
		}
		par->spec_r[0] = (u16)s5;
		par->spec_b[0] = (u16)s6;
		if (par->spec_en == 1) {
			w[n].reg = 0x16090; w[n++].val = (u32)par->spec_r[1] << 16 | (s5 & 0xffff);
			w[n].reg = 0x160a4; w[n++].val = t32_awb_u32at(par->raw, 3480);
			ctl = (u32)st->spec_flags[0] << 16 | par->spec_ctl;
		} else {
			st->spec_r0 = t32_awb_calibrate(s5, par->cal_r);
			st->spec_b0 = t32_awb_calibrate(s6, par->cal_b);
			ctl = par->spec_ctl;
		}
	} else {
		if (par->spec_en)
			st->spec_flags[0] = 0;
		ctl = par->spec_ctl;
	}
	w[n].reg = 0x160b8; w[n++].val = ctl;

	for (k = 0; k < 5; k++)
		if (par->spec_slot_en[k])
			break;
	if (k < 5) {		/* a stored sample is in use: take a new one into the ring */
		u32 slot, i;
		int ring = st->spec_ring;

		v_a = rd(rdp, 0x16190);
		v_b = rd(rdp, 0x16194);
		v_c = rd(rdp, 0x16198);
		if (v_b) {
			s5 = (v_a << 8) / v_b;
			s6 = (v_c << 8) / v_b;
		}
		slot = 0;
		for (i = 1; i <= 5; i++) {
			slot = (u32)((ring + (int)i) % 5) + 5;
			if (par->raw[5704 + slot])
				break;
		}
		if (i > 5) {
			for (slot = 5; slot != 10 && par->raw[5704 + slot] != 1; slot++)
				;
			if (slot == 10)
				slot = (u32)((ring + 1) % 5) + 5;
		}
		st->spec_ring = (int)slot;
		par->raw[5704 + slot] = 1;
		t32_awb_put16at(par->raw, 3460 + 2 * slot, s5);
		t32_awb_put16at(par->raw, 3480 + 2 * slot, s6);
	}

	for (k = 0; k < 5; k++) {
		u8 en = par->spec_slot_en[k];

		ctl = t32_awb_u32at(par->raw, 344 + 4 * k);
		if (en == 1) {
			w[n].reg = reg_r[k]; w[n++].val = t32_awb_u32at(par->raw, off_r[k]);
			w[n].reg = reg_b[k]; w[n++].val = t32_awb_u32at(par->raw, off_b[k]);
			ctl |= (u32)st->spec_flags[5 + k] << 16;
		} else if (en) {
			st->spec_rs[k] = t32_awb_calibrate(par->spec_r[5 + k], par->cal_r);
			st->spec_bs[k] = t32_awb_calibrate(par->spec_b[5 + k], par->cal_b);
		}
		w[n].reg = reg_ctl[k]; w[n++].val = ctl;
	}
	return n;
}

#endif /* TX_ISP_T32_AWB_H */
