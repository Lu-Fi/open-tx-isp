/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "ccmP": the CCM block glue (driver/t32/tx_isp_t33_ccmP.h)
 * with the calling convention of the vendor functions, built for mipsel and run
 * against the vendor tx-isp-t33.ko instructions (spec: ccmP.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_ccmP.h"

/* module statics (GLOBAL lines) */
u8 tispCcmRefresh;
u32 ccm_para[4];
u8 stIspInitPar[4032];

int bo_tisp_ccm_ev_update(u32 ch, const u8 *sec, u32 lo, u32 hi)
{
	return t33_ccm_ev_update_glue(ch, sec, lo, hi);
}

int bo_tisp_ccm_api_set(u32 ch, const u8 *src)
{
	return t33_ccm_api_set_glue(ch, src);
}

int bo_tisp_ccm_irq_frame_done(u32 ch)
{
	return t33_ccm_irq_frame_done_glue(ch);
}
