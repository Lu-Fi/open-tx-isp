/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "tgA": AWB / AF gain update entry points
 * (driver/t32/tx_isp_t33_tgA.h) with the vendor calling convention, built
 * for mipsel and run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_tgA.h"

/* module globals the vendor functions read / write (GLOBAL lines) */
u32 awbInfo[4];
u32 total_gain[4];
u8 tispAwbRefresh;

/* callees ported elsewhere / hardware: recorded, not run */
extern void tisp_awb_set_regional_threshold(u32 ch);
extern void tisp_awb_set_lum_th_freq(u32 ch);

void bo_tisp_awb_tgain_update(u32 ch, u32 unused, u32 tgain)
{
	(void)unused;
	t33_awb_tgain_update((u32 **)awbInfo, ch, tgain, &tispAwbRefresh);
	tisp_awb_set_regional_threshold(ch);
	tisp_awb_set_lum_th_freq(ch);
}

u32 bo_tisp_awb_ev_update(u32 ch, u32 unused, u32 ev)
{
	(void)unused;
	return t33_awb_ev_update((u32 **)awbInfo, ch, ev);
}

void bo_tisp_af_tgain_update(u32 ch, u32 gain)
{
	t33_af_tgain_update(total_gain, ch, gain);
}
