/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 day/night parameter switches: tisp_ccm_switch_para, tisp_csccr_switch_para,
 * tisp_defog_switch_para, tisp_lsc_switch_para.  The block callees (interpolation, register
 * writes, list refresh) are ported / proven elsewhere and are called by their vendor names.
 * Per-channel block states are the vendor's pointer arrays (tparamsP, ccm_para, csccrInfo,
 * defog_info, lsc_para) indexed by channel.
 */
#ifndef TX_ISP_T33_SWMID_H
#define TX_ISP_T33_SWMID_H

#include "tx_isp_t33_blk.h"

#ifndef T33_SW_GLUE	/* the driver maps the CCM names (tx_isp_t32_hw.c, before t33_sw_tone) */
extern int tisp_ccm_interp_by_ct(u32 idx, u32 ct, u32 force);
extern int tisp_ccm_interp_by_ev(u32 idx, u32 pad, u32 ev_lo, u32 ev_hi, u32 force);
extern int tisp_ccm_no_interp(u32 idx);
extern int tisp_ccm_calc_para_refresh(u32 idx);
extern int tisp_ccm_write_reg(u32 idx, u32 mode);
#endif
extern int tisp_csccr_para_refresh_by_mode(u32 mode, u32 zero);
extern int tisp_csccr_sync_api_para(u32 idx, u32 one);
extern int tisp_csccr_bin_par_update(u32 idx);
extern int t33_swmid_reg_write(u32 idx, u32 list, u32 slot, u32 val) __asm__("tisp_reg_write_t33");
extern int t33_swmid_printf(int lvl, const char *fmt, ...) __asm__("isp_printf");
extern int defog_count_weight35abc(u32 idx, u32 a, u32 b, u32 c, u8 *p0, u8 *p1, u8 *p2, u8 *p3,
				   u8 *p4, u8 *p5, u8 *p6);
extern int tisp_defog_write_reg_para1(u32 idx);
extern int tisp_defog_interp_trans_k(u32 idx, u32 k, u8 *p0, u8 *p1, u8 *p2, u8 *p3, u8 *p4, u8 *p5,
				     u8 *p6);
extern int tisp_defog_update_reg_para2(u32 idx);
extern int tisp_defog_write_reg_para2(u32 idx);
extern int tisp_defog_write_reg_para3(u32 idx, u32 zero);
extern int tisp_lsc_lut_valid_check(u32 h, u32 w, u32 mh, u32 mw, u8 *p0, u8 *p1, u8 *p2, u8 *p3,
				    u8 *p4, u8 *p5, u8 *p6);
extern int tisp_lsc_interp_by_tgain(u32 idx, u32 gain, u32 force);
extern int tisp_lsc_interp_by_ct(u32 idx, u32 ct, u32 force);
extern int func_lsc_para_update(u32 idx);
extern int tisp_lsc_write_reg(u32 idx, u32 mode);

static inline u32 t33_swmid_w(const void *p)
{
	return (u32)(unsigned long)p;
}

static inline u8 *t33_swmid_p(u32 w)
{
	return (u8 *)(unsigned long)w;
}

