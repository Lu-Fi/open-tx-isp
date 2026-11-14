/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "bcshct" (openimp #507): tisp_bcsh_ct_update
 * (driver/t32/tx_isp_t33_bcsh.h t33_bcsh_ct_update) built for mipsel and run against the vendor
 * tisp_bcsh_ct_update of tx-isp-t33.ko by tools/ko_emu/diff_t33_bcshct.py on the same bcsh_para block,
 * section, CT knots and CT.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bcsh.h"

int emu_bcsh_ct(u8 *sec, const u8 *lut, u8 *st, int ct, u32 *l)
{
	return t33_bcsh_ct_update(st, sec, lut, ct, l);
}
