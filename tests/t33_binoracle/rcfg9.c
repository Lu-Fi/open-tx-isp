/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg9": the BCSH / LDC / LSF register-list init
 * functions (driver/t32/tx_isp_t33_rcfg9.h) with the vendor calling
 * convention, run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg9.h"

u32 bo_tisp_rcfg_bcsh_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_bcsh_init(e); }
u32 bo_tisp_rcfg_ldc_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_ldc_init(e); }
u32 bo_tisp_rcfg_lsf_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_lsf_init(e); }
