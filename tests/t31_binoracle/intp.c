/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "intp": tisp_simple_intp / tisp_dmsc_intp (driver/t31/tx_isp_t31_intp.h) vs the
 * vendor tx-isp-t31.ko instructions (spec: intp.conf; openimp #533).  The vendor tisp_dmsc_intp runs its own
 * tisp_simple_intp, ours t31_simple_intp; the 44 results and curves are compared as globals.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t31_intp.h"

#define DECL(n, s) uint32_t dmsc_##n##_val; uint32_t dmsc_##n##_curve[9]; uint32_t *dmsc_##n##_curve_now;
T31_DMSC_INTP_LIST(DECL)

uint32_t bo_tisp_simple_intp(int idx, int frac, const uint32_t *a) { return t31_simple_intp(idx, frac, a); }
uint32_t bo_tisp_dmsc_intp(uint32_t gain) { T31_DMSC_INTP_BODY(gain); return 0; }
