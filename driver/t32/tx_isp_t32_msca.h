/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA frame-channel layer: the T23 channel packing, moved to 0xf0000.
 *
 * Channel N registers live at T32_MSCA_CH(N) = 0xf0100 + N * 0x100 with the
 * T23 field layout (driver/t23, QEMU ingenic-isp.c notes), confirmed by the
 * S0 vendor trace (r10/r11/r13):
 *   +0x04 scaled size (w << 16 | h)      0x07800438 for 1920x1080
 *   +0x08 crop position, +0x0c crop size
 *   +0x30 Y push, +0x38 Y done (read pops), +0x3c FIFO state (bit 0 empty)
 *   +0x40 Y stride, +0x44 Y FIFO clear
 *   +0x50 UV push, +0x58 UV done (read before the Y pop), +0x60 UV stride,
 *   +0x64 UV FIFO clear, +0x48/+0x68 done-FIFO clears
 * There is no frame number register (T31/T41 have one).  The vendor driver
 * writes 0 to the FIFO clears right after a push, so only bit 0 clears.
 * Channel done status is its own word (T32_CHDONE_STA bit N); the global
 * update request is T32_MSCA_UPDATE = 1 after reprogramming.
 *
 * The geometry rules are the T23 ones and are shared, not copied
 * (driver/t23/tx_isp_t23_msca_geom.h).
 */
#ifndef TX_ISP_T32_MSCA_H
#define TX_ISP_T32_MSCA_H

#include "tx_isp_t32_regs.h"
#include "../t23/tx_isp_t23_msca_geom.h"

#define T32_MSCA_CHANNELS		3

#define T32_MSCA_SCL_SIZE		0x04
#define T32_MSCA_CROP_POS		0x08
#define T32_MSCA_CROP_SIZE		0x0c
#define T32_MSCA_Y_PUSH			0x30
#define T32_MSCA_Y_DONE			0x38
#define T32_MSCA_FIFO_STATE		0x3c
#define T32_MSCA_Y_STRIDE		0x40
#define T32_MSCA_Y_CLR			0x44
#define T32_MSCA_Y_DONE_CLR		0x48
#define T32_MSCA_UV_PUSH		0x50
#define T32_MSCA_UV_DONE		0x58
#define T32_MSCA_UV_STRIDE		0x60
#define T32_MSCA_UV_CLR			0x64
#define T32_MSCA_UV_DONE_CLR		0x68

#define T32_MSCA_FIFO_EMPTY		(1U << 0)
#define T32_MSCA_FIFO_COUNT(v)		(((v) >> 8) & 0xf)

/* IVDC direct-mode addresses for channel 0 (sensor number in bits 26:25) */
#define T32_IVDC_Y_ADDR(sensor)		(0x80000000U | ((sensor) << 25))
#define T32_IVDC_UV_ADDR(sensor)	(0x81000000U | ((sensor) << 25))

/* geometry as the 96-byte QBUF carries it (the only place T32 libimp puts it) */
#define T32_QBUF_SIZE_WORD		19	/* h << 16 | w */
#define T32_QBUF_STRIDE_WORD		17
#define T32_QBUF_Y_PA_WORD		13
#define T32_QBUF_LENGTH_WORD		14
#define T32_QBUF_UV_PA_WORD		16

static inline u32 t32_msca_size_word(u32 w, u32 h)
{
	return (w << 16) | h;		/* MSCA order: width high */
}

#endif /* TX_ISP_T32_MSCA_H */
