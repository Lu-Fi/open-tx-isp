/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "fd-ysp": ysp frame-done handler with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_fd_ysp.h"

extern void tisp_reg_write_t33(u32 ch, u32 module, u32 reg, u32 val);
extern void tisp_mdns_decode_reg_write(u32 ch, const u8 *par);
extern void tisp_rcfg_module_cfg_trig(u32 ch, u32 module);
extern u32 tisp_rcfg_get_dma_state(u32 module);

u8 *o_yspInfo[4];
u8 o_yspCfgMode;
u8 o_stIspInitPar[4032];
u8 o_tispYspRefresh;

static const struct t33_ysp_ops ysp_ops = {
	tisp_reg_write_t33,
	tisp_mdns_decode_reg_write,
	tisp_rcfg_module_cfg_trig,
	tisp_rcfg_get_dma_state,
};

u32 bo_tisp_ysp_t33_frame_done_interrupt(u32 ch)
{
	return t33_ysp_frame_done(&ysp_ops, ch, t33_le32(o_stIspInitPar + 904), &o_tispYspRefresh,
				  o_yspCfgMode, o_yspInfo);
}
