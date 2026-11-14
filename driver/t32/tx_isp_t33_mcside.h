/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) module-control side effects: the two tiny state changes the
 * vendor's top-level control code performs (openimp #99).
 *
 * tisp_lce_top_change_state(ch): lceInfo[ch] is the per-channel LCE block;
 * word 25336 of it points at the LCE section, whose byte 196 selects the
 * curve source: 1 -> the default curve (section byte 5 = 3, then
 * tisp_lce_curve_init_default(ch)), anything else -> section byte 5 = 4.
 *
 * tisp_mdns_start(ch): mdnsCfgMode selects the register (0xd018, or 2 when
 * the mode byte is non-zero), tisp_reg_write_t33(id, 15, reg, 1) starts the
 * MDNS block and g_mdnsStart is set to 1.
 */
#ifndef TX_ISP_T33_MCSIDE_H
#define TX_ISP_T33_MCSIDE_H

#include "tx_isp_t33_blk.h"

/* tisp_lce_top_change_state: returns 1 when the default curve was requested */
static inline int t33_lce_top_change_state(u32 *lce, u32 ch)
{
	u32 *outer = (u32 *)lce[ch];
	u8 *sec = (u8 *)outer[25336 / 4];

	if (sec[196] == 1) {
		sec[5] = 3;
		return 1;
	}
	sec[5] = 4;
	return 0;
}

/* tisp_mdns_start: returns the register written (0xd018 or 2) */
static inline u32 t33_mdns_start(u8 mode)
{
	return mode ? 2 : 0xd018;
}

#endif /* TX_ISP_T33_MCSIDE_H */
