/* SPDX-License-Identifier: GPL-2.0 */
/*
 * binary oracle, block "t32-modctl": tisp_s_module_control of the vendor tx-isp-t32.ko (T32_OEM_KO) vs
 * driver/t32/tx_isp_t32_api_ctl.h (openimp #717): the top enable word 0x40, MDNS restart on bit 11 (the vendor's
 * tisp_mdns_start / tisp_mdns_reg_trig run unchanged on its side), tisp_lce_top_change_state on bits 4 / 10 (a recorded call here, its
 * own oracle is t32-lcetcs).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_api_ctl.h"

u8 cfgMode;
extern void tisp_reg_write(u32 ch, u32 reg, u32 val);
extern u32 tisp_reg_read(u32 ch, u32 reg);
extern void tisp_lce_top_change_state(u32 ch);

/* tisp_mdns_start (0xd018 = 1, 0xd014 = 1) and tisp_mdns_reg_trig (0xd014 = 1), both through tisp_reg_write */
static u32 bo_rd(u32 ch, u32 a) { return tisp_reg_read(ch, a); }
static int bo_wr2(u32 ch, u32 a, u32 v) { tisp_reg_write(ch, a, v); return 0; }
static int bo_mstart(u32 ch) { tisp_reg_write(ch, 0xd018, 1); tisp_reg_write(ch, 0xd014, 1); return 0; }
static int bo_lce(u32 ch) { tisp_lce_top_change_state(ch); return 0; }
static int bo_mtrig(u32 ch) { tisp_reg_write(ch, 0xd014, 1); return 0; }
static void bo_warn(void) { }
static const struct t32_api_ctl_ops bo_cops = { bo_rd, bo_wr2, bo_warn, bo_mstart, bo_lce, bo_mtrig };

void bo_tisp_s_module_control(u32 ch, u32 v)
{
	t32_s_module_control(&bo_cops, cfgMode, ch, v);
}
