/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 black level / green imbalance (GIB, block 0x3000) from the IQ bin at
 * AddSensor (S4 step 6): tisp_gib_init -> tisp_gib_interp_by_again (gain
 * 0x400), tisp_gib_slope_update (tisp_gib_calc_slope x5), tisp_gib_write_reg,
 * tisp_gib_interp_by_again_s, tisp_gib_write_blc_s_reg and the block id.
 *
 * IQ bin section 8 (548 bytes).  The vendor keeps the interpolated values
 * in a working block (here "cp", 0x84 bytes) and a table of 16 pointers to
 * them, which depends on the Bayer pattern (t32_gib_ptr_tab).  The vendor's
 * initial state (all-zero working block, gain words 0x400, no copy-back
 * mode, normal black level path) is reproduced.  The interpolated black
 * levels are also copied back into the section (the vendor does so when
 * section byte 0x223 is 0), so pass a writable copy.
 *
 * Per-frame paths: tisp_gib_dn_params_refresh (re-interpolate at the stored
 * gain, optional copy-back) and tisp_gib_wake_up (write the working block
 * and the normal black level path).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_gib_oracle, t32_gibf_oracle, t32_gibx_oracle).
 */
#ifndef TX_ISP_T32_GIB_H
#define TX_ISP_T32_GIB_H

#include "tx_isp_t32_tp.h"

#define T32_SEC_GIB		8
#define T32_GIB_SEC_BYTES	548U
#define T32_GIB_VALUES		0x84U
#define T32_GIB_ID		0x0134da11U
#define T32_GIB_MAX_WRITES	128

/*
 * The vendor's pointer table: the working block offset each of the 16 black
 * level words reads, chosen by the Bayer pattern index (word 2 of the init
 * block & 0x1f) through a jump table.  Rows: 0 = patterns 0 and 4..11 and
 * >= 20 (the default, RGGB first), then patterns 1, 2, 3, 12, 13, 14, 16, 17,
 * 18, 19.  Pattern 15 leaves two entries unset in the vendor (a null pointer
 * it would read) and is refused.
 */
static const u8 t32_gib_ptr_tab[11][16] = {
	{ 0x40, 0x42, 0x40, 0x42, 0x44, 0x46, 0x44, 0x46, 0x4c, 0x4e, 0x4c, 0x4e, 0x50, 0x52, 0x50, 0x52 },
	{ 0x46, 0x44, 0x46, 0x44, 0x42, 0x40, 0x42, 0x40, 0x52, 0x50, 0x52, 0x50, 0x4e, 0x4c, 0x4e, 0x4c },
	{ 0x42, 0x40, 0x42, 0x40, 0x46, 0x44, 0x46, 0x44, 0x4e, 0x4c, 0x4e, 0x4c, 0x52, 0x50, 0x52, 0x50 },
	{ 0x44, 0x46, 0x44, 0x46, 0x40, 0x42, 0x40, 0x42, 0x50, 0x52, 0x50, 0x52, 0x4c, 0x4e, 0x4c, 0x4e },
	{ 0x40, 0x42, 0x46, 0x42, 0x46, 0x48, 0x42, 0x48, 0x4c, 0x42, 0x52, 0x4e, 0x52, 0x48, 0x4e, 0x54 },
	{ 0x46, 0x42, 0x40, 0x42, 0x42, 0x48, 0x42, 0x48, 0x52, 0x4e, 0x4c, 0x4e, 0x4e, 0x54, 0x4e, 0x54 },
	{ 0x42, 0x40, 0x42, 0x46, 0x48, 0x42, 0x48, 0x42, 0x4e, 0x4c, 0x4e, 0x52, 0x54, 0x4e, 0x54, 0x4e },
	{ 0x42, 0x48, 0x42, 0x48, 0x40, 0x42, 0x46, 0x42, 0x4e, 0x54, 0x4e, 0x54, 0x4c, 0x4e, 0x52, 0x4e },
	{ 0x42, 0x48, 0x42, 0x48, 0x46, 0x42, 0x40, 0x42, 0x4e, 0x54, 0x4e, 0x54, 0x52, 0x4e, 0x4c, 0x4e },
	{ 0x48, 0x42, 0x48, 0x42, 0x42, 0x40, 0x42, 0x46, 0x54, 0x4e, 0x54, 0x4e, 0x4e, 0x4c, 0x4e, 0x52 },
	{ 0x48, 0x42, 0x48, 0x42, 0x42, 0x46, 0x42, 0x40, 0x54, 0x4e, 0x54, 0x4e, 0x4e, 0x52, 0x4e, 0x4c },
};

