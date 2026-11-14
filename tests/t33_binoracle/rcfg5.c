/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg5": the DMSC / DPC / GAMMA register-list
 * initialisers (driver/t32/tx_isp_t33_rcfg5.h) with the vendor calling
 * convention, built for mipsel and run against the vendor tx-isp-t33.ko
 * instructions (spec: rcfg5.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg5.h"

u32 bo_tisp_rcfg_dmsc_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_dmsc_init(e); }
u32 bo_tisp_rcfg_dpc_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_dpc_init(e); }
u32 bo_tisp_rcfg_gamma_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_gamma_init(e); }
