/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 csccr block: frame-done interrupt handler (vendor tisp_csccr_t33_frame_done_interrupt).
 *
 * The callees (tisp_rcfg_module_cfg_trig, tisp_mdns_dn_params_api2, tisp_csccr_set_day,
 * tisp_mdns_dn_params_api) must be declared by the includer.
 */
#ifndef TX_ISP_T33_FD_CSCCR_H
#define TX_ISP_T33_FD_CSCCR_H

#include "tx_isp_t33_blk.h"

#define T33_CSCCR_MOD		22
#define T33_INITPAR_MODE	904	/* stIspInitPar word: 1 = list refresh active */
#define T33_INITPAR_CH_SIZE	1008	/* per-channel stride of stIspInitPar */
#define T33_INITPAR_DAY_FLAG	41	/* per-channel byte: 0 -> set day mode */

/*
 * par: stIspInitPar, refresh: tispCsccrRefresh, cnt/cur/cur2: frameCount / curFrame / curFrame2
 * (u32 per channel).  Returns 1.
 */
static inline int t33_csccr_frame_done(u32 ch, const u8 *par, u8 *refresh, u32 *cnt, u32 *cur, u32 *cur2)
{
	if (t33_le32(par + T33_INITPAR_MODE) == 1 && *refresh == 1) {
		tisp_rcfg_module_cfg_trig(ch & 0xff, T33_CSCCR_MOD);
		*refresh = 0;
	}
	cnt[ch] = (cnt[ch] + 1) & 0xffffff;
	if (cnt[ch] == cur[ch]) {
		tisp_mdns_dn_params_api2(ch);
		if (par[ch * T33_INITPAR_CH_SIZE + T33_INITPAR_DAY_FLAG] == 0)
			tisp_csccr_set_day(ch & 0xff);
		cur[ch] = 0xffffffffU;
	}
	if (cur2[ch] == cnt[ch]) {
		tisp_mdns_dn_params_api(ch);
		cur2[ch] = 0xffffffffU;
	}
	return 1;
}

#endif /* TX_ISP_T33_FD_CSCCR_H */
