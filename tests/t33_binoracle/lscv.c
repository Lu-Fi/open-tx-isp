/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "lscv": tisp_lsc_interp_by_tgain and tisp_lsc_irq_frame_done on the vendor lsc_para
 * (driver/t32/tx_isp_t33_lscmisc.h t33_lsc_interp_by_tgain_v / t33_lsc_frame_done_v) with the vendor calling
 * convention (spec: lscv.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_lscmisc.h"

u32 *lsc_para[4];
u32 lsc_count[4];
u8 tispLscRefresh;
u8 stIspInitPar[4096];

extern int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);
extern int tisp_lsc_write_reg(u32 ch, u32 mode);

static void wr(u32 ch, u32 mode)
{
	tisp_lsc_write_reg(ch, mode);
}

int bo_tisp_lsc_interp_by_tgain(u32 ch, u32 gain)
{
	t33_lsc_interp_by_tgain_v(lsc_para[ch], gain);
	return 0;
}

int bo_tisp_lsc_irq_frame_done(u32 ch)
{
	return t33_lsc_frame_done_v(ch, lsc_para, lsc_count, stIspInitPar, &tispLscRefresh, tisp_rcfg_module_cfg_trig, wr);
}
