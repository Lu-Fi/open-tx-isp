/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "bcshO": tisp_bcsh_irq_frame_down with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bcshO.h"

extern void tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);
extern void tisp_params_transform(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 slot, u32 val);

u32 bcsh_frame_num[4];
void *bcshInfo[4];
u8 bcshCfgMode;
u8 tispBcshRefresh;
u8 stIspInitPar[4032];

static const struct t33_bcsh_fd_ops ops = {
	tisp_rcfg_module_cfg_trig, tisp_params_transform, tisp_rcfg_get_dma_state, tisp_reg_write_t33,
};

int bo_tisp_bcsh_irq_frame_down(u32 ch)
{
	u32 flag = stIspInitPar[904] | stIspInitPar[905] << 8 | stIspInitPar[906] << 16 | (u32)stIspInitPar[907] << 24;

	return t33_bcsh_frame_down(ch, &ops, bcsh_frame_num, flag, &tispBcshRefresh, bcshInfo, bcshCfgMode);
}
