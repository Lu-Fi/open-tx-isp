/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T32_MSCA_DONE_H
#define TX_ISP_T32_MSCA_DONE_H

/*
 * Done address words (Y_DONE / UV_DONE): the vendor tisp_msca_addr_fifo_read
 * drops the low three bits of both words (and v0, v0, -8) before it matches
 * the address with a queued buffer; the hardware may return flag bits there
 * (seen on the T33 silicon, openimp #393 / #529).
 */
#define T32_MSCA_DONE_ADDR(v)		((v) & ~7U)

#endif
