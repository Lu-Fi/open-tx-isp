/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) per-frame AWB: the vendor tisp_awb_process with
 * tisp_t33_awb_long_alogrithm, on the T32 port of the AWB algorithm
 * (tx_isp_t32_awb*.h, from Matt Davis' recovered C, PR #15).
 *
 * What the T33 module does differently from the T32 one (Ghidra project
 * t33, vendor tx-isp-t33.ko):
 *  - no tisp_awb_get_statistics unpack: tisp_t33_awb_long_alogrithm and
 *    tisp_t33_awb_sat_weight read the flat zone records
 *    (tx_isp_t33_stats.h) themselves.  Per zone the white sums R / G / B
 *    and the white count take the T32 group 0 places (n_r, den, n_b, w),
 *    the sums and count of all quads group 1.  t33_awb_load fills the T32
 *    per-zone tables that way, so phases 1..6 of the T32 port run
 *    unchanged (same arithmetic: (n << 10) / den, shares n / w, the
 *    1000 * w / area clear, the sat weights).
 *  - after sat_weight the vendor compares the buffer's closing tag with
 *    its head tag; on a mismatch it counts the frame (awbInfo + 0x14) and
 *    leaves before the still test (t33_awb_long_alogrithm returns 2).
 *  - run state: the T32 layout without the 40980 bytes of unpacked zone
 *    tables (T33 0x94a8 = T32 79036 lg_have, 0x10438 = T32 107596, ...);
 *    ours keeps the T32 layout, offsets below 38056 are equal.  The AWB
 *    section (tparamsP + 0xd48) has the T32 field offsets.
 *  - tisp_awb_set_gain keeps 0x10000 / gain (not the gain) in the history
 *    pair (st+0x10450 = T32 hist), writes R / B to AWBG list 7 slots 3 / 4
 *    (the B word through a 16 bit local, tx_isp_t33_awb.h) instead of
 *    0x6040 / 0x6044 and 0x2a040 / 0x2a044.
 *  - tisp_awb_spec_calculate writes AWB list 29 slots (slot s = register
 *    0x1604c + 4 * s) instead of the AWB registers.
 */
#ifndef TX_ISP_T33_AWBALG_H
#define TX_ISP_T33_AWBALG_H

#include "tx_isp_t32_awbinit.h"
#include "tx_isp_t33_stats.h"

/* the flat zone records of one statistics buffer into the T32 per-zone tables (z = row * 32 + col) */
static inline void t33_awb_load(struct t32_awb_state *st, const u8 *buf, u32 rows, u32 cols)
{
	struct t33_awb_zone zn;
	u32 r, c, z;

	if (rows > T33_AWB_GRID_MAX || cols > T33_AWB_GRID_MAX)
		return;
	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++) {
			z = r * 32 + c;
			t33_awb_zone_get(buf, cols, r, c, &zn);
			st->g0_n_r[z] = zn.wr;
			st->g0_den[z] = zn.wg;
			st->g0_n_b[z] = zn.wb;
			st->g0_w[z] = zn.wn;
			st->g1_n_r[z] = zn.r;
			st->g1_den[z] = zn.g;
			st->g1_n_b[z] = zn.b;
			st->g1_w[z] = zn.n;
		}
}

/* t32_awb_lg_gate after its sat_weight: the still test, else the reference means and the cleared detection tables */
static inline int t33_awb_lg_still(struct t32_awb *a, const struct t32_awb_lg *l)
{
	struct t32_awb_state *st = a->st;

	if (st->lg_have == 1) {
		u32 dr = (u16)(st->avg[4] > st->avg[0] ? st->avg[4] - st->avg[0] : st->avg[0] - st->avg[4]);
		u32 db = (u16)(st->avg[5] > st->avg[1] ? st->avg[5] - st->avg[1] : st->avg[1] - st->avg[5]);

		if (st->lg_calm == 1 && (u16)(dr + db) < l->still && !(t32_awb_u32at(st->raw, 107596) & 0x00ff00ffU))
			return 1;
	}
	st->avg[4] = st->avg[0];
	st->avg[5] = st->avg[1];
	memset(st->raw + 79228, 0, 784);
	memset(st->raw + 80012, 0, 784);
	memset(st->raw + 80796, 0, 784);
	memset(st->raw + 81188, 0, 144);
	memset(st->raw + 81332, 0, 720);
	memset(st->raw + 82052, 0, 960);
	memset(st->raw + 87108, 0, 4096);
	memset(st->raw + 91204, 0, 4096);
	memset(st->raw + 95300, 0, 4096);
	memset(st->raw + 99396, 0, 4096);
	memset(st->raw + 103492, 0, 4096);
	return 0;
}

