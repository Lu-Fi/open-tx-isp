/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "msca-api6" */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_msca_api6.h"

u8 bo_init_par[4032];
u8 bo_cfg_mode;
u8 *bo_msca_attr[3];
u32 bo_fcrop[60];

static u32 init_mode(void)
{
	u32 m;

	memcpy(&m, bo_init_par + 904, 4);
	return m;
}

void bo_tisp_msca_t33_addr_fifo_read(u32 id, u32 fifo, u32 *a, u32 *b)
{
	t33_msca_addr_fifo_read(init_mode(), bo_cfg_mode, id, fifo, a, b);
}

void bo_tisp_msca_t33_addr_fifo_write(u32 id, u32 fifo, u32 a, u32 b)
{
	t33_msca_addr_fifo_write(init_mode(), bo_cfg_mode, id, fifo, a, b);
}

void bo_tisp_msca_t33_channel_attr_set(u32 ch, u8 *a)
{
	t33_msca_channel_attr_set(ch, (const u32 *)a, bo_msca_attr, bo_fcrop);
}
