/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "clmct" (openimp #507): tisp_clm_ct_interp not forced (the CLM CT update,
 * driver/t32/tx_isp_t33_clm.h t33_clm_ct_gate + t33_clm_ct) on the vendor's clm_info layout (kept CT word
 * 0x18, segment byte 0xcbc, hysteresis u16 0x898, tables at 0x64 / 0x89a), run against the vendor by
 * tools/ko_emu/diff_t33_clmct.py.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_clm.h"

int emu_clm_ct(u8 *info, const u8 *sec, const u8 *lut, u32 lutbytes, int ct)
{
	if (t33_clm_ct_gate(sec, lut, ct, (u32 *)(info + 0x60), info + 0xcbc, t32_tp_le16(info + 0x898)))
		return 1;
	return t33_clm_ct(sec, lut, lutbytes, ct, info + 0x89a, (u16 *)(info + 0x64));
}
