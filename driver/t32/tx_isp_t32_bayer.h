/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tisp register read and the Bayer-order API:
 *
 *   t32_tisp_reg_read          tisp_reg_read 0x1c74c
 *   t32_api_get_bayer_type     tisp_api_get_bayer_type 0x29fc4, lib_tisp_api_get_bayer 0xb32a4
 *   t32_api_set_bayer_type     tisp_api_set_bayer_type 0x2a018, lib_tisp_api_set_bayer 0xb32b8
 *
 * tisp_reg_read: register shadow mode (t32_data_d03a0 == 1) reads word reg of the channel's shadow image,
 * otherwise system_reg_read(reg).  The Bayer order is the low byte of the top register 0x78 (shadow word 9
 * when the mode byte is non-zero; note the vendor tests != 0 here but == 1 in tisp_reg_read).  The set
 * keeps the upper 24 bits and writes through tisp_reg_write.  A channel >= 4 (low byte) is refused with
 * isp_printf(2, ...) and the print's return value is returned.  The lib_ wrappers only take the low
 * byte of both arguments.
 *
 * Hooks: struct t32_regio (system register access, the shadow image, tisp_reg_write, the error print).
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle
 * (t32_bayer_oracle).
 */
#ifndef TX_ISP_T32_BAYER_H
#define TX_ISP_T32_BAYER_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_TOP_BAYER_REG	0x78U	/* direct register */
#define T32_TOP_BAYER_SHADOW	9U	/* shadow image word */

struct t32_regio {
	void *ctx;
	unsigned char mode;					/* t32_data_d03a0 */
	u32 (*sys_read)(void *ctx, u32 reg);			/* system_reg_read */
	u32 *(*shadow)(void *ctx, u32 ch);			/* shadow image of channel ch */
	u32 (*reg_write)(void *ctx, u32 ch, u32 reg, u32 val);	/* tisp_reg_write */
	u32 (*error)(void *ctx, u32 line);			/* isp_printf(2, fmt, func, line) */
};

/* tisp_reg_read(ch, reg) */
static inline u32 t32_tisp_reg_read(const struct t32_regio *io, u32 ch, u32 reg)
{
	if (io->mode == 1)
		return io->shadow(io->ctx, ch)[reg];
	return io->sys_read(io->ctx, reg);
}

static inline u32 t32_bayer_reg(const struct t32_regio *io)
{
	return io->mode ? T32_TOP_BAYER_SHADOW : T32_TOP_BAYER_REG;
}

/* tisp_api_get_bayer_type(ch) */
static inline u32 t32_api_get_bayer_type(const struct t32_regio *io, u32 ch)
{
	ch &= 0xff;
	if (ch >= 4)
		return io->error(io->ctx, 357);
	return t32_tisp_reg_read(io, ch, t32_bayer_reg(io));
}

/* tisp_api_set_bayer_type(ch, bayer) */
static inline u32 t32_api_set_bayer_type(const struct t32_regio *io, u32 ch, u32 bayer)
{
	u32 v;

	ch &= 0xff;
	if (ch >= 4)
		return io->error(io->ctx, 369);
	v = t32_tisp_reg_read(io, ch, t32_bayer_reg(io));
	return io->reg_write(io->ctx, ch, t32_bayer_reg(io), (v & 0xffffff00U) | (bayer & 0xff));
}

#endif
