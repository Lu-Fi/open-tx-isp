/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) CCM on the vendor block state (openimp #99, wiring group 13d-1).
 *
 * - ccm_para[0]: the vendor's 0x290-byte block = t33_ccm_v (tx_isp_t32_hw.c): word 0 the section (tparamsP +
 *   0x5420 = t33_ccm_sec, the bin's section 12 copied at every bin parse, u32 aligned), 2 / 3 the EV words,
 *   4 / 5 the CT and its segment, 0x54.. / 0x5f.. / 0x68.. the matrices, 0x99.. the API matrix (ccm_ap);
 *   stLutInfo's CCM knots = t33_ccm_lut (the bin's payload behind the section).
 * - The vendor callees on it: tisp_ccm_interp_by_ct (t33_ccm_ct, with the not-forced gate: same even segment
 *   or |CT - last| <= sec + 0x738 -> 1 and nothing changed), tisp_ccm_interp_by_ev (t33_ccm_ev, not forced:
 *   |EV - last| < sec + 0x3c -> 1), tisp_ccm_no_interp (proven port t33_ccm_no_interp, block ccmsm),
 *   tisp_ccm_calc_para_refresh (t33_ccm_calc; func_ccm_calc_single_ks = proven t33_ccm_single_ks),
 *   tisp_ccm_write_reg (mode 0 / 2: the slots of list 10, ccmCfgMode 1).
 * - tisp_ccm_init (t33_ccm_attach): section copy with bytes 0x75a / 0x75b = 0, CT 5000 K / EV of the init,
 *   interp_by_ct (forced), interp_by_ev (forced), no_interp, calc_para_refresh, write_reg; the list layout
 *   itself still comes from the template (t33_ccm_init, tisp_rcfg_ccm_reg_init checks it).
 * - tisp_ccm_switch_para: the proven port t33_ccm_switch_para (block swmid) on ccm_para.
 * - tisp_ccm_ct_update / tisp_ccm_ev_update and tispCcmRefresh: refresh 1 when list 10 is idle, else 2;
 *   tisp_ccm_irq_frame_done (system irq 45 = interrupt status 0x300a8 bit 13) starts list 10.  The EV update
 *   and the frame done are the proven glue of block ccmP (tx_isp_t33_ccmP.h).
 *
 * Only the T33 build includes this file (tx_isp_t32_hw.c, after t33_ccm_v and t33_mod_buf).
 */
#ifndef TX_ISP_T33_CCMV_H
#define TX_ISP_T33_CCMV_H

#define T33_TP_OFF_CCM		21536U	/* tparamsP + 0x5420: the CCM section */
#define T33_CCM_IRQ_BIT		13	/* system irq 45 */

static u32 t33_ccm_sec32[T33_CCM_SEC_BYTES / 4];
#define t33_ccm_sec	((u8 *)t33_ccm_sec32)
static struct {
	const u8 *lut;
	u32 bytes;
} t33_ccm_lut;				/* stLutInfo[0]: the CCM knots */
static u32 ccm_para[4];			/* the vendor's block pointers: [0] = t33_ccm_v */
static u32 *t33_ccm_parap[4];		/* the same as a pointer array (block swmid) */
static u8 tispCcmRefresh;
u32 t33_ccm_runs[5];			/* init, CT writes, EV writes, switches, frame dones with a refresh */

static u32 *t33_ccm_blk(u32 idx)
{
	return (u32 *)(unsigned long)ccm_para[idx & 3];
}

static const u8 *t33_ccm_bsec(const u32 *v)
{
	return (const u8 *)(unsigned long)v[0];
}

/* tisp_ccm_interp_by_ct: 0 with the matrix interpolated at ct, 1 when the not-forced gate keeps the old one */
static int tisp_ccm_interp_by_ct(u32 idx, u32 ct, u32 force)
{
	static u32 save[T33_CCM_WORDS];
	u32 *v = t33_ccm_blk(idx);
	const u8 *sec;
	int d;

	if (!v || !t33_ccm_lut.lut)
		return 1;
	sec = t33_ccm_bsec(v);
	if (!force)
		memcpy(save, v, sizeof(save));
	t33_ccm_ct(v, sec, t33_ccm_lut.lut, ct);
	if (!force) {
		d = (int)(ct - save[4]);
		if ((v[5] == save[5] && !(v[5] & 1)) || (d < 0 ? -d : d) <= (int)t32_tp_le16(sec + 0x738)) {
			memcpy(v, save, sizeof(save));
			return 1;
		}
	}
	return 0;
}

/* tisp_ccm_interp_by_ev(idx, pad, EV low, EV high, force): 0 with the EV tables interpolated */
static int tisp_ccm_interp_by_ev(u32 idx, u32 pad, u32 ev_lo, u32 ev_hi, u32 force)
{
	u32 *v = t33_ccm_blk(idx);
	unsigned long long a, b, d;

	(void)pad;
	if (!v)
		return 1;
	if (!force) {
		a = (unsigned long long)ev_hi << 32 | ev_lo;
		b = (unsigned long long)v[3] << 32 | v[2];
		d = a > b ? a - b : b - a;
		if ((d >> 32 ? 0xffffffffU : (u32)d) < t33_le32(t33_ccm_bsec(v) + 0x3c))
			return 1;
	}
	return t33_ccm_ev(v, t33_ccm_bsec(v), ev_lo, ev_hi);
}

