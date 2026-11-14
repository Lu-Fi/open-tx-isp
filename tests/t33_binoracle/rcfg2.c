/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg2": the register-list init functions of
 * AWBG, BLC and CCM (driver/t32/tx_isp_t33_rcfg2.h) with the vendor
 * calling convention, run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg2.h"

u32 bo_tisp_rcfg_awbg_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_awbg_init(e); }
u32 bo_tisp_rcfg_blc_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_blc_init(e); }
u32 bo_tisp_rcfg_ccm_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_ccm_init(e); }
