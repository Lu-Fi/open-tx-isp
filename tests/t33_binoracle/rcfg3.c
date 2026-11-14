/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg3": the CDNS / CLM / CSC register-list init
 * (driver/t32/tx_isp_t33_rcfg3.h) with the vendor calling convention, built
 * for mipsel and run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg3.h"

u32 bo_tisp_rcfg_cdns_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_cdns_init(e); }
u32 bo_tisp_rcfg_clm_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_clm_init(e); }
u32 bo_tisp_rcfg_csc_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_csc_init(e); }
