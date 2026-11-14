/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "fd-sdns": tisp_sdns_t33_frame_done_interrupt */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_fd_sdns.h"

extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);

u32 o_stIspInitPar[1008];
u8 o_tispSdnsRefresh;
u8 o_sdnsCfgMode;
u8 *o_sdnsInfo[4];

static const struct t33_sdns_fd_ops o_ops = {
	tisp_reg_write_t33, tisp_rcfg_get_dma_state, tisp_rcfg_module_cfg_trig,
};

int bo_tisp_sdns_t33_frame_done_interrupt(u32 a0)
{
	return t33_sdns_frame_done(a0, o_stIspInitPar, &o_tispSdnsRefresh, o_sdnsCfgMode,
				   (const u8 *const *)o_sdnsInfo, &o_ops);
}
