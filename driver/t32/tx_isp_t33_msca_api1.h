/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) MSCA API wrappers: the vendor's lib_tisp_msca_* thin
 * functions mask their arguments to 8 bits and tail-call the real
 * tisp_msca_t33_* implementation (tx_isp_t32_hw.c / open runtime).
 *
 * The vendor code is a tail call (jr t9 after the andi masks), so the
 * port is just the argument masking plus the call to the callee, which
 * is declared extern here and recorded/compared by the oracle.
 */
#ifndef TX_ISP_T33_MSCA_API1_H
#define TX_ISP_T33_MSCA_API1_H

#include "tx_isp_t33_blk.h"

/* tisp_msca_t33_addr_fifo_read / _write / _chx_en: ported in tx_isp_t32_hw.c */
extern int tisp_msca_t33_addr_fifo_read(u32 addr, u32 data);
extern int tisp_msca_t33_addr_fifo_write(u32 addr, u32 data);
extern int tisp_msca_t33_chx_en(u32 ch, u32 x, u32 en);

/* lib_tisp_msca_addr_fifo_read: mask both args to 8 bits, tail-call */
static inline int t33_msca_addr_fifo_read(u32 addr, u32 data)
{
	return tisp_msca_t33_addr_fifo_read(addr & 0xff, data & 0xff);
}

/* lib_tisp_msca_addr_fifo_write: mask both args to 8 bits, tail-call */
static inline int t33_msca_addr_fifo_write(u32 addr, u32 data)
{
	return tisp_msca_t33_addr_fifo_write(addr & 0xff, data & 0xff);
}

/* lib_tisp_msca_chx_en: mask all three args to 8 bits, tail-call */
static inline int t33_msca_chx_en(u32 ch, u32 x, u32 en)
{
	return tisp_msca_t33_chx_en(ch & 0xff, x & 0xff, en & 0xff);
}

#endif /* TX_ISP_T33_MSCA_API1_H */
