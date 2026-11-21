/* SPDX-License-Identifier: GPL-2.0 */
/*
 * binary oracle, block "t32-lcetcs": tisp_lce_top_change_state of the vendor tx-isp-t32.ko
 * (T32_OEM_KO) vs driver/t32/tx_isp_t32_lcetcs.h (openimp #717).  Register stores and reads go
 * through tisp_reg_write / tisp_reg_read (recorded calls); the vendor's tisp_lce_write_curve / write_ctrl_reg /
 * write_all_reg run unchanged on its side.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_lcetcs.h"

u8 *lce_info[4];
u8 lceBuffInfo[4 * 36];
u8 lce_frame_flag[4];
u8 cfgMode;
extern void tisp_reg_write(u32 ch, u32 reg, u32 val);
extern void private_dma_cache_sync(u32 a, void *p, u32 len, u32 d);

static void bo_wr(u32 ch, u32 reg, u32 val) { tisp_reg_write(ch, reg, val); }
static void bo_sync(void *p, u32 len) { private_dma_cache_sync(0, p, len, 0); }
static const struct t32_lcetcs_ops bo_lops = { bo_wr, bo_sync };

void bo_tisp_lce_top_change_state(u32 ch)
{
	struct t32_lcetcs l = { lce_info[ch], (u8 *)(uintptr_t)t32_lcetcs_rd32(lceBuffInfo + 36 * ch + 12),
				t32_lcetcs_rd32(lceBuffInfo + 36 * ch + 24), &lce_frame_flag[ch] };

	t32_lcetcs_top_change_state(&bo_lops, ch, &l);
}
