/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA done-FIFO state (vendor tisp_msca_t33_addr_fifo_state, binary
 * oracle block "mscapop"; the read itself is tisp_msca_t33_addr_fifo_read,
 * tx_isp_t33_msca_api6.h).  1 while FIFO f holds a done entry: bit 0 of
 * 0xf013c + f * 0x100 clear.
 */
#ifndef TX_ISP_T33_MSCAPOP_H
#define TX_ISP_T33_MSCAPOP_H

#include "tx_isp_t33_blk.h"

extern u32 system_reg_read(u32 addr);

/* tisp_msca_t33_addr_fifo_state (id unused, as the vendor) */
static inline u32 t33_msca_addr_fifo_state(u32 id, u32 fifo)
{
	(void)id;
	return (system_reg_read(0xf013c + ((fifo & 0xff) << 8)) & 1) ^ 1;
}

#endif /* TX_ISP_T33_MSCAPOP_H */
