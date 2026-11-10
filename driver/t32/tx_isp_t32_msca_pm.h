/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA suspend / resume: readable C of tisp_msca_sleep(ch) and tisp_msca_wake_up(ch).
 * sleep reads a fixed list of MSCA registers (tisp_reg_read(ch, ...): direct address, or the
 * shadow index in shadow mode) into global save arrays (shared by all channels), then saves the
 * OSD mask word (0xf0048 -> sb + 880) and the channel's four "changed" words into the channel's
 * save record (t32_data_e2600 + 5 words per channel: [0] sb+880, [1..3] one nibble per set
 * telling which of the set's 4 mask-block enable bytes (+52, +68, +84, +100) are non-zero (also
 * stored to sb + 884 + 872 * set), [4] sb+888 = the enable bytes of the lines whose mode is 0, 1
 * or 2, shifted to their line number); the channel's autozoom flag byte (t32_data_e2760[ch]) is
 * set to byte +20 of the third set.
 * wake_up writes everything back with tisp_reg_write in the same table order: shadow-RAM flag
 * token, size words, the saved registers (re-running init_chx_cfg before each set's group),
 * scaling_algorithm, the saved changed words, cfg_shd(ch, 1) and finally the kick 0xf0010 = 1
 * (shadow index 13).
 * The vendor sleep stores the autozoom byte after every set; only the last store is visible.
 * Two shadow indices differ between the functions: sleep re-reads index 110 for the second
 * "+12" slot where wake_up writes index 78 / register 0xf0218.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscaq_oracle).
 */
#ifndef TX_ISP_T32_MSCA_PM_H
#define TX_ISP_T32_MSCA_PM_H

#include "tx_isp_t32_msca_cfg.h"

struct t32_msca_pm {
	u32 a2884[5], a2878[3], a2854[9], a2830[9], a280c[9], a27e8[9];
	u32 a27b8[12], a2788[12], a2778[4];
	u32 a2600[5];		/* the channel's save record */
};

struct t32_pm_item {
	u32 idx, reg;
	u32 *slot;
};

static inline unsigned int t32_pm_set_items(struct t32_msca_pm *pm, u32 k, int wake, struct t32_pm_item *it)
{
	static const u16 sreg[6] = { 308, 564, 820, 340, 596, 852 };
	static const u8 sidx[6] = { 54, 86, 118, 62, 94, 126 };
	unsigned int j, n = 0;

	it[n++] = (struct t32_pm_item){ 312, 0xf0800 + 4 * k, &pm->a2878[k] };
	it[n++] = (struct t32_pm_item){ 25, 0xf0060 + 4 * k, &pm->a2854[k] };
	it[n++] = (struct t32_pm_item){ 29, 0xf0070 + 4 * k, &pm->a2854[k + 3] };
	it[n++] = (struct t32_pm_item){ 33, 0xf0080 + 4 * k, &pm->a2854[k + 6] };
	it[n++] = (struct t32_pm_item){ 46, 0xf0118 + 4 * k, &pm->a2830[k] };
	if (wake)
		it[n++] = (struct t32_pm_item){ 78, 0xf0218 + 4 * k, &pm->a2830[k + 3] };
	else
		it[n++] = (struct t32_pm_item){ 110, 0xf0318 + 4 * k, &pm->a2830[k + 3] };
	it[n++] = (struct t32_pm_item){ 110, 0xf0318 + 4 * k, &pm->a2830[k + 6] };
	for (j = 0; j < 6; j++)
		it[n++] = (struct t32_pm_item){ k == 1 ? sidx[j] : 16395, 0xf0000 + sreg[j] + 4 * k,
						j < 3 ? &pm->a280c[k + 3 * j] : &pm->a27e8[k + 3 * (j - 3)] };
	return n;
}

static inline unsigned int t32_pm_loop2_items(struct t32_msca_pm *pm, u32 i, struct t32_pm_item *it)
{
	it[0] = (struct t32_pm_item){ 56, 0xf0140 + 4 * i, &pm->a27b8[i] };
	it[1] = (struct t32_pm_item){ 88, 0xf0240 + 4 * i, &pm->a27b8[i + 4] };
	it[2] = (struct t32_pm_item){ 120, 0xf0340 + 4 * i, &pm->a27b8[i + 8] };
	it[3] = (struct t32_pm_item){ 64, 0xf0160 + 4 * i, &pm->a2788[i] };
	it[4] = (struct t32_pm_item){ 96, 0xf0260 + 4 * i, &pm->a2788[i + 4] };
	it[5] = (struct t32_pm_item){ 128, 0xf0360 + 4 * i, &pm->a2788[i + 8] };
	it[6] = (struct t32_pm_item){ 265, 0xf0740 + 4 * i, &pm->a2778[i] };
	return 7;
}

