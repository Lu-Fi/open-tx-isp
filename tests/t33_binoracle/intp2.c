/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "intp2": the 1-D interpolation helpers
 * (driver/t32/tx_isp_t33_intp2.h) with the vendor calling convention,
 * built for mipsel and run against the vendor tx-isp-t33.ko instructions
 * by tools/ko_emu/t33_binoracle.py (spec: intp2.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_intp2.h"

u32 bo_tisp_ev_intp_uint16_t(u32 ev, const u8 *e, const u8 *t) { return t33_ev_intp_u16(ev, e, t); }
u32 bo_tisp_bv_intp_uint16_t(u32 bv, const u8 *e, const u8 *t) { return t33_bv_intp_u16(bv, e, t); }
u32 bo_tisp_bv_intp_uint8_t(u32 bv, const u8 *e, const u8 *t) { return t33_bv_intp_u8(bv, e, t); }
