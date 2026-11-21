/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LCE top state change (openimp #717): readable C of the vendor's
 * tisp_lce_top_change_state (0x69338) with the functions it runs,
 * tisp_lce_curve_init_default (0x6738c, inlined there), tisp_lce_write_all_reg
 * (0x67258), tisp_lce_write_curve (0x66ff4) and tisp_lce_write_ctrl_reg
 * (0x66c0c), in direct mode (cfgMode 0).  tisp_s_module_control calls it when
 * bit 4 or 10 of the top enable word changes.
 *
 * The LCE state (lce_info[ch], 0x22850 B) starts with the 32-bit address of
 * the parameter section in use.  Parameter byte 214 == 1: refresh mode 3
 * (+0x22830), both default curves (+0x1c2a4: 576 rows of the 20 byte pattern,
 * written to the second half of the curve DMA buffer, then +0x1efa4 and the
 * first row again at +0x223e7) and the control registers; else mode 4 only.
 * write_all_reg copies the second curve half over the first, syncs it and
 * points 0xc0a4 at the buffer only while the channel's lce_frame_flag is 0
 * (the first call after tisp_lce_init), then sets the flag.  The control
 * registers come from the section (+0x1ab .. +0x1af, +0x1a0 .. +0x1a6) and the
 * state (nine u16 at +0x2279a, the Gauss words at +0x22746, a u16 at +0x22762).
 *
 * Checked against the vendor tx-isp-t32.ko by tests/t33_binoracle (t32-lcetcs).
 */
#ifndef TX_ISP_T32_LCETCS_H
#define TX_ISP_T32_LCETCS_H

#include "tx_isp_t32_lce.h"

#define T32_LCETCS_MODE		0x22830U
#define T32_LCETCS_CURVE0	0x1c2a4U
#define T32_LCETCS_CURVE1	0x1efa4U
#define T32_LCETCS_ROW0		0x223e7U
#define T32_LCETCS_HALF		0x2d00U
#define T32_LCETCS_PAR_TOP	214

struct t32_lcetcs_ops {
	void (*reg_write)(u32 ch, u32 reg, u32 val);	/* tisp_reg_write */
	void (*cache_sync)(void *p, u32 len);		/* private_dma_cache_sync(0, p, len, 0) */
};

struct t32_lcetcs {
	u8 *st;			/* lce_info[ch] */
	u8 *curve;		/* lceBuffInfo[ch] +12: the curve DMA buffer (two halves) */
	u32 curve_pa;		/* lceBuffInfo[ch] +24 */
	u8 *frame_flag;		/* lce_frame_flag[ch] */
};

static inline u32 t32_lcetcs_rd16(const u8 *p) { return p[0] | (u32)p[1] << 8; }
static inline u32 t32_lcetcs_rd32(const u8 *p) { return t32_lcetcs_rd16(p) | t32_lcetcs_rd16(p + 2) << 16; }

static inline const u8 *t32_lcetcs_sec(const u8 *st)
{
	return (const u8 *)(uintptr_t)t32_lcetcs_rd32(st);
}

/* tisp_lce_write_ctrl_reg */
static inline void t32_lcetcs_write_ctrl_reg(const struct t32_lcetcs_ops *op, u32 ch, const u8 *st)
{
	const u8 *sec = t32_lcetcs_sec(st);
	unsigned int k;

	op->reg_write(ch, 0xc068, (sec[0x1ac] & 1U) << 4 | (sec[0x1ad] & 3U) << 8 | (sec[0x1ab] & 1U) |
		      (sec[0x1ae] & 1U) << 12 | (sec[0x1af] & 1U) << 16);
	for (k = 0; k < 4; k++)
		op->reg_write(ch, 0xc06c + 4 * k, (t32_lcetcs_rd16(st + 0x2279c + 4 * k) & 0x1ffU) << 16 |
			      (t32_lcetcs_rd16(st + 0x2279a + 4 * k) & 0x1ff));
	op->reg_write(ch, 0xc07c, t32_lcetcs_rd16(st + 0x227aa) & 0x1ff);
	op->reg_write(ch, 0xc060, (t32_lcetcs_rd16(sec + 0x1a2) & 0x3ffU) << 16 | (t32_lcetcs_rd16(sec + 0x1a0) & 0x3ff));
	op->reg_write(ch, 0xc064, (t32_lcetcs_rd16(sec + 0x1a6) & 0x3ffU) << 16 | (t32_lcetcs_rd16(sec + 0x1a4) & 0x3ff));
	for (k = 0; k < 7; k++)
		op->reg_write(ch, 0x25054 + 4 * k, t32_lcetcs_rd32(st + 0x22746 + 4 * k));
	op->reg_write(ch, 0x25070, t32_lcetcs_rd16(st + 0x22762));
	op->reg_write(ch, 0xc010, 1);
}

/* tisp_lce_write_all_reg: tisp_lce_write_curve, the first-call buffer hand-over, the control registers */
static inline void t32_lcetcs_write_all_reg(const struct t32_lcetcs_ops *op, u32 ch, struct t32_lcetcs *l)
{
	memcpy(l->curve + T32_LCETCS_HALF, l->st + T32_LCETCS_CURVE0, T32_LCETCS_HALF);	/* write_curve (LE words) */
	if (!*l->frame_flag) {
		memcpy(l->curve, l->curve + T32_LCETCS_HALF, T32_LCETCS_HALF);
		op->cache_sync(l->curve, T32_LCETCS_HALF);
		op->reg_write(ch, 0xc0a4, l->curve_pa);
		*l->frame_flag = 1;
	}
	t32_lcetcs_write_ctrl_reg(op, ch, l->st);
}

/* tisp_lce_curve_init_default */
static inline void t32_lcetcs_curve_init_default(const struct t32_lcetcs_ops *op, u32 ch, struct t32_lcetcs *l)
{
	t32_lce_curve(l->st + T32_LCETCS_CURVE0);
	t32_lcetcs_write_all_reg(op, ch, l);
	t32_lce_curve(l->st + T32_LCETCS_CURVE1);
	memcpy(l->st + T32_LCETCS_ROW0, l->st + T32_LCETCS_CURVE1, 20);
}

/* tisp_lce_top_change_state */
static inline void t32_lcetcs_top_change_state(const struct t32_lcetcs_ops *op, u32 ch, struct t32_lcetcs *l)
{
	if (t32_lcetcs_sec(l->st)[T32_LCETCS_PAR_TOP] != 1) {
		l->st[T32_LCETCS_MODE] = 4;
		return;
	}
	l->st[T32_LCETCS_MODE] = 3;
	t32_lcetcs_curve_init_default(op, ch, l);
}

#endif /* TX_ISP_T32_LCETCS_H */
