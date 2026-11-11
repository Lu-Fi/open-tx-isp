/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library setters: CSC colour-range switch and WDR on / off.
 *
 *   t32_lib_set_csccr        lib_tisp_set_csccr 0xb043c (ch, mode; both & 0xff):
 *       channel not running (dc060[ch] == 0): remember the mode for the start (e4018[ch]); returns 0
 *       channel running (dc060[ch] == 1): mode 0: state byte 37 = 0, the CSCCR frame marks
 *           f32f8[ch] / f32e8[ch] = f32d8[ch] + 1 / + 2 (24 bit); mode 1: state byte 37 = 1, tisp_reg_write
 *           of the CSCCR control 0x14054 = 0 and 0x14010 = 0xffffffff (shadow indexes 777 / 771 when
 *           d03a0 = 1); returns 0; other modes: log, -1
 *       other dc060 values: 0, nothing changed
 *   t32_lib_set_wdr_setting  lib_tisp_set_wdr_setting 0xb05a8 (attr: ch = byte 17, mode = word 8):
 *       only for a running channel (dc060[ch] == 1), else 0.  State byte 36 = (mode == 1).  Top control
 *       (system register 0x88, or 0x4000 when d03a0 = 1): WDR on = bit 16 | bits 8..9 of (state word 8 >> 8)
 *       or-ed into the read value; WDR off = bit 16 cleared, bit 9 of (first read >> 8) or-ed into a second
 *       read.  Then system register 0x20 (0x4000 when d03a0 = 1) = 0xc7ff1000; returns 0.
 *
 * No channel range check in the vendor (vendor behaviour, kept).  OS / register calls are hooks.
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libset_oracle).
 */
#ifndef TX_ISP_T32_LIBSET_H
#define TX_ISP_T32_LIBSET_H

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

#define T32_SET_CH_STATE	904U	/* t32_data_dc284 block per channel */
#define T32_SET_ST_WDR		36U	/* byte */
#define T32_SET_ST_CSCCR	37U	/* byte */
#define T32_SET_ST_W8		8U	/* u32 */
#define T32_CH_RUNNING		1U	/* t32_data_dc060[ch] */

#define T32_CSCCR_CTRL		0x14054U
#define T32_CSCCR_CTRL_SH	777U
#define T32_CSCCR_MASK		0x14010U
#define T32_CSCCR_MASK_SH	771U

#define T32_WDR_TOP_REG		0x88U
#define T32_WDR_SHADOW_REG	0x4000U
#define T32_WDR_REG20		0x20U
#define T32_WDR_EN		0x10000U
#define T32_WDR_REG20_VAL	0xc7ff1000U

struct t32_set_hooks {
	void *ctx;
	u32 (*sys_read)(void *ctx, u32 reg);				/* system_reg_read */
	u32 (*sys_write)(void *ctx, u32 reg, u32 val);			/* system_reg_write */
	u32 (*reg_write)(void *ctx, u32 ch, u32 reg, u32 val);		/* tisp_reg_write */
	void (*log)(void *ctx, u32 level, u32 line, u32 val);		/* isp_printf */
};

/* the per channel vendor data the setters use */
struct t32_set_data {
	const u8 *running;	/* t32_data_dc060 */
	u8 *state;		/* t32_data_dc284 */
	u8 *csccr_start;	/* t32_data_e4018 */
	const u32 *frame;	/* t32_data_f32d8 */
	u32 *csccr_mark1;	/* t32_data_f32f8 */
	u32 *csccr_mark2;	/* t32_data_f32e8 */
	const u8 *shadow;	/* t32_data_d03a0 */
};

static inline u32 t32_lib_set_csccr(const struct t32_set_hooks *h, const struct t32_set_data *d, u32 ch, u32 mode)
{
	u8 *st;

	ch &= 0xff;
	mode &= 0xff;
	if (d->running[ch] != T32_CH_RUNNING) {
		if (d->running[ch] == 0)
			d->csccr_start[ch] = (u8)mode;
		return 0;
	}
	st = d->state + T32_SET_CH_STATE * ch;
	if (mode == 0) {
		st[T32_SET_ST_CSCCR] = 0;
		d->csccr_mark1[ch] = (d->frame[ch] + 1) & 0xffffff;
		d->csccr_mark2[ch] = (d->frame[ch] + 2) & 0xffffff;
		return 0;
	}
	if (mode == 1) {
		st[T32_SET_ST_CSCCR] = 1;
		h->reg_write(h->ctx, ch, *d->shadow ? T32_CSCCR_CTRL_SH : T32_CSCCR_CTRL, 0);
		h->reg_write(h->ctx, ch, *d->shadow ? T32_CSCCR_MASK_SH : T32_CSCCR_MASK, 0xffffffffU);
		return 0;
	}
	h->log(h->ctx, 2, 298, mode);
	return (u32)-1;
}

static inline u32 t32_lib_set_wdr_setting(const struct t32_set_hooks *h, const struct t32_set_data *d,
					  const u8 *attr)
{
	u32 ch = attr[17], mode = *(const u32 *)(attr + 8), reg, v, keep;
	u8 *st;

	if (d->running[ch] != T32_CH_RUNNING)
		return 0;
	st = d->state + T32_SET_CH_STATE * ch;
	reg = *d->shadow ? T32_WDR_SHADOW_REG : T32_WDR_TOP_REG;
	if (mode == 1) {
		st[T32_SET_ST_WDR] = 1;
		keep = (*(const u32 *)(st + T32_SET_ST_W8) >> 8) & 0x300;
		v = h->sys_read(h->ctx, reg);
		v = keep | T32_WDR_EN | v;
	} else {
		st[T32_SET_ST_WDR] = 0;
		keep = (h->sys_read(h->ctx, reg) >> 8) & 0x200;
		v = (keep | h->sys_read(h->ctx, reg)) & ~T32_WDR_EN;
	}
	h->sys_write(h->ctx, reg, v);
	h->sys_write(h->ctx, *d->shadow ? T32_WDR_SHADOW_REG : T32_WDR_REG20, T32_WDR_REG20_VAL);
	return 0;
}

#endif
