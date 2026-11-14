/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "ctup": tisp_ct_update (driver/t32/tx_isp_t33_ctup.h) with the vendor calling
 * convention, run against the vendor tx-isp-t33.ko instructions (spec: ctup.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_ctup.h"

int ct_last[4];

u32 bo_tisp_ct_update(u32 ch, u32 x, int ct, int ct2) { return t33_ct_update(ch, x, ct, ct2, ct_last + 2 * ch); }
