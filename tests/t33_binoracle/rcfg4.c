/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg4": the CSCCR / DEFOG / DG register-list
 * init functions (driver/t32/tx_isp_t33_rcfg4.h) with the vendor calling
 * convention, built for mipsel and run against the vendor tx-isp-t33.ko
 * instructions (spec: rcfg4.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg4.h"

u32 bo_tisp_rcfg_csccr_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_csccr_init(e); }
u32 bo_tisp_rcfg_defog_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_defog_init(e); }
u32 bo_tisp_rcfg_dg_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_dg_init(e); }