/*
 * tisp_t33_awb_long_alogrithm on the buffer buf (0x7400 bytes, already
 * loaded with t33_awb_load).  0 ran, 1 still picture, 2 tag mismatch
 * (*drops counted), -1 refused (grid over 32 x 32).
 */
static inline int t33_awb_long_alogrithm(struct t32_awb *a, struct t32_awb_ctd *d, u32 tbl, const u8 *buf,
					 u32 *drops, int *path)
{
	const struct t32_awb_par *par = a->par;
	struct t32_awb_state *st = a->st;
	struct t32_awb_lg l;
	int p;

	t32_awb_lg_entry(a, &l);
	if (t32_awb_lg_zones(a, &l))
		return -1;
	t32_awb_lg_means(a, &l);
	t32_awb_lg_shares(a, &l);
	t32_awb_lg_clear(a, &l);
	if ((u16)(par->sat_mode - 1) < 2)
		t32_awb_sat_weight(par, st->g0_n_r, st->g0_den, st->g0_n_b, st->g0_w, st->zt2, l.gain);
	if (t33_st_le16(buf) != t33_st_le16(buf + t33_awb_tag_off(l.rows, l.cols))) {
		(*drops)++;
		return 2;
	}
	if (t33_awb_lg_still(a, &l))
		return 1;
	if (t32_awb_lg_detect(a, &l, d, tbl))
		return -1;
	p = t32_awb_lg_apply(a, &l);
	if (path)
		*path = p;
	return 0;
}

/* AWB list 29 slot of an AWB register (0x16060 .. 0x160fc), -1 outside */
static inline int t33_awb_slot(u32 reg)
{
	if (reg == 0x16040 || reg == 0x16044)
		return 3 + (int)((reg - 0x16040) >> 2);
	if (reg >= 0x16060 && reg <= 0x160fc && !(reg & 3))
		return (int)((reg - 0x1604c) >> 2);
	return -1;
}

/* one AWB frame's results for the lists */
struct t33_awb_out {
	struct t32_stats_write spec[2 * T32_AWB_MAX_WRITES];	/* spec_calculate's writes (list 29 registers) */
	unsigned int nspec;
	u32 gain_r, gain_b;	/* AWBG list 7 slots 3 / 4 (B word 16 bit) */
	int gain_set;		/* 0: frozen, nothing for list 7 */
	int rc, path;		/* long algorithm result / apply path */
	u32 ct;			/* event 11 */
};

/*
 * tisp_awb_process: long_par_update, spec_calculate, the T33 long
 * algorithm, set_gain, event 11 and the 0..100 counter.  rd reads the
 * AWB registers (0x1608c, the spec statistics 0x16190..).  Returns -1
 * when the algorithm refuses the grid.
 */
static inline int t33_awb_process(struct t32_awb *a, struct t32_awb_ctd *d, u32 (*rd)(void *, u32), void *rdp,
				  const u8 *buf, u16 *counter, u32 *drops, struct t33_awb_out *o)
{
	struct t32_stats_write w[T32_AWB_MAX_WRITES];
	struct t32_awb_lp lp;
	unsigned int n, i;
	u32 next;

	t32_awb_long_par_update(a, &lp);
	o->nspec = t32_awb_spec_calculate(a, rd, rdp, o->spec);
	o->path = 0;
	o->rc = t33_awb_long_alogrithm(a, d, lp.tbl, buf, drops, &o->path);
	if (o->rc < 0)
		return -1;
	n = t32_awb_set_gain(a, w);
	/* the T33 keeps the inverses in the history pair */
	a->st->hist[0] = a->hw->inv_r;
	a->st->hist[1] = a->hw->inv_b;
	o->gain_set = 0;
	for (i = 0; i < n; i++) {
		if (w[i].reg == 0x6040) {
			o->gain_r = w[i].val;
			o->gain_set = 1;
		} else if (w[i].reg == 0x6044) {
			o->gain_b = w[i].val & 0xffff;	/* vendor: lhu of the gain_reg result */
		}
	}
	o->ct = a->par->ct;
	next = *counter + 1;
	*counter = next < 101 ? next : 0;
	return 0;
}

#endif /* TX_ISP_T33_AWBALG_H */
