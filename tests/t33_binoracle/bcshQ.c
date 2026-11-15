/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "bcshQ": vendor calling convention wrappers (see bcshQ.conf). */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bcshQ.h"

void *bo_mdnsInfo[4];

int bo_tisp_bcsh_sub_interp(u32 f, int a, int b) { return t33_bcshq_sub_interp(f, a, b); }
void bo_tisp_bcsh_get_new_para(u8 *sec, u8 *t) { t33_bcshq_get_new_para(sec, t); }
int bo_tisp_set_hue(u32 ch, u32 val) { return t33_bcshq_set_hue(ch, val); }
int bo_tisp_mdns_refresh(u32 ch, u32 a1) { return t33_mdns_refresh(ch, a1, bo_mdnsInfo); }