/* row of t32_gib_ptr_tab for a Bayer pattern index, -1 when not built */
static inline int t32_gib_ptr_row(u32 bayer)
{
	bayer &= 0x1f;
	if (bayer >= 1 && bayer <= 3)
		return (int)bayer;
	if (bayer >= 12 && bayer <= 19)
		return bayer == 15 ? -1 : (int)bayer - (bayer < 15 ? 8 : 9);
	return 0;
}

#define T32_GIB_GP(i)	t32_tp_le16(cp + pt[i])

/* tisp_round_int64(x, 0, 1) */
static inline u32 t32_gib_round1(u32 x)
{
	return (x >> 1) + (x & 1);
}

/*
 * The vendor divides without a zero check; on the MIPS core a zero divisor gives
 * the quotient 1 for a negative and -1 for a non-negative dividend.  C would trap.
 */
static inline u32 t32_gib_div(int num, int den)
{
	if (!den)
		return num < 0 ? 1 : ~0U;
	return (u32)(num / den);
}

/* tisp_gib_calc_slope: slopes (x 256) between five knots, 0 when the knots are not ordered */
static inline void t32_gib_slope(const u8 *xs, const u8 *ys, u8 *out)
{
	u32 x0 = t32_tp_le16(xs), x1 = t32_tp_le16(xs + 2), x2 = t32_tp_le16(xs + 4), x3 = t32_tp_le16(xs + 6);
	u32 x4 = t32_tp_le16(xs + 8), y0 = t32_tp_le16(ys), y1 = t32_tp_le16(ys + 2), y2 = t32_tp_le16(ys + 4);
	u32 y3 = t32_tp_le16(ys + 6), y4 = t32_tp_le16(ys + 8);
	u32 s0, s1, s2, s3;

	t32_tp_put16(out, 0);
	t32_tp_put16(out + 2, 0);
	t32_tp_put16(out + 4, 0);
	t32_tp_put16(out + 6, 0);
	if (!(x0 < x1) && !(x1 < x2) && !(x2 < x3) && x4 <= x3)
		return;
	if (y1 < y0 || y2 < y1 || y3 < y2 || y3 > y4)
		return;
	s0 = t32_gib_div((int)((y1 - y0) * 0x100), (int)(x1 - x0));
	s1 = t32_gib_div((int)((y2 - y1) * 0x100), (int)(x2 - x1));
	s2 = t32_gib_div((int)((y3 - y2) * 0x100), (int)(x3 - x2));
	s3 = t32_gib_div((int)((y4 - y3) * 0x100), (int)(x4 - x3));
	t32_tp_put16(out, (s0 & 0xffff) > 0xfff ? 0xfff : s0);
	t32_tp_put16(out + 2, (s1 & 0xffff) > 0xfff ? 0xfff : s1);
	t32_tp_put16(out + 4, (s2 & 0xffff) > 0xfff ? 0xfff : s2);
	t32_tp_put16(out + 6, (s3 & 0xffff) > 0xfff ? 0xfff : s3);
}

/* the largest of five black levels and the matching 0x4a / 0x56 word */
static inline u32 t32_gib_gain(const u8 *p)
{
	u32 v = t32_tp_le16(p), k;

	for (k = 1; k < 5; k++)
		if (v <= t32_tp_le16(p + 2 * k))
			v = t32_tp_le16(p + 2 * k);
	return (v >> 11) ? 0x1fff : t32_gib_round1(0x2000000U / (0x1000 - v));
}

/* The expressions below keep the operator precedence of the transcribed code. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wparentheses"
#endif

/*
 * tisp_gib_write_reg(ch, mode): the black level words, then the section
 * words.  mode 1 stops after 0x3068 with the 0x3010 trigger (gain update
 * only).  ip10: the WDR flag (gain block byte 16, tisp_gib_wdr_en); when set
 * the normal black level path (0x318c..0x3198, 0x3188, 0x319c) is skipped.
 */
static inline void t32_gib_write_reg_mode(const u8 *sec, const u8 *cp, const u8 *pt, u32 mode, int ip10,
					  struct t32_front_write *w, unsigned int *np)
{
	unsigned int n = *np;

