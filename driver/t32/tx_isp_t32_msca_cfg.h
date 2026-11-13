/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA configuration commit: readable C of tisp_msca_cfg_shd(ch, force) and
 * tisp_msca_chx_cfg_load(ch, set).
 *
 * cfg_shd runs under the MSCA spin lock (ops->lock / ops->unlock) and brings every pending
 * change of channel ch to the hardware:
 *   1. a pending day/night refresh (dn flag, t32_data_e2764[ch]) runs dn_refresh; otherwise, when
 *      nothing at all is pending (shadow-scale flag, autozoom flag | force, curve pending flag,
 *      changed words at sb + 880 / 884 / 1756 / 2628 / 888) it just unlocks and returns;
 *   2. a pending shadow-scale flag (t32_data_e275c[ch]) merges the bits 1..3 / 4 / 5 of the
 *      three sets' words at sb + 48 / 920 / 1792 into register 0xf0050 (shadow index 21);
 *   3. an autozoom flag of exactly 1 (t32_data_e2760[ch]) re-runs para_calc / curve_calc /
 *      init_chx_cfg for all three sets and marks the curves pending;
 *   4. direct mode (t32_data_d03a0 == 0): refuses with an isp_printf when 0xf8100 bit 1 is set or
 *      when the shadow-RAM flag is not 90 and force is 0; else it clears the flag, starts the
 *      RAM write (0xf8000 = 257), writes the curves (when force or pending), the OSD, mask and
 *      line configs (each only if its words show a change), stores the RAM flag with the counted
 *      writes, commits (0xf8000 = 0x01010102) and, when *cnt != 0, starts the transfer
 *      (0xf8100 = (cnt - 1) << 4 | 1);
 *      shadow mode: OSD, mask and line configs, then ch_curve_write_multi when force or pending.
 * The vendor guards each direct-mode writer with "cnt < 257, else isp_printf"; cnt is at most
 * 78 + 43 + 39 before the last writer, so those guards never fire and are dropped.
 *
 * chx_cfg_load (a channel/set started): sets the set's enable flag in the channel's parameter
 * block (par + 172 + set), then under the lock para_calc, curve_calc and scaling_algorithm,
 * runs cfg_shd(ch, 1), and under the lock again init_chx_cfg for the set; when register 256
 * (shadow index 11) reads 5 and set is 0 the two strides 0xf0140 / 0xf0160 are set to 4096;
 * finally the kick 0xf0010 = 1.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscap_oracle).  The callees are our own ports (curve.h, crop.h, dn.h,
 * mask.h, osd.h, line.h, io.h); their register writes are modelled in direct mode (see there).
 */
#ifndef TX_ISP_T32_MSCA_CFG_H
#define TX_ISP_T32_MSCA_CFG_H

#include "tx_isp_t32_msca_dn.h"
#include "tx_isp_t32_msca_mask.h"
#include "tx_isp_t32_msca_osd.h"
#include "tx_isp_t32_msca_line.h"

struct t32_msca_cfg_ops {
	void (*lock)(void);
	void (*unlock)(void);
	void (*msg)(unsigned int kind, u32 idx);	/* 0: busy / flag not ready; 1: refused line idx */
};

struct t32_msca_cfg_st {
	struct t32_msca_ch *c;
	u8 *sb;			/* t32_data_e2768[ch] (the sets) */
	u8 dn;			/* t32_data_e2764[ch] */
	u8 shd;			/* t32_data_e275c[ch] */
	u8 az;			/* t32_data_e2760[ch] */
	u32 par_base;		/* t32_data_dd0a4[ch] */
	u32 *osd_info;		/* t32_data_e28bc */
	const u16 *w302;	/* the channel context's word at +302 */
};

static inline void t32_msca_cfg_lines(const struct t32_msca_cfg_ops *ops, struct t32_ms_io *io,
				      struct t32_msca_cfg_st *st, u32 ch, u16 *cnt)
{
	u8 *sb = st->sb;
	u32 sk[20], i;
	unsigned int ns;

	if (t32_msa_r32(sb + 880))
		t32_msca_api_set_osd(io, sb, ch, cnt, st->osd_info);
	if (t32_msa_r32(sb + 884) || t32_msa_r32(sb + 1756) || t32_msa_r32(sb + 2628))
		t32_msca_api_set_mask(io, sb, ch, cnt);
	if (t32_msa_r32(sb + 888)) {
		t32_msca_api_set_line(io, sb, ch, cnt, sk, &ns);
		for (i = 0; i < ns; i++)
			ops->msg(1, sk[i]);
	}
}

static inline void t32_msca_cfg_shd(const struct t32_msca_cfg_ops *ops, struct t32_ms_io *io,
				    struct t32_msca_cfg_st *st, u32 ch, u32 force)
{
	struct t32_msca_ch *c = st->c;
	u8 *sb = st->sb;
	u16 cnt = 0;
	u32 set;

