/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "fd-dmsc": the DMSC frame-done handler with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_fd_dmsc.h"

u8 dmscCfgMode;
u8 *dmscInfo[4];
u8 tispDmscRefresh;
u32 stIspInitPar[1008];

int bo_tisp_dmsc_t33_frame_done_interrupt(u32 ch)
{
	return t33_dmsc_frame_done(ch, stIspInitPar[226], dmscInfo, &dmscCfgMode, &tispDmscRefresh);
}