static inline void t33_swmid_put16(u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static inline void t33_swmid_put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* tisp_ccm_switch_para */
static inline int t33_ccm_switch_para(u32 idx, u8 *const *tp, u32 *const *ccm_para, u8 *refresh)
{
	u8 *p = tp[idx];
	u32 *info = ccm_para[idx];

	info[0] = t33_swmid_w(p + 21536);
	p[23419] = 0;
	tisp_ccm_interp_by_ct(idx, info[4], 1);
	info = ccm_para[idx];
	tisp_ccm_interp_by_ev(idx, info[4], info[2], info[3], 1);
	tisp_ccm_no_interp(idx & 0xff);
	tisp_ccm_calc_para_refresh(idx);
	tisp_ccm_write_reg(idx, 0);
	*refresh = 1;
	return 0;
}

/* tisp_csccr_switch_para */
static inline int t33_csccr_switch_para(u32 a0, u8 *const *tp, u32 *const *csccr_info, const u8 *cfg_mode,
					u8 *csccr_refresh)
{
	u32 idx = a0 & 0xff;
	u32 *info = csccr_info[idx];
	u8 *sec;
	u32 q0, q1, b8, b9, b10;

	info[3] = t33_swmid_w(tp[idx] + 17880);
	sec = t33_swmid_p(info[3]);
	sec[14] = 1;
	tisp_csccr_para_refresh_by_mode(sec[4], 0);
	tisp_csccr_sync_api_para(idx, 1);
	tisp_csccr_bin_par_update(idx);
	info = csccr_info[idx];
	q0 = (u16)info[0];
	q1 = (u16)info[1];
	b8 = info[2] & 0xff;
	b9 = (info[2] >> 8) & 0xff;
	b10 = (info[2] >> 16) & 0xff;
	t33_swmid_reg_write(idx, 22, *cfg_mode ? 3 : 0x14040, q1 & 0x7ff);
	t33_swmid_reg_write(idx, 22, *cfg_mode ? 4 : 0x14044, b10 << 8 | b8 << 16 | b9);
	t33_swmid_reg_write(idx, 22, *cfg_mode ? 6 : 0x14050, q0 & 0x7ff);
	t33_swmid_reg_write(idx, 22, *cfg_mode ? 1 : 0x14010, 3);
	*csccr_refresh = 1;
	return 0;
}

/* tisp_defog_switch_para */
static inline int t33_defog_switch_para(u32 a0, u32 *const *defog_info)
{
	u32 idx = a0 & 0xff;
	u32 *info = defog_info[idx];
	u8 *s0 = t33_swmid_p(info[3]);
	u8 *zero = t33_swmid_p(info[4]);
	u8 s5 = s0[433];

	if (!(info[8] == t32_tp_le16(s0 + 192) && info[9] == t32_tp_le16(s0 + 190))) {
		defog_count_weight35abc(idx, info[8], info[9], s5, s0 + 80, s0 + 128, s0 + 434, s0 + 466,
					s0 + 498, s0 + 530, s0 + 562);
		info = defog_info[idx];
		t33_swmid_put16(s0 + 190, (u16)info[9]);
		t33_swmid_put16(s0 + 192, (u16)info[8]);
	}
	info = defog_info[idx];
	((u8 *)info)[48] = s5;
	tisp_defog_write_reg_para1(idx);
	if (!s0[594])
		t33_swmid_put32(s0 + 4, info[6]);
	tisp_defog_interp_trans_k(idx, t33_le32(s0 + 4), s0 + 8, s0 + 614, s0 + 623, s0 + 632, s0 + 641,
				  s0 + 650, s0 + 260);
	tisp_defog_update_reg_para2(idx);
	tisp_defog_write_reg_para2(idx);
	memset(zero + 2458, 128, 180);
	memset(zero + 2638, 128, 180);
	memset(zero + 2818, 128, 180);
	memset(zero + 2998, 0, 180);
	tisp_defog_write_reg_para3(idx, 0);
	return 0;
}

/* tisp_lsc_switch_para */
static inline int t33_lsc_switch_para(u32 idx, u8 *const *tp, u32 *const *lsc_para)
{
	u8 *p = tp[idx];
	u32 *info = lsc_para[idx];
	u32 *pa;
	u8 *p2;

	info[0] = t33_swmid_w(p + 17488);
	pa = (u32 *)t33_swmid_p(info[1]);
	p2 = t33_swmid_p(info[2]);
	tisp_lsc_lut_valid_check(pa[0], pa[1], p[17576], p[17577], p2 + 8, p2 + 9, p2 + 10, p2 + 11, p2,
				 p2 + 21, p2 + 4);
	if (!p2[21]) {
		t33_swmid_printf(1, "%s[%d]: lsc mesh %d x %d invalid (cell %d x %d), using 80\n", __func__,
				 1039, pa[0], pa[1], p[17576], p[17577]);
		p[17576] = 80;
		p[17577] = 80;
		tisp_lsc_lut_valid_check(pa[0], pa[1], p[17576], p[17577], p2 + 8, p2 + 9, p2 + 10, p2 + 11,
					 p2, p2 + 21, p2 + 4);
	}
	pa = (u32 *)t33_swmid_p(lsc_para[idx][1]);
	tisp_lsc_interp_by_tgain(idx, pa[5], 1);
	pa = (u32 *)t33_swmid_p(lsc_para[idx][1]);
	tisp_lsc_interp_by_ct(idx & 0xff, (u16)pa[2], 1);
	func_lsc_para_update(idx);
	tisp_lsc_write_reg(idx, 0);
	return 0;
}

#endif /* TX_ISP_T33_SWMID_H */
