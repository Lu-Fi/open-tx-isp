/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg7": the MDNS / RDNS / SDNS register-list
 * init functions (driver/t32/tx_isp_t33_rcfg7.h) with the vendor calling
 * convention, run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg7.h"

u32 t33_st_isp_init_par_mode[227];

u32 bo_tisp_rcfg_mdns_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_mdns_init(e); }
u32 bo_tisp_rcfg_rdns_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_rdns_init(e); }
u32 bo_tisp_rcfg_sdns_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_sdns_init(e); }
