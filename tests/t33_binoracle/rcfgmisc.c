/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfgmisc": the rcfg trigger / reset / interrupt
 * switch helpers (driver/t32/tx_isp_t33_rcfgmisc.h) with the calling
 * convention of the vendor functions they port.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfgmisc.h"

u32 g_rcfg_trig_start_cnt[4];
u8 stIspInitPar[4032];
u8 stModuleAttr[7168];
u8 *rcfg_buf_info[1];

void bo_lib_tisp_rcfg_cfg_trig(u32 id) { t33_rcfg_cfg_trig(id); }
void bo_tisp_rcfg_t33_reset_reg_cfg(void) { t33_rcfg_reset_reg_cfg(); }
void bo_tisp_rcfg_t33_switch_interrupt(u32 id) { t33_rcfg_switch_interrupt(id); }
