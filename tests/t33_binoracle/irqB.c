/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "irqB": frame-done IRQ handlers with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_irqB.h"

u8 stIspInitPar[4032];
u8 tispCscRefresh __attribute__((aligned(4)));
u8 tispGammaRefresh __attribute__((aligned(4)));
u8 tispLscRefresh __attribute__((aligned(4)));
u32 gsm_count[4];
u32 gamma_count[4];
u32 lsc_count[4];
u8 *lsc_para[4];

int bo_tisp_csc_fra_down_interrupt(u32 ch) { return t33_csc_frame_down(ch); }
int bo_tisp_gsm_irq_frmae_done(u32 ch) { return t33_gsm_frame_done(ch); }
int bo_tisp_gamma_irq_frame_done(u32 ch) { return t33_gamma_frame_done(ch); }
int bo_tisp_lsc_irq_frame_done(u32 ch) { return t33_lsc_frame_done(ch); }
