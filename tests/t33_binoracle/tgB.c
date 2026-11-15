/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "tgB": the long-frame gain / EV dispatch
 * (driver/t32/tx_isp_t33_tgB.h) with the vendor calling convention, run
 * against the vendor tx-isp-t33.ko instructions (spec: tgB.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_tgB.h"

/* the vendor callees: recorded as CALLS, never run */
extern u32 tisp_top_get_bypass(u32 ch);
extern void tisp_dpc_l_gain_update(u32 ch, u32 gain);
extern void tisp_blc_tgain_update(u32 ch, u32 gain);
extern void tisp_lsc_tgain_update(u32 ch, u32 arg1, u32 gain, u32 force);
extern void tisp_dmsc_refresh(u32 ch, u32 gain);
extern void tisp_mdns_refresh(u32 ch, u32 gain);
extern void tisp_ysp_refresh(u32 ch, u32 gain);
extern void tisp_cdns_refresh(u32 ch, u32 gain);
extern void tisp_ydns_refresh(u32 ch, u32 gain);
extern void tisp_sdns_refresh(u32 ch, u32 gain);
extern void tisp_rdns_refresh(u32 ch, u32 gain);
extern void tisp_awb_tgain_update(u32 ch, u32 arg1, u32 gain, u32 force);
extern void tisp_af_tgain_update(u32 ch, u32 gain);
extern void tisp_awb_ev_update(u32 ch, u32 arg1, u32 ev0, u32 ev1);
extern void tisp_gamma_ev_update(u32 ch, u32 arg1, u32 ev0, u32 ev1);
extern void tisp_ccm_ev_update(u32 ch, u32 arg1, u32 ev0, u32 ev1);
extern void tisp_rdns_ev_update(u32 ch, u32 arg1, u32 ev0, u32 ev1);
extern void tisp_defog_ev_update(u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5);
extern void tisp_bcsh_ev_update(u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5);
extern void tisp_lce_ev_update(u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5);
extern void tisp_tmo_ev_update(u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5);

/* module data objects (GLOBAL lines) */
u32 tgain_old[T33_TGB_CHANNELS];
u32 ev_last[T33_TGB_EV_WORDS];

static const struct t33_tgB_ops tgB_ops = {
	.top_get_bypass = tisp_top_get_bypass,
	.dpc_l_gain_update = tisp_dpc_l_gain_update,
	.blc_tgain_update = tisp_blc_tgain_update,
	.lsc_tgain_update = tisp_lsc_tgain_update,
	.dmsc_refresh = tisp_dmsc_refresh,
	.mdns_refresh = tisp_mdns_refresh,
	.ysp_refresh = tisp_ysp_refresh,
	.cdns_refresh = tisp_cdns_refresh,
	.ydns_refresh = tisp_ydns_refresh,
	.sdns_refresh = tisp_sdns_refresh,
	.rdns_refresh = tisp_rdns_refresh,
	.awb_tgain_update = tisp_awb_tgain_update,
	.af_tgain_update = tisp_af_tgain_update,
	.awb_ev_update = tisp_awb_ev_update,
	.gamma_ev_update = tisp_gamma_ev_update,
	.ccm_ev_update = tisp_ccm_ev_update,
	.rdns_ev_update = tisp_rdns_ev_update,
	.defog_ev_update = tisp_defog_ev_update,
	.bcsh_ev_update = tisp_bcsh_ev_update,
	.lce_ev_update = tisp_lce_ev_update,
	.tmo_ev_update = tisp_tmo_ev_update,
};

int bo_tisp_long_tgain_update(u32 ch, u32 arg1, u32 gain, u32 force)
{
	return t33_long_tgain_update(&tgB_ops, tgain_old, ch, arg1, gain, force);
}

int bo_tisp_long_ev_update(u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5)
{
	return t33_long_ev_update(&tgB_ops, ev_last, ch, arg1, ev0, ev1, a4, a5);
}

int bo_tisp_long_again_update(void)
{
	return t33_long_again_update();
}
