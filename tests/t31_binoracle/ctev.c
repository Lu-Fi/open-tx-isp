/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "ctev": tisp_ct_update / tisp_ev_update (driver/t31/tx_isp_t31_ctev.h) with the
 * vendor calling convention, run against the vendor tx-isp-t31.ko instructions (spec: ctev.conf; openimp #532).
 */
#include <stdint.h>
#include "tx_isp_t31_ctev.h"

uint32_t o_ev_last[2];

uint32_t bo_tisp_ct_update(uint32_t a, uint32_t b) { return t31_ct_update(a, b); }
uint32_t bo_tisp_ev_update(uint32_t ev, uint32_t aux) { return t31_ev_update(ev, aux, o_ev_last); }
