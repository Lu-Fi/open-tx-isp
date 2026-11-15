/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mdnsP": the MDNS parameter helpers
 * (driver/t32/tx_isp_t33_mdnsP.h) with the vendor calling convention,
 * built for mipsel and run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mdnsP.h"

/* the vendor's mdnsInfo[16] pointer array (module data object) */
u32 mdnsInfo[4];

/* tisp_reg_write_t33: hardware register / list slot write (ported elsewhere) */
extern int tisp_reg_write_t33(u32 id, u32 blk, u32 reg, u32 val);

int bo_tisp_mdns_buf_clc(u32 id) { return t33_mdns_buf_clc(id, tisp_reg_write_t33); }
void bo_tisp_mdns_cmp_par_calc(u32 chn, u32 h, u32 w) { t33_mdns_cmp_par((u32 *)mdnsInfo[chn & 0xff], h, w); }
int bo_tisp_mdns_refresh(u32 chn, u32 mode) { return t33_mdns_refresh(chn, mode, (u32 *)mdnsInfo[chn & 0xff]); }