static int tisp_ccm_no_interp(u32 idx)
{
	u32 *v = t33_ccm_blk(idx);

	if (v)
		t33_ccm_no_interp(v, (const u32 *)(const void *)t33_ccm_bsec(v));
	return 0;
}

static int tisp_ccm_calc_para_refresh(u32 idx)
{
	u32 *v = t33_ccm_blk(idx);

	if (v)
		t33_ccm_calc(v);
	return 0;
}

/* tisp_ccm_write_reg(idx, mode): modes 0 / 2 store the block's words into the list 10 slots */
static int tisp_ccm_write_reg(u32 idx, u32 mode)
{
	u32 *v = t33_ccm_blk(idx), *l;

	if (!v || (mode & 0xfd) || !(t33_mod_built & (1ULL << T33_MOD_CCM)) || !(l = t33_mod_buf(T33_MOD_CCM)))
		return 0;
	t33_ccm_store(v, l);
	return 0;
}

/* the proven glue of block ccmP: tisp_ccm_ev_update / api_set / irq_frame_done on these names */
#define T33_CCMP_GLUE
#define stIspInitPar	((const u8 *)t33_st_isp_init_par_mode)
#define tisp_ccm_interp_by_ev(c, s, lo, hi, f)	tisp_ccm_interp_by_ev(c, 0, lo, hi, f)
#define tisp_ccm_interp_by_ct(c, s, f)		tisp_ccm_interp_by_ct(c, (u32)(unsigned long)(s), f)
#include "tx_isp_t33_ccmP.h"
#undef tisp_ccm_interp_by_ct
#undef tisp_ccm_interp_by_ev
#undef stIspInitPar

/*
 * tisp_ccm_init (channel 0): sec / lut / lutbytes the bin's section 12 and its knots, l / cap list 10.
 * Returns the list length (t33_mod_done), -1 when the payload does not hold the knots.
 */
static int t33_ccm_attach(const u8 *sec, const u8 *lut, u32 lutbytes, u32 ct, u32 ev_lo, u32 *l, u32 cap)
{
	unsigned long flags;
	int len;

	local_irq_save(flags);
	ccm_para[0] = 0;
	t33_ccm_parap[0] = NULL;
	tispCcmRefresh = 0;
	local_irq_restore(flags);
	memcpy(t33_ccm_sec, sec, T33_CCM_SEC_BYTES);
	t33_ccm_sec[0x75a] = t33_ccm_sec[0x75b] = 0;
	/* the template list, ccm_sp / ip / ap, interp_by_ct / _by_ev (forced), no_interp, calc, write_reg */
	len = t33_ccm_init(t33_ccm_sec, lut, lutbytes, ct, ev_lo, 0, t33_ccm_v, l, cap);
	if (len <= 0)
		return len;
	t33_ccm_lut.lut = lut;
	t33_ccm_lut.bytes = lutbytes;
	t33_ccm_v[0] = (u32)(unsigned long)t33_ccm_sec;
	t33_ccm_parap[0] = t33_ccm_v;
	ccm_para[0] = (u32)(unsigned long)t33_ccm_v;
	t33_ccm_runs[0]++;
	return len;
}

/* the bin's new section / knots at a bin switch (tisp_bin_data_parse into tparamsP / stLutInfo); 0 when usable */
static int t33_ccm_new_bin(const u8 *sec, const u8 *lut, u32 lutbytes)
{
	if (!ccm_para[0] || !sec[0x75e] || lutbytes < sec[0x75e] * 40U)
		return -1;
	memcpy(t33_ccm_sec, sec, T33_CCM_SEC_BYTES);
	t33_ccm_lut.lut = lut;
	t33_ccm_lut.bytes = lutbytes;
	return 0;
}

/* tisp_ccm_ct_update */
static void t33_ccm_ct_update(u32 ct)
{
	unsigned long flags;

	if (!ccm_para[0])
		return;
	local_irq_save(flags);
	if (!tisp_ccm_interp_by_ct(0, ct, 0)) {
		tisp_ccm_write_reg(0, 0);
		t33_ccm_runs[1]++;
	}
	tispCcmRefresh = tisp_rcfg_get_dma_state(T33_MOD_CCM) ? 2 : 1;
	local_irq_restore(flags);
}

/* tisp_ccm_ev_update (block ccmP) */
static void t33_ccm_ev_upd(u32 lo, u32 hi)
{
	unsigned long flags;

	if (!ccm_para[0])
		return;
	local_irq_save(flags);
	if (!t33_ccm_ev_update_glue(0, t33_ccm_sec, lo, hi))
		t33_ccm_runs[2]++;
	local_irq_restore(flags);
}

/* tisp_ccm_irq_frame_done (system irq 45, block ccmP; interrupt context) */
static void t33_ccm_fd(void)
{
	if (!ccm_para[0])
		return;
	if (tispCcmRefresh)
		t33_ccm_runs[4]++;
	t33_ccm_irq_frame_done_glue(0);
}

#endif /* TX_ISP_T33_CCMV_H */
