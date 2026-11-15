/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) CCM block glue, from the vendor tx-isp-t33.ko:
 *  - tisp_ccm_ev_update: long EV event -> interp_by_ev, then
 *    calc_para_refresh + write_reg, and the refresh flag;
 *  - tisp_ccm_api_set: per-channel set (memcpy of the 40-byte manual
 *    matrix, interp_by_ct, interp_by_ev, no_interp, calc_para_refresh,
 *    write_reg) and the refresh flag;
 *  - tisp_ccm_irq_frame_done: the frame-done refresh of list 10.
 */
#ifndef TX_ISP_T33_CCMP_H
#define TX_ISP_T33_CCMP_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_ccm.h"
#include "tx_isp_t33_ccmsm.h"

extern u8 tispCcmRefresh;
extern u32 ccm_para[4];
extern u8 stIspInitPar[4032];

/* callees recorded by the oracle / proven elsewhere */
extern int tisp_ccm_interp_by_ev(u32 ch, const u8 *sec, u32 lo, u32 hi, u32 force);
extern void tisp_ccm_calc_para_refresh(u32 ch);
extern void tisp_ccm_write_reg(u32 ch, u32 slot);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern void tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);
extern int tisp_ccm_interp_by_ct(u32 ch, const u8 *sec, u32 force);
extern void tisp_ccm_no_interp(u32 ch);

/* tisp_ccm_ev_update: EV clamped to 0x100 << 32, interp_by_ev (not forced);
 * on success calc_para_refresh + write_reg; tispCcmRefresh = 1 when the
 * DMA is idle, 2 otherwise.  Returns interp_by_ev's result. */
static inline int t33_ccm_ev_update_glue(u32 ch, const u8 *sec, u32 lo, u32 hi)
{
	int r;

	if (hi > 0x100 || (hi == 0x100 && lo)) {
		lo = 0;
		hi = 0x100;
	}
	r = tisp_ccm_interp_by_ev(ch, sec, lo, hi, 0);
	if (!r) {
		tisp_ccm_calc_para_refresh(ch);
		tisp_ccm_write_reg(ch, 0);
	}
	tispCcmRefresh = tisp_rcfg_get_dma_state(10) == 0 ? 1 : 2;
	return r;
}

/* tisp_ccm_api_set: p = ccm_para[ch]; word 0 of p points at a block whose
 * byte 1883 is set to 2; p+20 = 0xff; the 40 bytes at src go to p+612. */
static inline int t33_ccm_api_set_glue(u32 ch, const u8 *src)
{
	u8 *p = (u8 *)(unsigned long)ccm_para[ch];
	u8 *q = (u8 *)(unsigned long)t33_le32(p);
	const u8 *sec;

	q[1883] = 2;
	*(u32 *)(void *)(p + 20) = 0xff;
	memcpy(p + 612, src, 40);

	sec = (const u8 *)(unsigned long)t33_le32(p + 16);
	tisp_ccm_interp_by_ct(ch, sec, 1);
	tisp_ccm_interp_by_ev(ch, sec, t33_le32(p + 8), t33_le32(p + 12), 1);
	tisp_ccm_no_interp(ch & 0xff);
	tisp_ccm_calc_para_refresh(ch);
	tisp_ccm_write_reg(ch, 0);

	tispCcmRefresh = 1;
	return 0;
}

/* tisp_ccm_irq_frame_done: trigger list 10 when a refresh is pending. */
static inline int t33_ccm_irq_frame_done_glue(u32 ch)
{
	if (t33_le32(stIspInitPar + 904) != 1)
		return 1;

	if (tispCcmRefresh == 1) {
		tisp_rcfg_module_cfg_trig(ch & 0xff, 10);
		tispCcmRefresh = 0;
	} else if (tispCcmRefresh == 2) {
		if (tisp_rcfg_get_dma_state(10) == 0) {
			tisp_rcfg_module_cfg_trig(ch & 0xff, 10);
			tispCcmRefresh = 0;
		}
	}
	return 1;
}

#endif /* TX_ISP_T33_CCMP_H */
