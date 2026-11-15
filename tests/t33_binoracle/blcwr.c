/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "blcwr": tisp_blc_write_reg (driver/t32/tx_isp_t33_blcsw.h t33_blc_write_reg) with the
 * vendor calling convention (spec: blcwr.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_blcsw.h"

extern int tisp_rcfg_get_dma_state(u32 mod);
extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
extern int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);

struct t33_blc_info *blc_para[4];
u8 blcCfgMode;
u8 tispBlcRefresh;

static const struct t33_blc_hw hw = { tisp_rcfg_get_dma_state, tisp_reg_write_t33, tisp_rcfg_module_cfg_trig };

int bo_tisp_blc_write_reg(u32 ch)
{
	return t33_blc_write_reg(ch, blc_para[ch], blcCfgMode, &tispBlcRefresh, &hw);
}