	T32_FWR(w, n, 0x3044, (T32_GIB_GP(1) & 0xfff) << 0x10 | T32_GIB_GP(0) & 0xfff);
	T32_FWR(w, n, 0x3048, (T32_GIB_GP(3) & 0xfff) << 0x10 | T32_GIB_GP(2) & 0xfff);
	T32_FWR(w, n, 0x3050,t32_tp_le16(cp + 0x4a) & 0x1fff);
	T32_FWR(w, n, 0x317c, (T32_GIB_GP(5) & 0xfff) << 0x10 | T32_GIB_GP(4) & 0xfff);
	T32_FWR(w, n, 0x3180, (T32_GIB_GP(7) & 0xfff) << 0x10 | T32_GIB_GP(6) & 0xfff);
	if (!ip10) {
		T32_FWR(w, n, 0x318c, (T32_GIB_GP(1) & 0xfff) << 0x10 | T32_GIB_GP(0) & 0xfff);
		T32_FWR(w, n, 0x3190, (T32_GIB_GP(3) & 0xfff) << 0x10 | T32_GIB_GP(2) & 0xfff);
		T32_FWR(w, n, 0x3200,t32_tp_le16(cp + 0x4a) & 0x1fff);
		T32_FWR(w, n, 0x3194, (T32_GIB_GP(5) & 0xfff) << 0x10 | T32_GIB_GP(4) & 0xfff);
		T32_FWR(w, n, 0x3198, (T32_GIB_GP(7) & 0xfff) << 0x10 | T32_GIB_GP(6) & 0xfff);
	}
	T32_FWR(w, n, 0x3068, cp[0x80]);
	if (mode == 1) {
		T32_FWR(w, n, 0x3010, 3);
		*np = n;
		return;
	}
	T32_FWR(w, n, 0x3040,sec[0x202] & 1);
	T32_FWR(w, n, 0x304c,sec[0x203] & 1);
	if (!ip10) {
		T32_FWR(w, n, 0x3188,sec[0x202] & 1);
		T32_FWR(w, n, 0x319c,sec[0x203] & 1);
	}
	T32_FWR(w, n, 0x3064,sec[0x206] & 1);
	T32_FWR(w, n, 0x306c, (t32_tp_le16(sec + 10) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 8) & 0x3ff);
	T32_FWR(w, n, 0x3070,t32_tp_le16(sec + 0xc) & 0x3ff);
	T32_FWR(w, n, 0x3074,t32_tp_le16(sec + 0xe) & 0x7ff);
	T32_FWR(w, n, 0x3078, (t32_tp_le16(sec + 0x12) & 0x7ff) << 0x10 |
			t32_tp_le16(sec + 0x10) & 0x7ff);
	T32_FWR(w, n, 0x307c,t32_tp_le16(sec + 0x14) & 0x7ff);
	T32_FWR(w, n, 0x3080,sec[0x213] & 3);
	T32_FWR(w, n, 0x3084,sec[0x215] & 1);
	T32_FWR(w, n, 0x3088,sec[0x1ff] & 0x1f);
	T32_FWR(w, n, 0x308c,sec[0x200] & 0x1f);
	T32_FWR(w, n, 0x3090,sec[0x201] & 0x1f);
	T32_FWR(w, n, 0x3094,sec[0x216] & 1);
	T32_FWR(w, n, 0x3098, (t32_tp_le16(sec + 0x116) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 0x114) & 0xfff);
	T32_FWR(w, n, 0x309c, (t32_tp_le16(sec + 0x11a) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 0x118) & 0xfff);
	T32_FWR(w, n, 0x30a0,t32_tp_le16(sec + 0x11c) & 0xfff);
	T32_FWR(w, n, 0x30a4, (t32_tp_le16(sec + 0x120) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x11e) & 0x3ff);
	T32_FWR(w, n, 0x30a8, (t32_tp_le16(sec + 0x124) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x122) & 0x3ff);
	T32_FWR(w, n, 0x30ac,t32_tp_le16(sec + 0x126) & 0x3ff);
	T32_FWR(w, n, 0x30b0, (t32_tp_le16(cp + 0x5a) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 0x58) & 0xfff);
	T32_FWR(w, n, 0x30b4, (t32_tp_le16(cp + 0x5e) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 0x5c) & 0xfff);
	T32_FWR(w, n, 0x30b8,sec[0x217] & 1);
	T32_FWR(w, n, 0x30bc, (t32_tp_le16(sec + 0x12a) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 0x128) & 0xfff);
	T32_FWR(w, n, 0x30c0, (t32_tp_le16(sec + 0x12e) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 300) & 0xfff);
	T32_FWR(w, n, 0x30c4,t32_tp_le16(sec + 0x130) & 0xfff);
	T32_FWR(w, n, 0x30c8, (t32_tp_le16(sec + 0x134) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x132) & 0x3ff);
	T32_FWR(w, n, 0x30cc, (t32_tp_le16(sec + 0x138) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x136) & 0x3ff);
	T32_FWR(w, n, 0x30d0,t32_tp_le16(sec + 0x13a) & 0x3ff);
	T32_FWR(w, n, 0x30d4, (t32_tp_le16(cp + 0x62) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 0x60) & 0xfff);
	T32_FWR(w, n, 0x30d8, (t32_tp_le16(cp + 0x66) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 100) & 0xfff);
	T32_FWR(w, n, 0x30dc,sec[0x218] & 1);
	T32_FWR(w, n, 0x30e0,sec[0x219] & 3);
	T32_FWR(w, n, 0x30e4, (t32_tp_le16(sec + 0x13e) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 0x13c) & 0xfff);
	T32_FWR(w, n, 0x30e8, (t32_tp_le16(sec + 0x142) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 0x140) & 0xfff);
	T32_FWR(w, n, 0x30ec,t32_tp_le16(sec + 0x144) & 0xfff);
	T32_FWR(w, n, 0x30f0, (t32_tp_le16(sec + 0x148) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x146) & 0x3ff);
	T32_FWR(w, n, 0x30f4, (t32_tp_le16(sec + 0x14c) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x14a) & 0x3ff);
	T32_FWR(w, n, 0x30f8,t32_tp_le16(sec + 0x14e) & 0x3ff);
	T32_FWR(w, n, 0x30fc, (t32_tp_le16(cp + 0x6a) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 0x68) & 0xfff);
	T32_FWR(w, n, 0x3100, (t32_tp_le16(cp + 0x6e) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 0x6c) & 0xfff);
	T32_FWR(w, n, 0x3104,sec[0x21a] & 1);
	T32_FWR(w, n, 0x3108,sec[0x21b] & 3);
	T32_FWR(w, n, 0x310c, (t32_tp_le16(sec + 0x152) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 0x150) & 0xfff);
	T32_FWR(w, n, 0x3110, (t32_tp_le16(sec + 0x156) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 0x154) & 0xfff);
	T32_FWR(w, n, 0x3114,t32_tp_le16(sec + 0x158) & 0xfff);
	T32_FWR(w, n, 0x3118, (t32_tp_le16(sec + 0x15c) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x15a) & 0x3ff);
	T32_FWR(w, n, 0x311c, (t32_tp_le16(sec + 0x160) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x15e) & 0x3ff);
	T32_FWR(w, n, 0x3120,t32_tp_le16(sec + 0x162) & 0x3ff);
	T32_FWR(w, n, 0x3124, (t32_tp_le16(cp + 0x72) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 0x70) & 0xfff);
	T32_FWR(w, n, 0x3128, (t32_tp_le16(cp + 0x76) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 0x74) & 0xfff);
	T32_FWR(w, n, 0x312c,sec[0x21c] & 1);
	T32_FWR(w, n, 0x3130,sec[0x21d] & 3);
	T32_FWR(w, n, 0x3134, (t32_tp_le16(sec + 0x166) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 0x164) & 0xfff);
	T32_FWR(w, n, 0x3138, (t32_tp_le16(sec + 0x16a) & 0xfff) << 0x10 |
			t32_tp_le16(sec + 0x168) & 0xfff);
	T32_FWR(w, n, 0x313c,t32_tp_le16(sec + 0x16c) & 0xfff);
	T32_FWR(w, n, 0x3140, (t32_tp_le16(sec + 0x170) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x16e) & 0x3ff);
	T32_FWR(w, n, 0x3144, (t32_tp_le16(sec + 0x174) & 0x3ff) << 0x10 |
			t32_tp_le16(sec + 0x172) & 0x3ff);
	T32_FWR(w, n, 0x3148,t32_tp_le16(sec + 0x176) & 0x3ff);
	T32_FWR(w, n, 0x314c, (t32_tp_le16(cp + 0x7a) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 0x78) & 0xfff);
	T32_FWR(w, n, 0x3150, (t32_tp_le16(cp + 0x7e) & 0xfff) << 0x10 |
			t32_tp_le16(cp + 0x7c) & 0xfff);
	T32_FWR(w, n, 0x3154,sec[0x21f] & 3);
	T32_FWR(w, n, 0x3158,t32_tp_le16(sec + 0x178) & 0x3ff);
	T32_FWR(w, n, 0x315c,t32_tp_le16(sec + 0x17a) & 0x3ff);
	T32_FWR(w, n, 0x3160,t32_tp_le16(sec + 0x17c) & 0x3ff);
	T32_FWR(w, n, 0x3164,sec[0x1fe] & 0xf);
	T32_FWR(w, n, 0x3010, 3);
	*np = n;
}

