/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) module-control side effects: the two state changes the vendor's tisp_s_module_control performs
 * (openimp #99, oracle block mcside).
 *
 * tisp_lce_top_change_state(ch): section byte 196 of the channel's LCE section selects the curve source:
 * 1 -> section byte 5 = 3 and tisp_lce_curve_init_default(ch), anything else -> section byte 5 = 4.
 *
 * tisp_mdns_start(ch): tisp_reg_write_t33(ch, MDNS list 15, slot 2 / register 0xd018 by mdnsCfgMode, 1) starts the
 * MDNS block, g_mdnsStart = 1.
 */
#ifndef TX_ISP_T33_MCSIDE_H
#define TX_ISP_T33_MCSIDE_H

#include "tx_isp_t33_blk.h"

#define T33_MCS_MDNS_LIST	15

extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);

/* sec: the LCE section lceInfo[ch] points at (word 25336) */
static inline void t33_lce_top_change_state(u8 *sec, u32 ch, void (*curve_default)(u32 ch))
{
	if (sec[196] == 1) {
		sec[5] = 3;
		curve_default(ch);
	} else {
		sec[5] = 4;
	}
}

static inline void t33_mdns_start(u32 ch, u8 cfg_mode, u8 *started)
{
	tisp_reg_write_t33(ch, T33_MCS_MDNS_LIST, cfg_mode ? 2 : 0xd018, 1);
	*started = 1;
}

#endif /* TX_ISP_T33_MCSIDE_H */
