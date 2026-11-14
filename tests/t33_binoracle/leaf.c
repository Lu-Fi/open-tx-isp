/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "leaf": our small T33-only helpers (driver/t32/tx_isp_t33_*.h) with the calling
 * convention of the vendor functions they port, built for mipsel (tests/t33_binoracle/run.sh) and run against
 * the vendor tx-isp-t33.ko instructions by tools/ko_emu/t33_binoracle.py (spec: leaf.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_lsc.h"
#include "tx_isp_t33_mdns.h"
#include "tx_isp_t33_osd.h"

int bo_tisp_sub_interp(u32 f, int a, int b) { return t33_sub_interp(f, a, b); }
u32 bo_tisp_ev_intp_uint8_t(u32 ev, const u8 *e, const u8 *t) { return t33_ev_intp_u8(ev, e, t); }
u32 bo_tisp_sqrt_round(unsigned long long x) { return t33_sqrt_round(x); }
int bo_func_mdns_fixpoint_sqrt(int x, int g, int iters) { return t33_fix_sqrt(x, g, iters); }
void bo_cti_func_slope_calc(int x, int y, u32 *val, u32 *shift) { t33_slope(x, y, val, shift); }
void bo_func_sort_L2H(int *p, int n) { t33_mdns_sort(p, n); }
void bo_find_k_n(u32 v, u8 *k, u8 *n) { t33_find_k_n(v, k, n); }
