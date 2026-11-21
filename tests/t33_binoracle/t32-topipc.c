/* SPDX-License-Identifier: GPL-2.0 */
/*
 * binary oracle, block "t32-topipc": tisp_top_ipc_get_mem_size / tisp_top_ipc_set_mem_cfg of the vendor
 * tx-isp-t32.ko (T32_OEM_KO) vs driver/t32/tx_isp_t32_front.h (openimp #759, now used at GET / SET_ISP_IPC_MEM).
 * The config words are the vendor's stMemInfo (word 9 = IPC base, word 13 = size); the register writes go
 * through system_reg_write (recorded, in order; the last one a tail call).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_front.h"

u32 bo_mem[19];
extern void system_reg_write(u32 reg, u32 val);

u32 bo_tisp_top_ipc_get_mem_size(u32 w, u32 raw)
{
	return t32_top_ipc_get_mem_size(w, raw, &bo_mem[13]);
}

void bo_tisp_top_ipc_set_mem_cfg(u32 base)
{
	struct t32_front_write w[6];
	unsigned int i, n = t32_top_ipc_set_mem_cfg(bo_mem, base, w);

	for (i = 0; i < n; i++)
		system_reg_write(w[i].reg, w[i].val);
}