	ch &= 0xff;
	force &= 0xff;
	ops->lock();
	if (st->dn) {
		io->n += (unsigned int)t32_msca_dn_refresh(c, sb, st->par_base, io->w + io->n);
		st->dn = 0;
	} else if (!st->shd && !(st->az | force) && !c->pending && !t32_msa_r32(sb + 880) &&
		   !t32_msa_r32(sb + 884) && !t32_msa_r32(sb + 1756) && !t32_msa_r32(sb + 2628) &&
		   !t32_msa_r32(sb + 888)) {
		ops->unlock();
		return;
	}
	if (st->shd) {
		u32 t3 = t32_msa_r32(sb + 48), t4 = t32_msa_r32(sb + 920), t5 = t32_msa_r32(sb + 1792);
		u32 v;

		v = (t5 >> 3 & 1) << 29 | (t4 >> 3 & 1) << 28 | (t3 >> 3 & 1) << 27 |
		    (t5 >> 1 & 1) << 26 | (t4 >> 1 & 1) << 25 | (t3 >> 1 & 1) << 24 |
		    (t5 >> 2 & 1) << 21 | (t4 >> 2 & 1) << 20 | (t3 >> 2 & 1) << 19 |
		    (t5 & 1) << 18 | (t4 & 1) << 17 | (t3 & 1) << 16 |
		    (t5 >> 5 & 1) << 15 | (t5 >> 4 & 1) << 14 | (t4 >> 5 & 1) << 13 |
		    (t4 >> 4 & 1) << 12 | (t3 >> 5 & 1) << 11 | (t3 >> 4 & 1) << 10;
		t32_ms_tw(io, ch, 21, 0xf0050, (t32_ms_tr(io, ch, 21, 0xf0050) & 0xc00001ffu) | v);
		st->shd = 0;
	}
	if (st->az == 1) {
		for (set = 0; set < 3; set++) {
			t32_msca_para_calc(c, sb, set);
			t32_msca_curve_calc(c, sb, set);
			t32_msca_init_chx_cfg_w(c, sb, set, io->w, &io->n);
		}
		st->az = 0;
		c->pending = 1;
	}
	if (io->shadow) {
		t32_msca_cfg_lines(ops, io, st, ch, &cnt);
		if (force | c->pending)
			io->n += (unsigned int)t32_msca_ch_curve_write_multi(c, io->w + io->n);
		ops->unlock();
		return;
	}
	if (t32_ms_sr(io, 0xf8100) & 2) {
		ops->msg(0, 0);
		ops->unlock();
		return;
	}
	if (t32_msca_get_shdram_flag(io) != 90 && !force) {
		ops->msg(0, 0);
		ops->unlock();
		return;
	}
	t32_msca_set_shdram_flag(io, ch, 0, st->w302, NULL);
	t32_ms_sw(io, 0xf8000, 257);
	if (force | c->pending)
		t32_msca_ch_curve_write_w(c, &cnt, io->w, &io->n);
	t32_msca_cfg_lines(ops, io, st, ch, &cnt);
	t32_msca_set_shdram_flag(io, ch, 1, st->w302, &cnt);
	t32_ms_sw(io, 0xf8000, 0x01010102);
	if (cnt)
		t32_ms_sw(io, 0xf8100, (u32)(cnt - 1) << 4 | 1);
	ops->unlock();
}

/* par = the channel's parameter block (c->par), flag byte par + 172 + set */
static inline void t32_msca_chx_cfg_load(const struct t32_msca_cfg_ops *ops, struct t32_ms_io *io,
					 struct t32_msca_cfg_st *st, u32 ch, u32 set)
{
	struct t32_msca_ch *c = st->c;

	ch &= 0xff;
	set &= 0xff;
	c->par[172 + set] = 1;
	ops->lock();
	t32_msca_para_calc(c, st->sb, set);
	t32_msca_curve_calc(c, st->sb, set);
	t32_msca_scaling_algorithm_w(c, io->w, &io->n);
	ops->unlock();
	t32_msca_cfg_shd(ops, io, st, ch, 1);
	ops->lock();
	t32_msca_init_chx_cfg_w(c, st->sb, set, io->w, &io->n);
	if (t32_ms_tr(io, ch, 11, 256) == 5 && set == 0) {
		t32_ms_tw(io, ch, 56, 0xf0140, 4096);
		t32_ms_tw(io, ch, 64, 0xf0160, 4096);
	}
	t32_ms_tw(io, ch, 13, 0xf0010, 1);
	ops->unlock();
}

#endif /* TX_ISP_T32_MSCA_CFG_H */
