/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfg8": the TMO / YDNS / YSP register-list init
 * (driver/t32/tx_isp_t33_rcfg8.h) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfg8.h"

u32 bo_tisp_rcfg_tmo_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_tmo_init(e); }
u32 bo_tisp_rcfg_ydns_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_ydns_init(e); }
u32 bo_tisp_rcfg_ysp_reg_init(u32 a0, u32 *e) { (void)a0; return t33_rcfg_ysp_init(e); }
