/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg1": the AE / AF / AWB module-attribute
 * table entries (driver/t32/tx_isp_t33_rcfg1.h) with the vendor calling
 * convention, run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg1.h"

u32 bo_tisp_rcfg_ae_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_ae_init(e); }
u32 bo_tisp_rcfg_af_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_af_init(e); }
u32 bo_tisp_rcfg_awb_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_awb_init(e); }
