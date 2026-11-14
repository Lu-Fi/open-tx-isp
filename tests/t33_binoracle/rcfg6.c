/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg6": the IPC / LCE / LSC register-list init
 * functions (driver/t32/tx_isp_t33_rcfg6.h) with the vendor calling
 * convention, run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg6.h"

/* vendor signature: (a0 unused, a1 = module-attribute table entry) */
u32 bo_tisp_rcfg_ipc_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_ipc_init(e); }
u32 bo_tisp_rcfg_lce_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_lce_init(e); }
u32 bo_tisp_rcfg_lsc_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_lsc_init(e); }
