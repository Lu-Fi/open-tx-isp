/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 binary oracle, block "bcshtr": tisp_bcsh_ev_update / tisp_bcsh_ct_update triggers
 * (driver/t31/tx_isp_t31_ctev.h) vs the vendor tx-isp-t31.ko instructions (spec: bcshtr.conf; openimp #533).
 * o_blk mirrors the vendor BCSH_real block: real, last ev, ev threshold, last ct, ct threshold.
 */
#include <stdint.h>
#include "tx_isp_t31_ctev.h"

int o_blk[5];
uint32_t o_cm_ev, o_cm_ct, o_raw_ev, o_raw_ct;

uint32_t bo_tisp_bcsh_ev_update(uint32_t ev)
{
	return t31_bcsh_ev_update(ev, &o_raw_ev, &o_cm_ev, &o_blk[0], (uint32_t *)&o_blk[1], o_blk[2]);
}

uint32_t bo_tisp_bcsh_ct_update(uint32_t ct)
{
	return t31_bcsh_ct_update(ct, &o_raw_ct, &o_cm_ct, &o_blk[0], (uint32_t *)&o_blk[3], o_blk[4]);
}
