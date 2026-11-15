/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 frame-done IRQ handlers (openimp #99), binary oracle block "irqB": CSC frame-down, GSM, gamma and
 * LSC frame-done.  The callees are ported elsewhere and declared extern here; the module state is the
 * vendor's .bss (counters per channel, refresh bytes, lsc_para pointers, stIspInitPar word 904).
 */
#ifndef TX_ISP_T33_IRQB_H
#define TX_ISP_T33_IRQB_H

#include "tx_isp_t33_blk.h"

extern u8 stIspInitPar[];
extern u8 tispCscRefresh __attribute__((aligned(4)));
extern u8 tispGammaRefresh __attribute__((aligned(4)));
extern u8 tispLscRefresh __attribute__((aligned(4)));
extern u32 gsm_count[4];
extern u32 gamma_count[4];
extern u32 lsc_count[4];
extern u8 *lsc_para[4];

extern void tisp_rcfg_module_cfg_trig(u32 a0, u32 id);
extern int tisp_t33_csc_write_reg(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 id);
extern int tisp_gamma_write_lut_rgb(u32 ch);
extern int tisp_lsc_write_reg(u32 ch, u32 mode);

/* tisp_csc_fra_down_interrupt */
static inline int t33_csc_frame_down(u32 ch)
{
	if (t33_le32(stIspInitPar + 904) != 1)
		return 1;
	if (tispCscRefresh == 1) {
		tisp_rcfg_module_cfg_trig(ch & 0xff, 13);
		tispCscRefresh = 0;
	} else if (tispCscRefresh == 2) {
		tisp_t33_csc_write_reg(ch & 0xff);
	}
	return 1;
}

/* tisp_gsm_irq_frmae_done */
static inline int t33_gsm_frame_done(u32 ch)
{
	gsm_count[ch] += 1;
	return 1;
}

/* tisp_gamma_irq_frame_done */
static inline int t33_gamma_frame_done(u32 ch)
{
	gamma_count[ch] = (gamma_count[ch] + 1) & 0xffffff;
	if (tispGammaRefresh == 1) {
		tisp_rcfg_module_cfg_trig(ch & 0xff, 11);
		tispGammaRefresh = 0;
	} else if (tispGammaRefresh == 2) {
		if (tisp_rcfg_get_dma_state(11) == 0) {
			tisp_gamma_write_lut_rgb(ch);
			tisp_rcfg_module_cfg_trig(ch & 0xff, 11);
			tispGammaRefresh = 0;
		}
	}
	return 1;
}

/* tisp_lsc_irq_frame_done */
static inline int t33_lsc_frame_done(u32 ch)
{
	u8 *p;

	lsc_count[ch] = (lsc_count[ch] + 1) & 0xffff;
	p = lsc_para[ch];
	if (p[24]) {
		p[24] = 0;
		tisp_lsc_write_reg(ch, 2);
	}
	if (t33_le32(stIspInitPar + 904) != 1)
		return 1;
	if (tispLscRefresh == 1) {
		tisp_rcfg_module_cfg_trig(ch & 0xff, 6);
		tispLscRefresh = 0;
	} else if (tispLscRefresh == 2) {
		tisp_lsc_write_reg(ch, 2);
	}
	return 1;
}

#endif /* TX_ISP_T33_IRQB_H */
