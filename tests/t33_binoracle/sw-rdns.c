/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "sw-rdns": tisp_rdns_switch_para with the vendor
 * calling convention, built for mipsel and run against the vendor
 * tx-isp-t33.ko instructions (spec: sw-rdns.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_swrdns.h"

/* callees recorded by the oracle (CALLS), vendor names */
extern void func_rdns_bin_para_update(u32 ch);
extern void func_rdns_para_update(u32 ch);
extern u32 tisp_ev_intp_uint8_t(u32 ev, const u8 *e, const u8 *t);
extern u32 tisp_simple_intp_uint8_t(int i, u32 f, const u8 *t);
extern u32 tisp_rcfg_get_dma_state(u32 id);
extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);

/* module data objects (GLOBAL lines) */
u32 *rdnsInfo[4];
u8 rdnsCfgMode;
u8 tispRdnsRefresh;

void bo_tisp_rdns_switch_para(u32 ch) { t33_rdns_switch_para(ch); }
