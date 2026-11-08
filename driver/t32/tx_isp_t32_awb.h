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
		u8 raw[3310];
		T32_AWB_AT(24, u32 wb_r);	/* measured red balance, scaled by cal_r */
		T32_AWB_AT(28, u32 wb_b);	/* measured blue balance, scaled by cal_b */
		T32_AWB_AT(44, u32 ct);		/* current colour temperature */
		T32_AWB_AT(52, u32 cal_r);
		T32_AWB_AT(56, u32 cal_b);
		T32_AWB_AT(3064, u32 hot_off[2]);	/* gain offsets (r, b) for ct >= ct_hi */
		T32_AWB_AT(3072, u32 mid_off[2]);	/* ... for ct_lo < ct < ct_hi */
		T32_AWB_AT(3080, u32 cold_off[2]);	/* ... for ct <= ct_lo */
		T32_AWB_AT(3088, u32 ct_lo);
		T32_AWB_AT(3092, u32 ct_hi);
		T32_AWB_AT(3306, u16 fp_shift);		/* fixed point shift of the calibration product */
		T32_AWB_AT(3308, u16 out_shift);	/* shift between the product and the gain */
	};
};

/* Run state (vendor ctx +4) */
struct t32_awb_state {
	union {
		u8 raw[0x1d48c];
		T32_AWB_AT(28760, u32 last_ct);		/* ct of the last offset selection */
		T32_AWB_AT(29792, u32 off_r);		/* selected gain offsets */
		T32_AWB_AT(29796, u32 off_b);
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

#define T32_AWB_MAX_WRITES	8

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

#endif /* TX_ISP_T32_AWB_H */
