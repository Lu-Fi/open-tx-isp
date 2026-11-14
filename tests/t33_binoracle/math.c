/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "math": fixpoint_sqrt and the lib_tisp_* thunks
 * (driver/t32/tx_isp_t33_math.h) with the vendor calling convention, built
 * for mipsel and run against tx-isp-t33.ko by tools/ko_emu/t33_binoracle.py.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_math.h"

int bo_fixpoint_sqrt(int x, int g, int iters) { return t33_fixpoint_sqrt(x, g, iters); }
int bo_lib_tisp_fix_point_mult2_32(int a, int b, int shift) { return t33_fix_point_mult2_32(a, b, shift); }
int bo_lib_tisp_log2_fixed_to_fixed(int x, int y, u32 frac) { return t33_log2_fixed_to_fixed(x, y, frac); }
