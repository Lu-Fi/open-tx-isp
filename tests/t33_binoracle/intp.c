/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "intp": signed EV / bit-value interpolators and
 * the simple u16 interpolator (driver/t32/tx_isp_t33_intp.h) with the vendor
 * calling convention, built for mipsel and run against tx-isp-t33.ko.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_intp.h"

int bo_tisp_ev_intp_int8_t(u32 ev, const u8 *e, const u8 *t) { return t33_ev_intp_s8(ev, e, t); }
int bo_tisp_bv_intp_int8_t(u32 v, const u8 *k, const u8 *t) { return t33_bv_intp_s8(v, k, t); }
u16 bo_tisp_simple_intp_uint16_t(u32 i, u32 f, const u8 *t) { return t33_simple_intp_u16(i, f, t); }
