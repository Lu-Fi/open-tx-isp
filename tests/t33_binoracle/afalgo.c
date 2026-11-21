/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "afalgo": tisp_af_alogrithm (driver/t32/tx_isp_t33_afalgo.h t33_af_alogrithm) with the
 * vendor calling convention and globals (spec: afalgo.conf, openimp #437).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_afalgo.h"

extern u32 system_reg_read(u32 reg);

u8 *af_info[4];
u8 *af_buf_info[4];
u32 fv_old[4];

int bo_tisp_af_alogrithm(u32 ch)
{
	u8 *info, *sec, *base;
	u32 bank;

	ch &= 0xff;
	info = af_info[ch];
	sec = (u8 *)(uintptr_t)t33_afa_rd32(info + 0x4e8);
	bank = system_reg_read(0x1811c) & 3;
	base = (u8 *)(uintptr_t)t33_afa_rd32(af_buf_info[ch] + 4);
	t33_af_alogrithm(info, sec, base + bank * T33_AFA_BANK_BYTES, &fv_old[ch]);
	return 0;
}
