/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "bcshsw" (openimp #507): tisp_bcsh_switch_para's state part
 * (driver/t32/tx_isp_t33_bcsh.h t33_bcsh_switch_state) built for mipsel and run against the vendor
 * tisp_bcsh_switch_para of tx-isp-t33.ko by tools/ko_emu/diff_t33_bcshsw.py on the same bcsh_para block,
 * section and CT knots.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bcsh.h"

void emu_bcsh_sw(u8 *sec, const u8 *lut, u8 *st)
{
	t33_bcsh_switch_state(sec, lut, st);
}
