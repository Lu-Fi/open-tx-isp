/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "sw-small": SDNS / YDNS / DPC day-night IQ-bin switch
 * (driver/t32/tx_isp_t33_swsmall.h) with the vendor calling convention (spec: sw-small.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_swsmall.h"

#define NOIPA __attribute__((noinline, noclone, used))

u8 sdnsCfgMode, ydnsCfgMode, dpcCfgMode;
u8 tispSdnsRefresh, tispYdnsRefresh, tispDpcRefresh;
u32 *sdnsInfo[4], *ydnsInfo[4], *dpcInfo[4];
u32 *tparamsP[4];

/* callees: recorded by the oracle (CALLS), never run */
NOIPA void tisp_reg_write_t33(u32 id, u32 list, u32 slot, u32 val) { (void)id; (void)list; (void)slot; (void)val; __asm__ volatile("" ::: "memory"); }
NOIPA int tisp_rcfg_get_dma_state(u32 id) { (void)id; __asm__ volatile("" ::: "memory"); return 0; }
NOIPA u32 tisp_simple_intp_uint8_t(int idx, u32 frac, const u8 *tab) { (void)idx; (void)frac; (void)tab; __asm__ volatile("" ::: "memory"); return 0; }
NOIPA u32 tisp_simple_intp_uint16_t(int idx, u32 frac, const u8 *tab) { (void)idx; (void)frac; (void)tab; __asm__ volatile("" ::: "memory"); return 0; }
NOIPA void tisp_sdns_other_params_transform(u32 ch) { (void)ch; __asm__ volatile("" ::: "memory"); }
NOIPA void func_sdns_para_update(u32 ch) { (void)ch; __asm__ volatile("" ::: "memory"); }
NOIPA void func_ydns_bin_para_update(u32 ch) { (void)ch; __asm__ volatile("" ::: "memory"); }
NOIPA void func_ydns_para_update(u32 ch) { (void)ch; __asm__ volatile("" ::: "memory"); }
NOIPA void tisp_dpc_bin_para_update(u32 ch) { (void)ch; __asm__ volatile("" ::: "memory"); }
NOIPA void func_dpc_para_update(u32 ch) { (void)ch; __asm__ volatile("" ::: "memory"); }
NOIPA void func_ctr_para_update(u32 ch) { (void)ch; __asm__ volatile("" ::: "memory"); }
NOIPA void func_cac_para_update(u32 ch) { (void)ch; __asm__ volatile("" ::: "memory"); }

int bo_tisp_sdns_switch_para(u32 ch) { return t33_sdns_switch(ch); }
void bo_tisp_ydns_switch_para(u32 ch) { t33_ydns_switch(ch); }
void bo_tisp_dpc_switch_para(u32 ch) { t33_dpc_switch(ch); }
void bo_tisp_dpc_l_gain_update(u32 ch, u32 gain) { t33_dpc_l_gain_update(ch, gain); }
