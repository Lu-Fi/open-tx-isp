/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AWB / AF gain update entry points (openimp #99, block tgA):
 *  - tisp_awb_tgain_update: store the AWB interpolation point (word 0x434 of
 *    the AWB state, i.e. at state + 0x10000 + 0x434) and re-run the regional
 *    threshold / lum threshold register writes, then flag tispAwbRefresh;
 *  - tisp_awb_ev_update: store the EV (>> 10) at state + 0x10000 + 0x430;
 *  - tisp_af_tgain_update: store the AF total gain in total_gain[ch].
 *
 * awbInfo[ch] is a pointer to the AWB descriptor; its word 1 is the AWB
 * state base.  The vendor adds 0x10000 to that base before the stores.
 */
#ifndef TX_ISP_T33_TGA_H
#define TX_ISP_T33_TGA_H

#include "tx_isp_t33_blk.h"

/* word index of the AWB state word the vendor writes (0x10000 + 0x434 / 0x430) */
#define T33_AWB_W_TGAIN		((0x10000 + 0x434) / 4)
#define T33_AWB_W_EV		((0x10000 + 0x430) / 4)

/* tisp_awb_tgain_update */
static inline void t33_awb_tgain_update(u32 **awb_info, u32 ch, u32 tgain, u8 *refresh)
{
	u32 *state = (u32 *)(uintptr_t)awb_info[ch][1];

	state[T33_AWB_W_TGAIN] = tgain;
	*refresh = 1;
}

/* tisp_awb_ev_update */
static inline u32 t33_awb_ev_update(u32 **awb_info, u32 ch, u32 ev)
{
	u32 *state = (u32 *)(uintptr_t)awb_info[ch][1];

	state[T33_AWB_W_EV] = ev >> 10;
	return 0;
}

/* tisp_af_tgain_update */
static inline void t33_af_tgain_update(u32 *total_gain, u32 ch, u32 gain)
{
	total_gain[ch] = gain;
}

#endif /* TX_ISP_T33_TGA_H */
