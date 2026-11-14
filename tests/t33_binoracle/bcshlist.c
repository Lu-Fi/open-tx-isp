/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "bcshlist" (openimp #507): who writes list 18 when.  The day / night switch
 * (t33_bcsh_switch_para: the state only, no list word), the EV update (t33_bcsh_ev_update) and the CT update
 * (t33_bcsh_ct_update) of driver/t32/tx_isp_t33_bcsh.h, built for mipsel and run in sequences against the vendor
 * tisp_bcsh_switch_para / tisp_bcsh_ev_update / tisp_bcsh_ct_update / tisp_bcsh_irq_frame_down of tx-isp-t33.ko
 * by tools/ko_emu/diff_t33_bcshlist.py.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bcsh.h"

int emu_bcsh_sw(u8 *sec, const u8 *lut, u8 *st, u32 *l)
{
	return t33_bcsh_switch_para(sec, lut, st, l);
}

int emu_bcsh_ev(u8 *sec, u8 *st, u32 lo, u32 hi, u32 bvl, u32 bvh, u32 *l)
{
	return t33_bcsh_ev_update(st, sec, lo, hi, bvl, bvh, l);
}

int emu_bcsh_ct(u8 *sec, const u8 *lut, u8 *st, int ct, u32 *l)
{
	return t33_bcsh_ct_update(st, sec, lut, ct, l);
}
