/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "dnglue": CSCCR / MDNS day-night glue (tx_isp_t33_dnglue.h) with the vendor calling convention. */
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
extern int isp_printf(int level, const char *fmt, ...);

#include "tx_isp_t33_dnglue.h"

u8 o_csccrCfgMode[4], o_tispCsccrRefresh[4], o_mdnsCfgMode[4], o_tispMdnsRefresh[4];
u8 o_uvLimitFlag[4], o_ispInitFinish[4], o_start_night[4];
u8 o_stIspInitPar[4032];
u32 o_frameCount[4], o_curFrame[4], o_curFrame2[4];

static const struct t33_dn_state st = {
	o_csccrCfgMode, o_tispCsccrRefresh, o_mdnsCfgMode, o_tispMdnsRefresh, o_uvLimitFlag,
	o_ispInitFinish, o_start_night, o_stIspInitPar, o_frameCount, o_curFrame, o_curFrame2,
};

void bo_tisp_csccr_set_day(u32 ch) { t33_csccr_set_day(&st, ch); }
void bo_tisp_csccr_set_night(u32 ch) { t33_csccr_set_night(&st, ch); }
int bo_tisp_mdns_dn_params_api(const u8 *info) { return t33_mdns_dn_api(&st, 0, info); }
int bo_tisp_mdns_dn_params_api2(const u8 *info) { return t33_mdns_dn_api2(&st, 0, info); }
int bo_lib_tisp_set_csccr(u32 ch, u32 mode) { return t33_lib_set_csccr(&st, ch, mode); }
