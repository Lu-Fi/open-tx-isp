/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mdnsQ": tisp_mdns_deinit / tisp_mdns_sleep /
 * tisp_mdns_wake_up (driver/t32/tx_isp_t33_mdnsQ.h) with the vendor
 * calling convention, run against the vendor tx-isp-t33.ko instructions
 * (spec: mdnsQ.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mdnsQ.h"

/* module globals the vendor functions read / write (GLOBAL lines) */
u32 mdnsInfo[4];
u8 riscvMdnsEn;
u32 tisp_mdns_ref_info[5];
u8 stIspInitPar[4032];
u8 global_ybsnthres[44];
u8 global_ypbv0stathres[44];
u8 global_ypbv1stathres[44];
u8 global_yrefweimin[44];
u8 global_yrefweiminadjthres[44];

/* callees: neither side runs them, the calls are recorded and compared */
/* (declared extern in tx_isp_t33_mdnsQ.h, left undefined so they are not inlined) */

void bo_tisp_mdns_deinit(u32 chn) { t33_mdns_deinit(chn); }
void bo_tisp_mdns_sleep(void) { t33_mdns_sleep(); }
void bo_tisp_mdns_wake_up(void) { t33_mdns_wake_up(); }