/* tisp_gib_write_reg(0) with the WDR flag ip10 (init / refresh / wake-up paths) */
static inline void t32_gib_write_reg(const u8 *sec, const u8 *cp, const u8 *pt, int ip10, struct t32_front_write *w, unsigned int *np)
{
	t32_gib_write_reg_mode(sec, cp, pt, 0, ip10, w, np);
}

/*
 * tisp_gib_write_blc_s_reg (the normal black level path); wdr: the WDR flag
 * (gain block byte 16, tisp_gib_wdr_en); with WDR mode 1 the vendor writes
 * the short-frame enables (section bytes 0x204 / 0x205) and levels.
 */
static inline void t32_gib_write_blc_s_wdr(const u8 *sec, const u8 *cp, const u8 *pt, u32 wdr,
					   struct t32_front_write *w, unsigned int *np)
{
	unsigned int n = *np;
	/* WDR mode 1: the short-frame levels (pointer entries 8..15, gain at +0x56) */
	unsigned int b = wdr == 1 ? 8 : 0;
	u32 go = wdr == 1 ? 0x56 : 0x4a;

	T32_FWR(w, n, 0x3188, sec[wdr == 1 ? 0x204 : 0x202] & 1);
	T32_FWR(w, n, 0x319c, sec[wdr == 1 ? 0x205 : 0x203] & 1);
	T32_FWR(w, n, 0x318c, (T32_GIB_GP(b + 1) & 0xfff) << 0x10 | T32_GIB_GP(b + 0) & 0xfff);
	T32_FWR(w, n, 0x3190, (T32_GIB_GP(b + 3) & 0xfff) << 0x10 | T32_GIB_GP(b + 2) & 0xfff);
	T32_FWR(w, n, 0x3200,t32_tp_le16(cp + go) & 0x1fff);
	T32_FWR(w, n, 0x3194, (T32_GIB_GP(b + 5) & 0xfff) << 0x10 | T32_GIB_GP(b + 4) & 0xfff);
	T32_FWR(w, n, 0x3198, (T32_GIB_GP(b + 7) & 0xfff) << 0x10 | T32_GIB_GP(b + 6) & 0xfff);
	T32_FWR(w, n, 0x3010, 3);
	*np = n;
}

