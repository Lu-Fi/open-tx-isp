/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) BCSH / MDNS small entry points (openimp #99): tisp_bcsh_sub_interp,
 * tisp_bcsh_get_new_para, tisp_set_hue, tisp_mdns_refresh.
 */
#ifndef TX_ISP_T33_BCSHQ_H
#define TX_ISP_T33_BCSHQ_H

#include "tx_isp_t33_bcsh.h"

extern int tisp_bcsh_api_set_hue(u32 ch, const u8 *val);
extern int tisp_mdns_par_refresh(u32 ch, u32 a1, u32 a2);

/* tisp_bcsh_sub_interp: a + round((b - a) * f, 12) */
static inline int t33_bcshq_sub_interp(u32 f, int a, int b)
{
	return t33_sub_interp(f, b == a ? a : a, b);
}

/* tisp_bcsh_get_new_para: the CT table of section sec into t (24 u16) */
static inline void t33_bcshq_get_new_para(u8 *sec, u8 *t)
{
	t33_bcsh_new_para(sec, t);
}

/* tisp_set_hue: the hue byte goes to the API setter by pointer */
static inline int t33_bcshq_set_hue(u32 ch, u32 val)
{
	u8 b = (u8)val;

	return tisp_bcsh_api_set_hue(ch, &b);
}

/* tisp_mdns_refresh: par_refresh with the state word 8728 of mdnsInfo[ch] */
static inline int t33_mdns_refresh(u32 ch, u32 a1, void *const *info)
{
	const u32 *v = info[ch];

	tisp_mdns_par_refresh(ch, a1, v[2182]);
	return 0;
}

#endif /* TX_ISP_T33_BCSHQ_H */
