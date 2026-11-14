/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) frame-done IRQ handlers: a module state byte selects
 * "trigger the rcfg list" (tisp_rcfg_module_cfg_trig) or "write registers"
 * (the block's write_reg), then the state byte is cleared.
 *
 * The vendor keeps the state bytes in .bss (tispBlcRefresh, tispDefogRefresh,
 * tispClmRefresh) and gates the BLC/defog handlers on the word at byte
 * offset 904 of stIspInitPar being 1.  The callees are ported elsewhere
 * (tx_isp_t33_blk.h, tx_isp_t33_clm.h, tx_isp_t32_hw.c) and are declared
 * extern here.
 */
#ifndef TX_ISP_T33_IRQ_H
#define TX_ISP_T33_IRQ_H

#include "tx_isp_t33_blk.h"

/* module state bytes (vendor .bss) */
extern u8 tispBlcRefresh __attribute__((aligned(4)));
extern u8 tispDefogRefresh __attribute__((aligned(4)));
extern u8 tispClmRefresh __attribute__((aligned(4)));
/* stIspInitPar byte image; the mode flag is the LE u32 at byte offset 904 */
extern u8 stIspInitPar[4096];

/* callees ported elsewhere */
extern void tisp_rcfg_module_cfg_trig(u32 a0, u32 id);
extern void tisp_blc_write_reg(void);
extern void tisp_clm_t33_write_reg(void);

/* tisp_blc_t33_frame_done_interrupt */
static inline int t33_blc_frame_done(u32 a0)
{
	if (t33_le32(stIspInitPar + 904) != 1)
		return 0;
	if (tispBlcRefresh == 1) {
		tisp_rcfg_module_cfg_trig(a0 & 0xff, 4);
		tispBlcRefresh = 0;
	} else if (tispBlcRefresh == 2) {
		tisp_blc_write_reg();
	}
	return 0;
}

/* tisp_defog_t33_frame_done_interrupt */
static inline int t33_defog_frame_done(u32 a0)
{
	if (t33_le32(stIspInitPar + 904) != 1)
		return 1;
	if (tispDefogRefresh == 1) {
		tisp_rcfg_module_cfg_trig(a0 & 0xff, 12);
		tispDefogRefresh = 0;
	}
	return 1;
}

/* tisp_clm_irq_frame_done */
static inline int t33_clm_frame_done(u32 a0)
{
	if (tispClmRefresh == 1) {
		tisp_rcfg_module_cfg_trig(a0 & 0xff, 19);
		tispClmRefresh = 0;
	} else if (tispClmRefresh == 2) {
		tisp_clm_t33_write_reg();
	}
	return 1;
}

#endif /* TX_ISP_T33_IRQ_H */