/* tisp_gib_write_blc_s_reg with WDR off (init / refresh / wake-up paths) */
static inline void t32_gib_write_blc_s(const u8 *sec, const u8 *cp, const u8 *pt, struct t32_front_write *w, unsigned int *np)
{
	t32_gib_write_blc_s_wdr(sec, cp, pt, 0, w, np);
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/*
 * sec: writable copy of section 8 (T32_GIB_SEC_BYTES); bayer: the init
 * parameter's pattern (word 2 of the AddSensor init block, & 0x1f; 0 = RGGB
 * first).  The writes go to w (T32_GIB_MAX_WRITES entries).  Returns their
 * number, -1 for Bayer pattern 15 (unset entries in the vendor).
 */
static inline int t32_gib_init(u8 *sec, u32 bayer, struct t32_front_write *w)
{
	u8 cp[T32_GIB_VALUES];
	unsigned int n = 0, k;
	const u8 *pt;
	int row;
	static const u8 src[5] = { 0x24, 0x3a, 0x50, 0x66, 0x7c };
	static const u8 srcs[5] = { 0x9c, 0xb2, 0xc8, 0xde, 0xf4 };
	static const u16 slx[5] = { 0x114, 0x128, 0x13c, 0x150, 0x164 };
	u32 f = 0x400;	/* the vendor's gain words: index 0, fraction 0x400 */

	row = t32_gib_ptr_row(bayer);
	if (row < 0)
		return -1;
	pt = t32_gib_ptr_tab[row];
	for (k = 0; k < T32_GIB_VALUES; k++)
		cp[k] = 0;
	sec[0x220] = 0;
	sec[0x221] = 0;
	/* tisp_gib_interp_by_again */
	for (k = 0; k < 5; k++) {
		if (!t32_front_le32(sec + 4))
			t32_tp_put16(cp + 0x40 + 2 * k, t32_tp_intp_u16(0, f, sec + src[k]));
		else
			t32_tp_put16(cp + 0x40 + 2 * k, t32_tp_le16(sec + 0x92 + 2 * k));
	}
	if (!t32_front_le32(sec + 4) && !sec[0x223])
		for (k = 0; k < 5; k++)
			t32_tp_put16(sec + 0x92 + 2 * k, t32_tp_le16(cp + 0x40 + 2 * k));
	cp[0x80] = !t32_front_le32(sec + 4) ? t32_tp_intp_u8(0, f, sec + 0x207) : sec[0x212];
	t32_tp_put16(cp + 0x4a, t32_gib_gain(cp + 0x40));
	/* tisp_gib_slope_update */
	for (k = 0; k < 5; k++)
		t32_gib_slope(sec + slx[k], sec + slx[k] + 10, cp + 0x58 + 8 * k);
	t32_gib_write_reg(sec, cp, pt, 0, w, &n);
	/* tisp_gib_interp_by_again_s */
	for (k = 0; k < 5; k++) {
		if (!t32_front_le32(sec + 4))
			t32_tp_put16(cp + 0x4c + 2 * k, t32_tp_intp_u16(0, f, sec + srcs[k]));
		else
			t32_tp_put16(cp + 0x4c + 2 * k, t32_tp_le16(sec + 0x10a + 2 * k));
	}
	if (!t32_front_le32(sec + 4) && !sec[0x223])
		for (k = 0; k < 5; k++)
			t32_tp_put16(sec + 0x10a + 2 * k, t32_tp_le16(cp + 0x4c + 2 * k));
	t32_tp_put16(cp + 0x56, t32_gib_gain(cp + 0x4c));
	t32_gib_write_blc_s(sec, cp, pt, w, &n);
	T32_FWR(w, n, 0x3000, T32_GIB_ID);
	return (int)n;
}

/* Context for the per-frame paths: the vendor's block per channel holds +0
 * the section pointer, +4 a pointer to the two gain words (index << 16 |
 * fraction) used by tisp_gib_interp_by_again (+0) and
 * tisp_gib_interp_by_again_s (+4), +8 the working block pointer and +16 the
 * held black levels (u16 flag, then 5 levels at +2 and 5 at +14).
 */
struct t32_gib_ctx {
	u8 *sec;		/* IQ bin section 8 */
	u8 *cp;			/* 0x84-byte working block */
	u8 *gw;			/* two gain words */
	u8 *ptrow;		/* heap copy of the Bayer pointer-table row */
	u8 *hold;		/* T32_GIB_HOLD_BYTES: flag + held black levels */
};

#define T32_GIB_HOLD_BYTES	24

/*
 * tisp_gib_interp_by_again (set 0) / tisp_gib_interp_by_again_s (set 1):
 * black levels at gain word gw into cp + cpo, the gain into cp + cpo + 10.
 * sec[0x220] / sec[0x221] select whether the held levels (hold flag 1) are
 * used; sec[0x223] == 1 with sec[0x220] 2 (or 0 and sec[0x221] 2) writes the
 * held levels back into the section.  Set 0 also sets cp[0x80].
 */
static inline void t32_gib_interp_set(struct t32_gib_ctx *ctx, u32 gw, int set)
{
	static const u8 src[2][5] = { { 0x24, 0x3a, 0x50, 0x66, 0x7c }, { 0x9c, 0xb2, 0xc8, 0xde, 0xf4 } };
	u8 *sec = ctx->sec, *cp = ctx->cp, *hold = ctx->hold;
	u32 cpo = set ? 0x4c : 0x40, so = set ? 0x10a : 0x92, ho = set ? 14 : 2;
	u32 m = sec[0x220], m2 = sec[0x221], held = t32_tp_le16(hold);
	unsigned int k;

	if (m != 1 && (m != 0 || m2 >= 2) && held == 1) {
		for (k = 0; k < 5; k++)
			t32_tp_put16(cp + cpo + 2 * k, t32_tp_le16(hold + ho + 2 * k));
	} else {
		for (k = 0; k < 5; k++) {
			if (!t32_front_le32(sec + 4))
				t32_tp_put16(cp + cpo + 2 * k, t32_tp_intp_u16(gw >> 16, gw & 0xffff, sec + src[set][k]));
			else
				t32_tp_put16(cp + cpo + 2 * k, t32_tp_le16(sec + so + 2 * k));
		}
		if (!t32_front_le32(sec + 4) && !sec[0x223])
			for (k = 0; k < 5; k++)
				t32_tp_put16(sec + so + 2 * k, t32_tp_le16(cp + cpo + 2 * k));
		if (held != 1) {
			for (k = 0; k < 5; k++)
				t32_tp_put16(hold + ho + 2 * k, t32_tp_le16(cp + cpo + 2 * k));
			held = 0;	/* skip the write back below */
		}
	}
	if (held == 1 && sec[0x223] == 1 && (m == 2 || (m == 0 && m2 == 2)))
		for (k = 0; k < 5; k++)
			t32_tp_put16(sec + so + 2 * k, t32_tp_le16(hold + ho + 2 * k));
	if (!set)
		cp[0x80] = !t32_front_le32(sec + 4) ? t32_tp_intp_u8(gw >> 16, gw & 0xffff, sec + 0x207) : sec[0x212];
	t32_tp_put16(cp + cpo + 10, t32_gib_gain(cp + cpo));
}

/* tisp_gib_slope_update: five slope tables into the working block */
static inline void t32_gib_slope_update(struct t32_gib_ctx *ctx)
{
	static const u16 slx[5] = { 0x114, 0x128, 0x13c, 0x150, 0x164 };
	u8 *sec = ctx->sec;
	u8 *cp = ctx->cp;
	unsigned int k;

	for (k = 0; k < 5; k++)
		t32_gib_slope(sec + slx[k], sec + slx[k] + 10, cp + 0x58 + 8 * k);
}

/* tisp_gib_dn_params_refresh: re-interpolate at the stored gain, then write.
 * With mode 1 the vendor clears three section bytes first.  bayer is the
 * pattern given to the init (it selects the pointer table row). */
static inline int t32_gib_dn_params_refresh(struct t32_gib_ctx *ctx, u8 ch, u32 mode, u32 bayer,
					    struct t32_front_write *w)
{
	const u8 *pt;
	int row;
	unsigned int n = 0;

	(void)ch;
	row = t32_gib_ptr_row(bayer);
	if (row < 0)
		return -1;
	pt = t32_gib_ptr_tab[row];
	if (mode == 1) {
		ctx->sec[0x206] = 0;
		ctx->sec[0x21e] = 0;
		ctx->sec[0x21f] = 0;
	}
	t32_gib_interp_set(ctx, t32_front_le32(ctx->gw), 0);
	t32_gib_slope_update(ctx);
	t32_gib_write_reg(ctx->sec, ctx->cp, pt, 0, w, &n);
	t32_gib_interp_set(ctx, t32_front_le32(ctx->gw + 4), 1);
	t32_gib_write_blc_s(ctx->sec, ctx->cp, pt, w, &n);
	return (int)n;
}

/* tisp_gib_wake_up: write the working block and the normal black level path */
static inline int t32_gib_wake_up(struct t32_gib_ctx *ctx, u8 ch, u32 bayer,
				  struct t32_front_write *w)
{
	const u8 *pt;
	int row;
	unsigned int n = 0;

	(void)ch;
	row = t32_gib_ptr_row(bayer);
	if (row < 0)
		return -1;
	pt = t32_gib_ptr_tab[row];
	t32_gib_write_reg(ctx->sec, ctx->cp, pt, 0, w, &n);
	t32_gib_write_blc_s(ctx->sec, ctx->cp, pt, w, &n);
	return (int)n;
}

/*
 * Per-frame / API paths: tisp_gib_again_update, tisp_gib_again_s_update,
 * tisp_gib_param_array_get / _set, tisp_gib_api_set_blc / _get_blc,
 * tisp_gib_wdr_en and tisp_gib_deinit.  The vendor reaches the context
 * through t32_data_e17c0[ch]; the port takes the context directly.  The
 * context layout is the vendor's: +0 section pointer, +4 gain block pointer
 * (two gain words), +8 working block pointer, +16 held black levels.
 */

/* little-endian store of a gain word into the gain block */
static inline void t32_gib_put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* tisp_gib_again_update: store the gain word, interpolate set 0, write */
static inline int t32_gib_again_update(struct t32_gib_ctx *ctx, u8 ch, u32 gain,
				       struct t32_front_write *w)
{
	unsigned int n = 0;

	(void)ch;
	t32_gib_interp_set(ctx, gain, 0);
	t32_gib_put32(ctx->gw, gain);
	t32_gib_write_reg_mode(ctx->sec, ctx->cp, ctx->ptrow, 1, ctx->gw[16], w, &n);
	return (int)n;
}

/* tisp_gib_again_s_update: store the gain word, interpolate set 1, write blc_s */
static inline int t32_gib_again_s_update(struct t32_gib_ctx *ctx, u8 ch, u32 gain,
					 struct t32_front_write *w)
{
	unsigned int n = 0;

	(void)ch;
	t32_gib_put32(ctx->gw + 4, gain);
	t32_gib_interp_set(ctx, t32_front_le32(ctx->gw + 4), 1);
	t32_gib_write_blc_s_wdr(ctx->sec, ctx->cp, ctx->ptrow, ctx->gw[16], w, &n);
	return (int)n;
}

/* tisp_gib_param_array_get: the section is copied out whole */
static inline int t32_gib_param_array_get(const struct t32_gib_ctx *ctx, u8 *buf, u32 *len)
{
	memcpy(buf, ctx->sec, T32_GIB_SEC_BYTES);
	*len = T32_GIB_SEC_BYTES;
	return 0;
}

/* tisp_gib_param_array_set: replace the section, then the refresh path */
static inline int t32_gib_param_array_set(struct t32_gib_ctx *ctx, u8 ch, const u8 *buf,
					  struct t32_front_write *w)
{
	unsigned int n = 0;

	(void)ch;
	memcpy(ctx->sec, buf, T32_GIB_SEC_BYTES);
	ctx->sec[0x221] = 1;
	t32_gib_interp_set(ctx, t32_front_le32(ctx->gw), 0);
	t32_gib_slope_update(ctx);
	t32_gib_write_reg_mode(ctx->sec, ctx->cp, ctx->ptrow, 0, ctx->gw[16], w, &n);
	t32_gib_interp_set(ctx, t32_front_le32(ctx->gw + 4), 1);
	t32_gib_write_blc_s_wdr(ctx->sec, ctx->cp, ctx->ptrow, ctx->gw[16], w, &n);
	return (int)n;
}

/* tisp_gib_api_set_blc: copy the held levels in, mode 2, write both paths */
static inline int t32_gib_api_set_blc(struct t32_gib_ctx *ctx, u8 ch, const u8 *blc,
				      struct t32_front_write *w)
{
	unsigned int n = 0;

	(void)ch;
	if (!ctx)
		return -1;
	memcpy(ctx->hold, blc, T32_GIB_HOLD_BYTES);
	ctx->sec[0x221] = 2;
	t32_gib_interp_set(ctx, t32_front_le32(ctx->gw), 0);
	t32_gib_write_reg_mode(ctx->sec, ctx->cp, ctx->ptrow, 1, ctx->gw[16], w, &n);
	t32_gib_interp_set(ctx, t32_front_le32(ctx->gw + 4), 1);
	t32_gib_write_blc_s_wdr(ctx->sec, ctx->cp, ctx->ptrow, ctx->gw[16], w, &n);
	return (int)n;
}

/* tisp_gib_api_get_blc: copy the held levels out */
static inline int t32_gib_api_get_blc(const struct t32_gib_ctx *ctx, u8 *blc)
{
	if (!ctx)
		return -1;
	memcpy(blc, ctx->hold, T32_GIB_HOLD_BYTES);
	return 0;
}

/* tisp_gib_wdr_en: store the flag in the gain block, write the blc_s path */
static inline int t32_gib_wdr_en(struct t32_gib_ctx *ctx, u8 ch, u8 en,
				 struct t32_front_write *w)
{
	unsigned int n = 0;

	(void)ch;
	ctx->gw[16] = en;
	t32_gib_write_blc_s_wdr(ctx->sec, ctx->cp, ctx->ptrow, ctx->gw[16], w, &n);
	return (int)n;
}

/* tisp_gib_deinit: free the gain block, working block, held levels and context */
static inline int t32_gib_deinit(void (*vfree)(void *p), struct t32_gib_ctx **slot)
{
	struct t32_gib_ctx *ctx = *slot;

	if (ctx) {
		if (ctx->gw) {
			vfree(ctx->gw);
			ctx->gw = NULL;
		}
		if (ctx->cp) {
			vfree(ctx->cp);
			ctx->cp = NULL;
		}
		if (ctx->ptrow) {
			vfree(ctx->ptrow);
			ctx->ptrow = NULL;
		}
		if (ctx->hold) {
			vfree(ctx->hold);
			ctx->hold = NULL;
		}
		vfree(*slot);
		*slot = NULL;
	}
	return 0;
}

#undef T32_GIB_GP
#endif /* TX_ISP_T32_GIB_H */
