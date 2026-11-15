/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "cscR": csccr parameter API (driver/t32/tx_isp_t33_cscR.h) with the
 * calling convention of the vendor functions, built for mipsel and run against the vendor
 * tx-isp-t33.ko instructions (spec: cscR.conf, FUNCS).
 */
#include <stdint.h>
#include "tx_isp_t33_cscR.h"

/* callees ported / proven elsewhere: declared with the vendor name, never run */
extern int tisp_csccr_sync_api_para(u32 idx, u32 one);
extern int tisp_csccr_bin_par_update(u32 idx);
extern int tisp_reg_write_t33(u32 idx, u32 list, u32 slot, u32 val);

/* module data objects (GLOBAL lines) */
u32 *csccrInfo[4];
u8 csccrCfgMode;
u8 tispCsccrRefresh;
u8 t33_csccr_default_mode[12];
u8 t33_csccr_stretch_mode[12];
u8 t33_csccr_compress_mode[12];
u8 t33_csccr_alt_en[4];
const u8 *t33_csccr_alt_src[4];

int bo_tisp_csccr_api_get(u32 ch, u8 *out)
{
	return t33_csccr_api_get(ch, out, csccrInfo);
}

int bo_tisp_csccr_api_set(u32 ch, const u32 *in)
{
	return t33_csccr_api_set(ch, in, csccrInfo, &csccrCfgMode, &tispCsccrRefresh);
}

void bo_tisp_csccr_para_refresh_by_mode(u32 mode, u32 one, u8 *sec)
{
	t33_csccr_para_refresh_by_mode(mode, one, sec);
}
