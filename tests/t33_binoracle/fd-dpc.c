/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "fd-dpc": tisp_dpc_t33_frame_done_interrupt with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_fd_dpc.h"

extern int tisp_reg_write_t33(u32 ch, u32 module, u32 reg, u32 val);
extern int tisp_rcfg_get_dma_state(u32 id);
extern int tisp_rcfg_module_cfg_trig(u32 ch, u32 module);

u8 stIspInitPar[4032] __attribute__((aligned(4)));
u8 tispDpcRefresh;
u8 dpcCfgMode;
u8 *dpcInfo[4];

int bo_tisp_dpc_t33_frame_done_interrupt(u32 a0)
{
	struct t33_dpc_hw hw;

	hw.reg_write = tisp_reg_write_t33;
	hw.dma_state = tisp_rcfg_get_dma_state;
	hw.cfg_trig = tisp_rcfg_module_cfg_trig;
	return t33_dpc_frame_done(&hw, a0, t33_le32(stIspInitPar + 904), &tispDpcRefresh, dpcCfgMode,
				  dpcInfo[a0 & 0xff]);
}
