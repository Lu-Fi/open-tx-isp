/* SPDX-License-Identifier: GPL-2.0 */
/* binary oracle, block "t32-mscafifo": runs against the vendor tx-isp-t32.ko (T32_OEM_KO) */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_msca_fifo.h"

u8 bo_cfg_mode;

void bo_tisp_msca_addr_fifo_read(u32 id, u32 fifo, u32 *y, u32 *uv)
{
	t32_msca_addr_fifo_read(bo_cfg_mode, id, fifo, y, uv);
}
