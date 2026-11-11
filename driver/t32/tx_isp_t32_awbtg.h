/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AWB inputs from the AE (openimp #234): tisp_awb_tgain_update (0x3f12c)
 * with its two callees on the AWB context, tisp_awb_set_regional_threshold
 * (0x3bbb0) and tisp_awb_set_lum_th_freq (0x3be64), and tisp_awb_ev_update
 * (0x3f188, t32_awb_ev_update in tx_isp_t32_awb.h).
 *
 * The vendor runs them from its event thread: AE event 4 (the long total
 * gain, log2 Q16) -> tisp_long_tgain_update -> tisp_awb_tgain_update (on a
 * gain change), AE event 6 (the long EV) -> tisp_long_ev_update ->
 * tisp_awb_ev_update (on an EV change).  Both callees interpolate their
 * tuning rows (tisp_simple_intp_uint16) at the stored gain st +107592
 * (index = gain >> 16, fraction = gain & 0xffff), keep the results in the
 * par section and the state, and write the AWB statistics thresholds:
 *   regional threshold: par +3252 .. +3266 = rows +4588 + 22 k (k < 8),
 *     st +37992 / +37994 / +37996 / +38012 / +38014 / +38016 = rows
 *     +5242 / +5264 / +5286 / +5462 / +5484 / +5506, st +37998 .. +38004 and
 *     +38018 .. +38024 = par +3500 .. +3514; unless frozen (st +107597):
 *     0x16064 .. 0x16070 = the four u16 pairs of par +3252, trigger 0x16010.
 *   lum th freq: par +3268 / +3270 / +3272 / +3282 .. +3288 / +3294 .. +3300
 *     = rows +4764 + 22 k (k < 11); the luma limits can be overridden by the
 *     e16c4 words (word 0 set: word 1 lowers the low limit, word 3 raises
 *     the high one); words 1 .. 3 get the limits back; 0x160f0, 0x16074,
 *     0x160ec, 0x160f8, trigger 0x16010 (no freeze check).
 * Direct register mode only (as the other AWB ports: the shadow variant,
 * data d03a0 != 0, is not ported).  Checked bit-exact against Matt Davis'
 * recovered C (PR #15 @ 2e2e06bc) by tests/t32_oracle (t32_awbtg_oracle).
 */
#ifndef TX_ISP_T32_AWBTG_H
#define TX_ISP_T32_AWBTG_H

#include "tx_isp_t32_awbinit.h"

#define T32_AWBTG_MAX_WRITES	10U

/* the four u32 words of the vendor e16c4 + 16 * ch (word 0: the completion flag, also the override switch) */
struct t32_awb_lumov {
	u32 w[4];
};

/* tisp_awb_set_regional_threshold */
static inline unsigned int t32_awb_set_regional_threshold(struct t32_awb *a, struct t32_stats_write *w)
{
	static const u16 st_rows[6][2] = {
		{ 37992, 5242 }, { 37994, 5264 }, { 37996, 5286 }, { 38012, 5462 }, { 38014, 5484 }, { 38016, 5506 },
	};
	static const u16 st_cp[8] = { 37998, 38000, 38002, 38004, 38018, 38020, 38022, 38024 };
	u8 *par = a->par->raw, *st = a->st->raw;
	u32 g = a->st->tgain, i = g >> 16, f = g & 0xffff, k;
	unsigned int n = 0;

	for (k = 0; k < 8; k++)
		t32_wr16(par, 3252 + 2 * k, t32_intp_u16(i, f, par + 4588 + 22 * k));
	for (k = 0; k < 6; k++)
		t32_wr16(st, st_rows[k][0], t32_intp_u16(i, f, par + st_rows[k][1]));
	for (k = 0; k < 8; k++)
		t32_wr16(st, st_cp[k], t32_st16(par, 3500 + 2 * k));
	if (a->st->frz)
		return 0;
	for (k = 0; k < 4; k++)
		t32_awbi_w(w, &n, 0x16064 + 4 * k, t32_ld32(par, 3252 + 4 * k));
	t32_awbi_w(w, &n, T32_AWB_TRIG, 1);
	return n;
}

/* tisp_awb_set_lum_th_freq */
static inline unsigned int t32_awb_set_lum_th_freq(struct t32_awb *a, struct t32_awb_lumov *ov,
						   struct t32_stats_write *w)
{
	static const u16 dst[11] = { 3268, 3270, 3272, 3282, 3284, 3286, 3288, 3294, 3296, 3298, 3300 };
	u8 *par = a->par->raw;
	u32 g = a->st->tgain, i = g >> 16, f = g & 0xffff, k, lo, mid, hi, f0, f3;
	unsigned int n = 0;

	for (k = 0; k < 11; k++)
		t32_wr16(par, dst[k], t32_intp_u16(i, f, par + 4764 + 22 * k));
	lo = t32_st16(par, 3268);
	mid = t32_st16(par, 3270);
	hi = t32_st16(par, 3272);
	f0 = t32_st16(par, 3282);
	f3 = t32_st16(par, 3288);
	if (ov->w[0]) {
		if (ov->w[1] && ov->w[1] < mid) {
			lo = ov->w[1];
			f0 = 0;
		}
		if (ov->w[3] && mid < ov->w[3]) {
			hi = ov->w[3];
			f3 = 0;
		}
	}
	ov->w[1] = lo;
	ov->w[3] = hi;
	ov->w[2] = mid;
	t32_awbi_w(w, &n, 0x160f0, (f0 & 15) | (t32_st16(par, 3284) & 15) << 8 | (t32_st16(par, 3286) & 15) << 16 |
		   (f3 & 15) << 24);
	t32_awbi_w(w, &n, 0x16074, (u32)t32_st16(par, 3274) << 24 | mid << 8 | lo | hi << 16);
	t32_awbi_w(w, &n, 0x160ec, (t32_st16(par, 3280) & 3) << 16 | (t32_st16(par, 3278) & 1));
	t32_awbi_w(w, &n, 0x160f8, (t32_st16(par, 3294) & 15) | (t32_st16(par, 3296) & 15) << 8 |
		   (t32_st16(par, 3298) & 15) << 16 | (t32_st16(par, 3300) & 15) << 24);
	t32_awbi_w(w, &n, T32_AWB_TRIG, 1);
	return n;
}

/*
 * tisp_awb_tgain_update(ch, gain): st +107592 = gain (the low word of the
 * 64-bit argument), then the regional threshold and the lum th freq.  w gets
 * at most T32_AWBTG_MAX_WRITES writes; returns their number.
 */
static inline unsigned int t32_awb_tgain_apply(struct t32_awb *a, struct t32_awb_lumov *ov, u32 gain,
					       struct t32_stats_write *w)
{
	unsigned int n;

	a->st->tgain = gain;
	n = t32_awb_set_regional_threshold(a, w);
	return n + t32_awb_set_lum_th_freq(a, ov, w + n);
}

#endif /* TX_ISP_T32_AWBTG_H */
