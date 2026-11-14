/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "fd-cdns": CDNS frame-done handler with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_fd_cdns.h"

extern int tisp_reg_write_t33(u32 id, u32 mod, u32 reg, u32 val);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern int tisp_rcfg_module_cfg_trig(u32 id, u32 mod);

u32 t33o_initpar[1008];
u8 t33o_refresh;
u8 t33o_cfgmode;
u8 *t33o_info[4];

static const struct t33_cdns_ops t33o_ops = {
	tisp_reg_write_t33, tisp_rcfg_get_dma_state, tisp_rcfg_module_cfg_trig,
};

int bo_tisp_cdns_t33_frame_done_interrupt(u32 ch)
{
	return t33_cdns_frame_done(&t33o_ops, ch, t33o_initpar[226], &t33o_refresh, t33o_cfgmode, t33o_info);
}
