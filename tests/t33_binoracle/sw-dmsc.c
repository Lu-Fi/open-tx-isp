/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "sw-dmsc": tisp_dmsc_switch_para */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_swdmsc.h"

extern void tisp_dmsc_bin_par_update(u32 ch);
extern u32 tisp_simple_intp_uint8_t(u32 i, u32 f, const u8 *t);
extern u32 tisp_simple_intp_uint16_t(u32 i, u32 f, const u8 *t);
extern u32 tisp_ratio(u32 a, u32 b, u32 c);
extern void func_dmsc_para_update(u32 ch);
extern u32 tisp_rcfg_get_dma_state(u32 blk);
extern void tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);

u32 dmscInfo[4];
u32 tparamsP[4];
u8 dmscCfgMode;
u8 tispDmscRefresh;

int bo_tisp_dmsc_switch_para(u32 ch)
{
	struct t33_swdmsc_ops o;

	o.bin_par_update = tisp_dmsc_bin_par_update;
	o.intp8 = tisp_simple_intp_uint8_t;
	o.intp16 = tisp_simple_intp_uint16_t;
	o.ratio = tisp_ratio;
	o.para_update = func_dmsc_para_update;
	o.dma_state = tisp_rcfg_get_dma_state;
	o.reg_write = tisp_reg_write_t33;
	return t33_dmsc_switch_para(&o, ch, (u32 *)(uintptr_t)dmscInfo[ch], tparamsP[ch], dmscCfgMode,
				    &tispDmscRefresh);
}