static inline void t32_pm_prologue_items(struct t32_msca_pm *pm, struct t32_pm_item *it)
{
	it[0] = (struct t32_pm_item){ 17, 0xf0040, &pm->a2884[0] };
	it[1] = (struct t32_pm_item){ 21, 0xf0050, &pm->a2884[1] };
	it[2] = (struct t32_pm_item){ 16395, 0xf0054, &pm->a2884[2] };
	it[3] = (struct t32_pm_item){ 316, 0xf0810, &pm->a2884[3] };
	it[4] = (struct t32_pm_item){ 16395, 0xf0814, &pm->a2884[4] };
}

/* sb = sets of channel ch; rec = pm->a2600 of the channel; az = &t32_data_e2760[ch] */
static inline void t32_msca_sleep(struct t32_ms_io *io, struct t32_msca_pm *pm, u8 *sb, u32 ch, u8 *az)
{
	struct t32_pm_item it[13];
	unsigned int i, n, k;

	t32_pm_prologue_items(pm, it);
	for (i = 0; i < 5; i++)
		*it[i].slot = t32_ms_tr(io, ch, it[i].idx, it[i].reg);
	for (k = 0; k < 3; k++) {
		n = t32_pm_set_items(pm, k, 0, it);
		for (i = 0; i < n; i++)
			*it[i].slot = t32_ms_tr(io, ch, it[i].idx, it[i].reg);
	}
	*az = (u8)t32_msa_r16(sb + 872 * 2 + 20);
	for (k = 0; k < 4; k++) {
		n = t32_pm_loop2_items(pm, k, it);
		for (i = 0; i < n; i++)
			*it[i].slot = t32_ms_tr(io, ch, it[i].idx, it[i].reg);
	}
	t32_msa_w32(sb + 880, t32_ms_tr(io, ch, 19, 0xf0048));
	pm->a2600[0] = t32_msa_r32(sb + 880);
	for (k = 0; k < 3; k++) {
		u32 m = 0;

		for (i = 0; i < 4; i++)
			if (sb[872 * k + 52 + 16 * i])
				m |= 1u << i;
		t32_msa_w32(sb + 872 * k + 884, m);
		pm->a2600[1 + k] = m;
	}
	t32_msa_w32(sb + 888, 0);
	pm->a2600[4] = 0;
	for (i = 0; i < 20; i++) {
		const u8 *l = sb + 116 + 28 * i;

		if (l[0] > 2)
			continue;
		t32_msa_w32(sb + 888, t32_msa_r32(sb + 888) | (u32)l[4] << i);
		pm->a2600[4] = t32_msa_r32(sb + 888);
	}
}

static inline void t32_msca_wake_up(const struct t32_msca_cfg_ops *ops, struct t32_ms_io *io,
				    struct t32_msca_cfg_st *st, struct t32_msca_pm *pm, u32 ch,
				    u16 width, u16 height)
{
	struct t32_pm_item it[13];
	unsigned int i, n, k;
	u8 *sb = st->sb;

	t32_msca_set_shdram_flag(io, ch, 2, st->w302, NULL);
	t32_ms_tw(io, ch, 37, 0xf0090, 0);
	t32_ms_tw(io, ch, 38, 0xf0094, (u32)width << 16 | height);
	t32_pm_prologue_items(pm, it);
	for (i = 0; i < 5; i++)
		t32_ms_tw(io, ch, it[i].idx, it[i].reg, *it[i].slot);
	for (k = 0; k < 3; k++) {
		t32_msca_init_chx_cfg_w(st->c, sb, k, io->w, &io->n);
		n = t32_pm_set_items(pm, k, 1, it);
		for (i = 0; i < n; i++)
			t32_ms_tw(io, ch, it[i].idx, it[i].reg, *it[i].slot);
	}
	for (k = 0; k < 4; k++) {
		n = t32_pm_loop2_items(pm, k, it);
		for (i = 0; i < n; i++)
			t32_ms_tw(io, ch, it[i].idx, it[i].reg, *it[i].slot);
	}
	t32_msca_scaling_algorithm_w(st->c, io->w, &io->n);
	t32_msa_w32(sb + 880, pm->a2600[0]);
	t32_msa_w32(sb + 884, pm->a2600[1]);
	t32_msa_w32(sb + 1756, pm->a2600[2]);
	t32_msa_w32(sb + 2628, pm->a2600[3]);
	t32_msa_w32(sb + 888, pm->a2600[4]);
	t32_msca_cfg_shd(ops, io, st, ch, 1);
	t32_ms_tw(io, ch, 13, 0xf0010, 1);
}

#endif /* TX_ISP_T32_MSCA_PM_H */
