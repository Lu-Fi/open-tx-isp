/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) BCSH API setters hue and saturation (openimp #99): same
 * flow as brightness / contrast (tx_isp_t33_bcshK.h), other section flag
 * byte and API value byte.
 */
#ifndef TX_ISP_T33_BCSHH_H
#define TX_ISP_T33_BCSHH_H

#include "tx_isp_t33_bcshK.h"

/* common body: section flag byte secoff := 2, state byte stoff := *val, forced EV pass, refresh, list store */
static inline int t33_bcsh_api_apply(u32 ch, const u8 *val, u32 secoff, u32 stoff)
{
	u8 *st = bcsh_para[ch];
	u8 *sec = *(u8 **)st;
	int r;

	sec[secoff] = 2;
	st[stoff] = val[0];
	tisp_bcsh_interp_by_ev(ch, (u32)(unsigned long)val, ((u32 *)st)[2], ((u32 *)st)[3], 1);
	tisp_bcsh_calc_para_refresh(ch, 0);
	tisp_params_transform(ch & 0xff);
	r = tisp_rcfg_get_dma_state(18);
	if (r) {
		tispBcshRefresh = 2;
		return 0;
	}
	t33_bcshk_write_all(ch & 0xff);
	tispBcshRefresh = 1;
	return 0;
}

/* tisp_bcsh_api_set_hue */
static inline int t33_bcsh_set_hue(u32 ch, const u8 *val)
{
	return t33_bcsh_api_apply(ch, val, 1193, 463);
}

/* tisp_bcsh_api_set_saturation */
static inline int t33_bcsh_set_saturation(u32 ch, const u8 *val)
{
	return t33_bcsh_api_apply(ch, val, 1138, 462);
}

#endif /* TX_ISP_T33_BCSHH_H */
