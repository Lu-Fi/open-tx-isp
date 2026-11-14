/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "fd-ydns": frame-done handler of the ydns block. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_fd_ydns.h"

extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern void tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);

u32 stIspInitPar[1008];
u8 tispYdnsRefresh;
u8 ydnsCfgMode;
u8 *ydnsInfo[4];

int bo_tisp_ydns_t33_frame_done_interrupt(u32 a0)
{
	static const struct t33_ydns_fd_ops ops = {
		tisp_reg_write_t33, tisp_rcfg_get_dma_state, tisp_rcfg_module_cfg_trig,
	};

	return t33_ydns_frame_done(a0, stIspInitPar[226], &tispYdnsRefresh, ydnsCfgMode,
				   ydnsInfo[a0 & 0xff], &ops);
}
