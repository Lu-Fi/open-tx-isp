/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC subdev stream on / off from the vendor module (Matt Davis'
 * recovered C, PR #15): vic_core_s_stream (0x3b6c).
 *
 * Argument: +0 enable, +4 sensor channel.  VIC (layout in
 * tx_isp_t32_vicops.h): per channel state +0x300 + ch * 4 (3 stopped,
 * 4 streaming), running flag +0x334, a busy word +0x338 (no start while it
 * is set), mode +0x448 (the first config word: 1 one channel, 3 / 4 two
 * channels).  tx_isp_vic_index_cal, tx_isp_vic_start and lib_tisp_vic_stop
 * are the hooks of struct t32_vicops_os, tx_vic_enable_irq /
 * tx_vic_disable_irq those of struct t32_vic_irq_os.
 *
 * Vendor flow: the index (index_cal) must be < 2; the state is read with the
 * channel, not the index, unchecked.  On: mode 1 enables the IRQ 0 and starts
 * with 0 (tail call, its result returned); other modes only for the channel
 * mode - 1: IRQs 0 and 1, start 0, then start 1 (tail call).  Off: state 3,
 * then (mode 3: channel > 0; mode 4: channel >= 2; other modes always)
 * disable the IRQ of the index, and for mode 1 or an odd channel (signed
 * % 2 == 1) stop the VIC library and clear the running flag.  Every other
 * path returns 0.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_vicstr_oracle).
 */
#ifndef TX_ISP_T32_VIC_STREAM_H
#define TX_ISP_T32_VIC_STREAM_H

#include "tx_isp_t32_vicops.h"

#define T32_VICD_BUSY		0x338U
#define T32_VICD_ST_STOP	3U
#define T32_VICD_ST_STREAM	4U

struct t32_vic_irq_os {
	void (*enable_irq)(u32 n);	/* tx_vic_enable_irq */
	void (*disable_irq)(u32 n);	/* tx_vic_disable_irq */
};

static inline int t32_vic_core_s_stream(const struct t32_vicops_os *os, const struct t32_vic_irq_os *irq,
					u8 *sd, const u8 *arg)
{
	u8 *vic;
	u32 idx, mode, st, en;
	int ch;

	if (!t32_vicd_valid(sd))
		return -22;
	vic = t32_vicd_priv(sd);
	if (!t32_vicd_valid(vic))
		return -22;
	idx = os->index_cal(vic, t32_g_rd32(arg, 4));
	if ((int)idx >= 2)
		return 0;
	ch = (int)t32_g_rd32(arg, 4);
	st = t32_g_rd32(vic, T32_VICD_STATE((u32)ch));
	en = t32_g_rd32(arg, 0);
	if (en) {
		if (st == T32_VICD_ST_STREAM || t32_g_rd32(vic, T32_VICD_BUSY))
			return 0;
		t32_g_wr32(vic, T32_VICD_STATE((u32)ch), T32_VICD_ST_STREAM);
		mode = t32_g_rd32(vic, T32_VICD_CFG);
		if (mode == 1) {
			irq->enable_irq(0);
			return os->vic_start(vic, 0);
		}
		if (t32_g_rd32(arg, 4) != mode - 1)
			return 0;
		irq->enable_irq(0);
		irq->enable_irq(1);
		os->vic_start(vic, 0);
		return os->vic_start(vic, 1);
	}
	if (st != T32_VICD_ST_STREAM)
		return 0;
	t32_g_wr32(vic, T32_VICD_STATE((u32)ch), T32_VICD_ST_STOP);
	mode = t32_g_rd32(vic, T32_VICD_CFG);
	ch = (int)t32_g_rd32(arg, 4);
	if (mode == 3 && ch <= 0)
		return 0;
	if (mode == 4 && ch < 2)
		return 0;
	irq->disable_irq(idx);
	ch = (int)t32_g_rd32(arg, 4);
	if (t32_g_rd32(vic, T32_VICD_CFG) == 1 || ch % 2 == 1) {
		os->lib_stop();
		t32_g_wr32(vic, T32_VICD_RUNNING, 0);
	}
	return 0;
}

#endif
