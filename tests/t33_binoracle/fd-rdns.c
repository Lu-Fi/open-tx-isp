/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "fd-rdns": rdns frame-done handler with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_rcfg33.h"

extern void tisp_reg_write_t33(u32 id, u32 mod, u32 reg, u32 val);
extern void tisp_rcfg_module_cfg_trig(u32 a, u32 b);
extern int tisp_rcfg_get_dma_state(u32 a);

#include "tx_isp_t33_fd_rdns.h"

u32 o_stIspInitPar[1008];
u8 o_tispRdnsRefresh;
u8 o_rdnsCfgMode;
u8 *o_rdnsInfo[4];

int bo_tisp_rdns_t33_frame_done_interrupt(u32 chan)
{
	return t33_rdns_frame_done(chan, o_stIspInitPar, &o_tispRdnsRefresh, o_rdnsCfgMode, o_rdnsInfo);
}
