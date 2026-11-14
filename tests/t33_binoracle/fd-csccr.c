/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "fd-csccr": csccr frame-done handler with the vendor calling convention. */
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

extern int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);
extern int tisp_mdns_dn_params_api2(u32 ch);
extern int tisp_csccr_set_day(u32 ch);
extern int tisp_mdns_dn_params_api(u32 ch);

#include "tx_isp_t33_fd_csccr.h"

u8 o_stIspInitPar[4032];
u8 o_tispCsccrRefresh[4];
u32 o_frameCount[4];
u32 o_curFrame[4];
u32 o_curFrame2[4];

int bo_tisp_csccr_t33_frame_done_interrupt(u32 ch)
{
	return t33_csccr_frame_done(ch, o_stIspInitPar, o_tispCsccrRefresh, o_frameCount, o_curFrame, o_curFrame2);
}
