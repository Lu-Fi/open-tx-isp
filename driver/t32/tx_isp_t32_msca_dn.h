/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA day/night refresh: readable C of tisp_msca_dn_refresh(ch).  It re-points the
 * context's parameter block at the tuning base + 0xef50, then reloads all three sets
 * (para_calc, curve_calc, init_chx_cfg, channel pending) and finishes with
 * scaling_algorithm.  Register modes: as in curve.h (direct writes modelled).
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscal_oracle).
 */
#ifndef TX_ISP_T32_MSCA_DN_H
#define TX_ISP_T32_MSCA_DN_H

#include "tx_isp_t32_msca_crop.h"

/* par_base = t32_data_dd0a4[ch]; returns the number of register writes */
static inline int t32_msca_dn_refresh(struct t32_msca_ch *c, u8 *sets, u32 par_base,
				      struct t32_front_write *w)
{
	unsigned int set, n = 0;

	c->par = (u8 *)(uintptr_t)(par_base + 0xef50);
	for (set = 0; set < 3; set++) {
		t32_msca_para_calc(c, sets, set);
		t32_msca_curve_calc(c, sets, set);
		t32_msca_init_chx_cfg_w(c, sets, set, w, &n);
		c->pending = 1;
	}
	t32_msca_scaling_algorithm_w(c, w, &n);
	return (int)n;
}

#endif /* TX_ISP_T32_MSCA_DN_H */
