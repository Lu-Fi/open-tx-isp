/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "i9N": tisp_dmsc_par_refresh */
#include <stdint.h>
#include <string.h>
#define T33_I9N_GLUE
#include "tx_isp_t33_dmsc.h"

u32 *dmscInfo[4];
u8 dmscCfgMode;
u8 tispDmscRefresh;
extern u32 tisp_ratio(u32 r, u32 v, u32 limit);
extern int tisp_rcfg_get_dma_state(u32 id);
extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);

#include "tx_isp_t33_i9N.h"

void bo_tisp_dmsc_par_refresh(u32 ch, u32 gain, u32 thr)
{
	t33_dmsc_par_step(ch, gain, thr);
}
