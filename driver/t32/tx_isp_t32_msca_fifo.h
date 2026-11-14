/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA done address FIFO read (vendor tisp_msca_addr_fifo_read): the two
 * address words of FIFO f, through the register list (cfgMode != 0) or the
 * registers, both with the low three bits dropped.  The driver reads the
 * registers itself (t32_hw_msca_pop_done) and shares T32_MSCA_DONE_ADDR;
 * this port is what the binary oracle block t32-mscafifo runs against the
 * vendor module.
 */
#ifndef TX_ISP_T32_MSCA_FIFO_H
#define TX_ISP_T32_MSCA_FIFO_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t32_msca_done.h"

extern u32 system_reg_read(u32 addr);
extern u32 tisp_reg_read(u32 id, u32 reg);

static inline void t32_msca_addr_fifo_read(u32 cfg_mode, u32 id, u32 fifo, u32 *y, u32 *uv)
{
	u32 f = fifo & 0xff;

	if (cfg_mode & 0xff) {
		*y = T32_MSCA_DONE_ADDR(tisp_reg_read(id & 0xff, (f << 5) + 52));
		*uv = T32_MSCA_DONE_ADDR(tisp_reg_read(id & 0xff, (f << 5) + 60));
		return;
	}
	*y = T32_MSCA_DONE_ADDR(system_reg_read(0xf0138 + (f << 8)));
	*uv = T32_MSCA_DONE_ADDR(system_reg_read(0xf0158 + (f << 8)));
}

#endif
