/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IVDC: AOV complete from the vendor module (Matt Davis' recovered C,
 * PR #15): ivdc_complete_module (0xd328).
 *
 * After an AOV resume, for each of the d9014 (IVDC mode) VPU channels the
 * two buffer addresses saved by prepare (save buffer d98b0) are written
 * back to the IVDC registers with their companion word, and the control
 * register 8 is set to 36 (first address) / 40 (second); when the saved
 * control word +128 says the channel pair is in step (bit 16 set: field
 * a == 0 and field b == 7; clear: a - b < 2 unsigned, a = bits 0..2,
 * b = bits 4..6) register 120 gets 0x1000000.  Channel 0 uses the save
 * offsets 24 / 28 (companions 192 / 196), channel k >= 1 the offsets
 * 1544 + 64 * (k - 1) and +4 (companions +28 / +32).  Without both
 * addresses the channel prints "not support".
 *
 * Vendor quirks kept: register 120 is written twice; a negative mode skips
 * every channel but still prints the enter message; the return value is
 * isp_printf's (or undefined with mode 0), so ours is void.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_ivdcc_oracle).
 */
#ifndef TX_ISP_T32_IVDC_AOV_H
#define TX_ISP_T32_IVDC_AOV_H

#include "tx_isp_t32_ivdc.h"

#define T32_IVDCC_REG_CTRL	8U
#define T32_IVDCC_REG_SYNC	120U
#define T32_IVDCC_SYNC		0x1000000U
#define T32_IVDCC_CTRL_A	36U
#define T32_IVDCC_CTRL_B	40U
#define T32_IVDCC_SAVE_STEP	128U

static inline void t32_ivdcc_reg_wr(const u8 *priv, u32 reg, u32 v)
{
	*t32_ivdc_reg(priv, reg) = v;
}

static inline void t32_ivdcc_channel(const struct t32_ivdc *d, const u8 *priv, u32 k)
{
	static const u32 line[4] = { 773, 809, 845, 881 };
	u32 base = k ? 1544U + 64U * (k - 1) : 24U, comp = k ? base + 28U : 192U;
	const u8 *s = d->save_buf;
	u32 a = t32_g_rd32(s, base), b = t32_g_rd32(s, base + 4), w, fa, fb;

	if (!a && !b) {
		isp_printf(2, "[%d]IVDC_VPU_CTRL not support! vpu0:%#x, vpu1:%#x\n", line[k], 0, 0);
	} else {
		if (a) {
			t32_ivdcc_reg_wr(priv, base, a);
			t32_ivdcc_reg_wr(priv, comp, t32_g_rd32(s, comp));
			t32_ivdcc_reg_wr(priv, T32_IVDCC_REG_CTRL, T32_IVDCC_CTRL_A);
		}
		if (b) {
			t32_ivdcc_reg_wr(priv, base + 4, b);
			t32_ivdcc_reg_wr(priv, comp + 4, t32_g_rd32(s, comp + 4));
			t32_ivdcc_reg_wr(priv, T32_IVDCC_REG_CTRL, T32_IVDCC_CTRL_B);
		}
	}
	w = t32_g_rd32(s, T32_IVDCC_SAVE_STEP);
	fa = w & 7;
	fb = w >> 4 & 7;
	if (w >> 16 & 1 ? fa + 7 == fb : fa - fb < 2) {
		t32_ivdcc_reg_wr(priv, T32_IVDCC_REG_SYNC, T32_IVDCC_SYNC);
		t32_ivdcc_reg_wr(priv, T32_IVDCC_REG_SYNC, T32_IVDCC_SYNC);
	}
}

static inline void t32_ivdc_complete_module(const struct t32_ivdc *d, u8 *sd)
{
	static const char fn[] = "ivdc_complete_module";
	u8 *priv = t32_ivdc_priv(sd);
	u32 k;

	if (!d->mode)
		return;
	if (!t32_ivdc_valid(priv)) {
		isp_printf(2, "[%s %d] AOV complete IVDC is NULL\n", fn, 748);
		return;
	}
	if (!d->save_buf) {
		isp_printf(2, "[%s %d] AOV resume IVDC mem is NULL\n", fn, 752);
		return;
	}
	for (k = 0; k < 4 && (int)k < (int)d->mode; k++)
		t32_ivdcc_channel(d, priv, k);
	isp_printf(0, "[%s %d] Enter the complete ... \n", fn, 899);
}

#endif
