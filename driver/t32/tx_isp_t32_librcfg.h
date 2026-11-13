/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library: register-config (rcfg) channel switch in the frame-done interrupt.
 *
 *   t32_lib_rcfg_switch_interrupt  lib_tisp_rcfg_switch_interrupt 0x1c874 (ch & 0xff): the previous channel
 *       (ch - 1, or channel count (dc608) - 1 for channel 0, both & 0xff) gets its rcfg control 0xd018 = 0 and
 *       the live values of 0xd088 / 0xd08c / 0xd090 copied into its register image (tisp_reg_write; shadow
 *       indexes 976 / 994 / 995 / 996 when d03a0 = 1); then the six DMA channel kick words (0xf0144, 0xf0164,
 *       0xf0244, 0xf0264, 0xf0344, 0xf0364) = 1 and returns system_reg_write(0x7120, 0).
 *
 * OS / register calls are hooks.  Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_librcfg_oracle).
 */
#ifndef TX_ISP_T32_LIBRCFG_H
#define TX_ISP_T32_LIBRCFG_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_RCFG_CTRL		0xd018U
#define T32_RCFG_CTRL_SH	976U
#define T32_RCFG_DONE		0x7120U

struct t32_rcfg_hooks {
	void *ctx;
	u32 (*sys_read)(void *ctx, u32 reg);				/* system_reg_read */
	u32 (*sys_write)(void *ctx, u32 reg, u32 val);			/* system_reg_write */
	u32 (*reg_write)(void *ctx, u32 ch, u32 reg, u32 val);		/* tisp_reg_write */
};

/* registers copied into the previous channel's image: address, shadow index */
static const u32 t32_rcfg_copy[3][2] = { { 0xd088, 994 }, { 0xd08c, 995 }, { 0xd090, 996 } };
static const u32 t32_rcfg_kick[6] = { 0xf0144, 0xf0164, 0xf0244, 0xf0264, 0xf0344, 0xf0364 };

/* ch_count = t32_data_dc608, shadow = t32_data_d03a0 */
static inline u32 t32_lib_rcfg_switch_interrupt(const struct t32_rcfg_hooks *h, u32 ch_count, const u8 *shadow,
						u32 ch)
{
	unsigned int i;
	u32 prev, v;

	ch &= 0xff;
	prev = (ch ? ch - 1 : ch_count - 1) & 0xff;
	h->reg_write(h->ctx, prev, *shadow ? T32_RCFG_CTRL_SH : T32_RCFG_CTRL, 0);
	for (i = 0; i < 3; i++) {
		v = h->sys_read(h->ctx, t32_rcfg_copy[i][0]);
		h->reg_write(h->ctx, prev, *shadow ? t32_rcfg_copy[i][1] : t32_rcfg_copy[i][0], v);
	}
	for (i = 0; i < 6; i++)
		h->sys_write(h->ctx, t32_rcfg_kick[i], 1);
	return h->sys_write(h->ctx, T32_RCFG_DONE, 0);
}

#endif
