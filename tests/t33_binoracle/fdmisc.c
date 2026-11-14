/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "fdmisc": MSCA / TOP frame-done glue, MDNS trigger, rcfg module getter
 * (tx_isp_t33_fd_misc.h) with the vendor calling convention. */
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

extern int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);
extern u32 tisp_top_reg_write(u32 ch);
extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);

#include "tx_isp_t33_fd_misc.h"

u8 o_stIspInitPar[4032];
u8 o_tispMscaRefresh[4], o_tispTopRefresh[4], o_tispMdnsRefresh[4], o_mdnsInit[4], o_mdnsCfgMode[4];
u32 o_testNum, o_RcfgModule;

int bo_msca_fd(u32 ch) { return t33_msca_frame_done(ch, o_stIspInitPar, o_tispMscaRefresh, &o_testNum); }
int bo_top_pnd(u32 ch) { return t33_top_pipe_null_down(ch, o_stIspInitPar, o_tispTopRefresh); }
void bo_mdns_trig(u32 ch) { t33_mdns_reg_trig(ch, o_mdnsCfgMode[0], o_mdnsInit[0], o_tispMdnsRefresh); }
u32 bo_get_rcfg_module(void) { return o_RcfgModule; }
