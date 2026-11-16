/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mscapop" (openimp #639): the done-FIFO state and the
 * lib_tisp_msca_addr_fifo_read wrapper (Y word 0xf0138 first, then UV 0xf0158, both & ~7
 * with stIspInitPar word 226 = 1) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_msca_api6.h"
#include "tx_isp_t33_mscapop.h"

u8 bo_init_par[4032];
u8 bo_cfg_mode;

u32 bo_tisp_msca_t33_addr_fifo_state(u32 id, u32 fifo)
{
	return t33_msca_addr_fifo_state(id, fifo);
}

void bo_lib_tisp_msca_addr_fifo_read(u32 id, u32 fifo, u32 *a, u32 *b)
{
	u32 m;

	memcpy(&m, bo_init_par + 904, 4);
	t33_msca_addr_fifo_read(m, bo_cfg_mode, id & 0xff, fifo & 0xff, a, b);
}
