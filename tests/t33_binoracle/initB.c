/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "initB": tisp_ccm_init / tisp_ccm_deinit /
 * tisp_clm_deinit (driver/t32/tx_isp_t33_initB.h) with the vendor calling
 * convention, built for mipsel and run against the vendor tx-isp-t33.ko.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_initB.h"

/* module data objects (GLOBAL lines) */
u32 tparamsP[4];
u32 ccm_para[4];
u32 clm_info[4];

int bo_tisp_ccm_init(u32 ch, u8 *initpar) { return t33_ccm_initB(ch, initpar); }
int bo_tisp_ccm_deinit(u32 ch) { return t33_ccm_deinit(ch); }
int bo_tisp_clm_deinit(u32 ch) { return t33_clm_deinit(ch); }
