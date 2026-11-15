/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "irqA": AE / AWB statistics interrupts (driver/t32/tx_isp_t33_irqA.h) with the
 * vendor calling convention.  Callees are extern with the vendor names (recorded as CALLS by the oracle).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_irqA.h"

extern int tisp_normal_ae_interrupt_static(u32 ch);
extern int tisp_normal_ae_interrupt_hist(u32 ch);
extern u32 system_reg_read(u32 reg);
extern int tisp_event_push(u32 ch, void *ev);
extern int tisp_rcfg_module_cfg_trig(u32 ch, u32 id);
extern int tisp_awb_set_hardware_param(u32 ch);

u32 t33_num_stat[4];
u32 t33_awb_info[4];
u8 t33_st_init_par[4032];
u8 t33_awb_refresh_flag[4];
u8 t33_awbg_refresh_flag[4];

int bo_tisp_ae_interrupt_hist(u32 ch) { return t33_irqa_ae_hist(ch, tisp_normal_ae_interrupt_static); }
int bo_tisp_ae_interrupt_static(u32 ch) { return t33_irqa_ae_static(ch, tisp_normal_ae_interrupt_hist); }

int bo_tisp_awb_interrupt_static(u32 ch)
{
	struct t33_irqa_ops o;

	o.reg_read = system_reg_read;
	o.event_push = tisp_event_push;
	o.trig = tisp_rcfg_module_cfg_trig;
	o.set_hw = tisp_awb_set_hardware_param;
	return t33_awb_int_static(ch, t33_num_stat, t33_awb_info, t33_st_init_par, t33_awb_refresh_flag,
				  t33_awbg_refresh_flag, &o);
}
