/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mdnspar": func_mdns_para_update and its y / cdns / cur_* / ref_* parts
 * (driver/t32/tx_isp_t33_mdns.h) on the MDNS block state (the vendor's mdnsInfo[0] block, T33_MDNS_WORDS words),
 * built for mipsel and run against the vendor tx-isp-t33.ko instructions (spec: mdnspar.conf, FUNCS, info:).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mdns.h"

void bo_mdns_y(u32 *v) { t33_mdns_y(v); }
void bo_mdns_cdns(u32 *v) { t33_mdns_cdns(v); }
void bo_cur_2d_ef(u32 *v) { t33_mdns_cur_2d_ef(v); }
void bo_cur_flat(u32 *v) { t33_mdns_cur_flat(v); }
void bo_cur_p1p2(u32 *v) { t33_mdns_cur_p1p2(v); }
void bo_cur_text(u32 *v) { t33_mdns_cur_text(v); }
void bo_ref_2d_ef(u32 *v) { t33_mdns_ref_2d_ef(v); }
void bo_ref_flat(u32 *v) { t33_mdns_ref_flat(v); }
void bo_ref_p1p2(u32 *v) { t33_mdns_ref_p1p2(v); }
void bo_mdns_para(u32 *v) { t33_mdns_para(v); }
