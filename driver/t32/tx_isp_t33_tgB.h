/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) long-frame gain / EV dispatch (openimp #99, block tgB).
 *
 * The vendor's tisp_long_tgain_update / tisp_long_ev_update fan a new total
 * gain / EV out to the per-block update functions, skipping the blocks whose
 * bypass bit is set in tisp_top_get_bypass's result, and remember the last
 * value per channel so a repeated value is a no-op.  tisp_long_again_update
 * is an empty stub in the T33 binary (returns 0).
 *
 * The per-block update functions are hardware / shared-T32 code: they are
 * called through the ops table (the oracle records them as CALLS).
 */
#ifndef TX_ISP_T33_TGB_H
#define TX_ISP_T33_TGB_H

#include "tx_isp_t33_blk.h"

struct t33_tgB_ops {
	u32 (*top_get_bypass)(u32 ch);				/* tisp_top_get_bypass */
	void (*dpc_l_gain_update)(u32 ch, u32 gain);		/* tisp_dpc_l_gain_update */
	void (*blc_tgain_update)(u32 ch, u32 gain);		/* tisp_blc_tgain_update */
	void (*lsc_tgain_update)(u32 ch, u32 arg1, u32 gain, u32 force);	/* tisp_lsc_tgain_update */
	void (*dmsc_refresh)(u32 ch, u32 gain);			/* tisp_dmsc_refresh */
	void (*mdns_refresh)(u32 ch, u32 gain);			/* tisp_mdns_refresh */
	void (*ysp_refresh)(u32 ch, u32 gain);			/* tisp_ysp_refresh */
	void (*cdns_refresh)(u32 ch, u32 gain);			/* tisp_cdns_refresh */
	void (*ydns_refresh)(u32 ch, u32 gain);			/* tisp_ydns_refresh */
	void (*sdns_refresh)(u32 ch, u32 gain);			/* tisp_sdns_refresh */
	void (*rdns_refresh)(u32 ch, u32 gain);			/* tisp_rdns_refresh */
	void (*awb_tgain_update)(u32 ch, u32 arg1, u32 gain, u32 force);	/* tisp_awb_tgain_update */
	void (*af_tgain_update)(u32 ch, u32 gain);		/* tisp_af_tgain_update */
	void (*awb_ev_update)(u32 ch, u32 arg1, u32 ev0, u32 ev1);		/* tisp_awb_ev_update */
	void (*gamma_ev_update)(u32 ch, u32 arg1, u32 ev0, u32 ev1);	/* tisp_gamma_ev_update */
	void (*ccm_ev_update)(u32 ch, u32 arg1, u32 ev0, u32 ev1);		/* tisp_ccm_ev_update */
	void (*rdns_ev_update)(u32 ch, u32 arg1, u32 ev0, u32 ev1);	/* tisp_rdns_ev_update */
	void (*defog_ev_update)(u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5);	/* tisp_defog_ev_update */
	void (*bcsh_ev_update)(u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5);	/* tisp_bcsh_ev_update */
	void (*lce_ev_update)(u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5);	/* tisp_lce_ev_update */
	void (*tmo_ev_update)(u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5);	/* tisp_tmo_ev_update */
};

#define T33_TGB_CHANNELS	4
#define T33_TGB_EV_WORDS	8

/*
 * tisp_long_tgain_update(ch, arg1, gain, force): dispatch gain to the blocks
 * whose bypass bit is clear; arg1 is forwarded to the LSC and AWB updates.
 * Returns 0.  A repeated gain for the channel is a no-op when force is 0.
 */
static inline int t33_long_tgain_update(const struct t33_tgB_ops *o, u32 *tgain_old,
					u32 ch, u32 arg1, u32 gain, u32 force)
{
	u32 byp, a1 = arg1;	/* vendor: $a1 is left holding gain once a callee that takes it ran */

	if (tgain_old[ch] == gain && !force)
		return 0;
	byp = o->top_get_bypass(ch & 0xff);
	if (!(byp & 0x1)) {
		o->dpc_l_gain_update(ch, gain);
		a1 = gain;
	}
	if (!(byp & 0x2)) {
		o->blc_tgain_update(ch, gain);
		a1 = gain;
	}
	if (!(byp & 0x8))
		o->lsc_tgain_update(ch, a1, gain, force);
	if (!(byp & 0x40)) {
		o->dmsc_refresh(ch, gain);
		a1 = gain;
	}
	if (!(byp & 0x1000)) {
		o->mdns_refresh(ch, gain);
		a1 = gain;
	}
	if (!((byp >> 16) & 1)) {
		o->ysp_refresh(ch, gain);
		a1 = gain;
	}
	if (!((byp >> 18) & 1)) {
		o->cdns_refresh(ch, gain);
		a1 = gain;
	}
	if (!(byp & 0x2000)) {
		o->ydns_refresh(ch, gain);
		a1 = gain;
	}
	if (!((byp >> 17) & 1)) {
		o->sdns_refresh(ch, gain);
		a1 = gain;
	}
	if (!((byp >> 20) & 1)) {
		o->rdns_refresh(ch, gain);
		a1 = gain;
	}
	o->awb_tgain_update(ch, a1, gain, force);
	o->af_tgain_update(ch, gain);
	tgain_old[ch] = gain;
	return 0;
}

/*
 * tisp_long_ev_update(ch, arg1, ev0, ev1, a4, a5): dispatch the EV pair to
 * the blocks whose bypass bit is clear; arg1 is forwarded to every EV update,
 * a4 / a5 to the defog / bcsh / lce / tmo updates.  Returns 0.  A repeated
 * pair for the channel is a no-op.
 */
static inline int t33_long_ev_update(const struct t33_tgB_ops *o, u32 *ev_last,
				     u32 ch, u32 arg1, u32 ev0, u32 ev1, u32 a4, u32 a5)
{
	u32 byp = o->top_get_bypass(ch & 0xff);
	u32 *e = ev_last + 2 * ch;

	if (e[0] == ev0 && e[1] == ev1)
		return 0;
	o->awb_ev_update(ch, arg1, ev0, ev1);
	if (!(byp & 0x100))
		o->gamma_ev_update(ch, arg1, ev0, ev1);
	if (!(byp & 0x80))
		o->ccm_ev_update(ch, arg1, ev0, ev1);
	if (!((byp >> 20) & 1))
		o->rdns_ev_update(ch, arg1, ev0, ev1);
	if (!(byp & 0x200))
		o->defog_ev_update(ch, arg1, ev0, ev1, a4, a5);
	if (!(byp & 0x4000))
		o->bcsh_ev_update(ch, arg1, ev0, ev1, a4, a5);
	if (!(byp & 0x800))
		o->lce_ev_update(ch, arg1, ev0, ev1, a4, a5);
	if (!(byp & 0x20))
		o->tmo_ev_update(ch, arg1, ev0, ev1, a4, a5);
	e[0] = ev0;
	e[1] = ev1;
	return 0;
}

/* tisp_long_again_update: empty stub in the T33 binary */
static inline int t33_long_again_update(void)
{
	return 0;
}

#endif /* TX_ISP_T33_TGB_H */
