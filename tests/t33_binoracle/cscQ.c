/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "cscQ": our T33 CSC API / refresh helpers (driver/t32/tx_isp_t33_cscQ.h) with the
 * calling convention of the vendor functions they port, built for mipsel (tests/t33_binoracle/run.sh) and run
 * against the vendor tx-isp-t33.ko instructions by tools/ko_emu/t33_binoracle.py (spec: cscQ.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>

extern int isp_printf(int level, const char *fmt, ...);

#include "tx_isp_t33_cscQ.h"

/* module globals (GLOBAL lines): the vendor's csc_para pointer array and tispCscRefresh */
u8 *csc_para[4];
u8 tispCscRefresh;
u8 stIspInitPar[4032];

/* callees (CALLS lines): neither side runs them, the calls are recorded and compared */
extern void tisp_csc_para_refresh_by_mode(u32 mode, u32 force, u8 *cp);
extern void tisp_csc_sync_api_para(u32 ch, u32 flag);
extern void tisp_csc_update_para(u32 ch);
extern int tisp_t33_csc_write_reg(u32 ch);
extern void tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);

int bo_tisp_csc_api_get(u32 ch, u8 *dst)
{
	return t33_csc_api_get(csc_para, ch, dst);
}

int bo_tisp_csc_api_set(u32 ch, const u8 *src)
{
	return t33_csc_api_set(csc_para, ch, src, tisp_csc_para_refresh_by_mode, tisp_csc_sync_api_para,
			       tisp_csc_update_para, tisp_t33_csc_write_reg);
}

int bo_tisp_csc_fra_down_interrupt(u32 ch)
{
	return t33_csc_fra_down_interrupt(ch, (const u32 *)stIspInitPar, &tispCscRefresh,
					  tisp_rcfg_module_cfg_trig, tisp_t33_csc_write_reg);
}
