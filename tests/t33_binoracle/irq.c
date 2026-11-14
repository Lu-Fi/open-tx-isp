/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "irq": frame-done IRQ handlers
 * (driver/t32/tx_isp_t33_irq.h) with the vendor calling convention, built
 * for mipsel and run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_irq.h"

/* module statics (vendor .bss) */
u8 tispBlcRefresh __attribute__((aligned(4)));
u8 tispDefogRefresh __attribute__((aligned(4)));
u8 tispClmRefresh __attribute__((aligned(4)));
u8 stIspInitPar[4096];

/* callees ported elsewhere / hardware: declared extern, never run */
extern void tisp_rcfg_module_cfg_trig(u32 a0, u32 id);
extern void tisp_blc_write_reg(void);
extern void tisp_clm_t33_write_reg(void);

int bo_tisp_blc_t33_frame_done_interrupt(u32 a0) { return t33_blc_frame_done(a0); }
int bo_tisp_defog_t33_frame_done_interrupt(u32 a0) { return t33_defog_frame_done(a0); }
int bo_tisp_clm_irq_frame_done(u32 a0) { return t33_clm_frame_done(a0); }
