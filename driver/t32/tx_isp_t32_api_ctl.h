/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 module enable and raw read/write setters: readable C of the vendor's
 * tisp_s_module_control, tisp_s_raw_rw_control and tisp_s_raw_row_control.
 *
 *  - tisp_s_module_control replaces the low 22 bits of the top enable word
 *    (bus 0x40, shadow index 2 with t32_data_d03a0 != 0) by the user's,
 *    restarting MDNS (bit 11 changed: tisp_mdns_start before, then
 *    tisp_mdns_reg_trig after the write) and LCE (bit 4 or 10 changed:
 *    tisp_lce_top_change_state).  It returns the result of the last call.
 *  - tisp_s_raw_rw_control / tisp_s_raw_row_control build a raw DMA
 *    descriptor on the stack from the channel state (t32_data_dc284 +
 *    ch * 904) and the user block but never hand it on (T32: no raw path);
 *    what remains are the vendor's error prints for unsupported fields:
 *    raw_rw word +8 not 0/1 (two prints), raw_row words +16 and +32 not 0.
 *    Both return 0.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_apictl_oracle).
 */
#ifndef TX_ISP_T32_API_CTL_H
#define TX_ISP_T32_API_CTL_H

#include "tx_isp_t32_tp.h"

struct t32_api_ctl_ops {
	u32 (*reg_read)(u32 ch, u32 addr);
	int (*reg_write)(u32 ch, u32 addr, u32 val);
	void (*warn)(void);
	int (*mdns_start)(u32 ch);
	int (*lce_top_change_state)(u32 ch);
	int (*mdns_reg_trig)(u32 ch);
};

#define T32_TOP_EN_MASK		0x3fffff	/* user-settable module enables */
#define T32_TOP_EN_MDNS		0x800
#define T32_TOP_EN_LCE		0x410

static inline u32 t32_ac_rd32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* tisp_s_module_control */
static inline int t32_s_module_control(const struct t32_api_ctl_ops *op, u8 shadow, u32 ch, u32 en)
{
	u32 addr = shadow ? 2 : 0x40;
	u32 top = op->reg_read(ch, addr);
	u32 chg = en ^ top;
	int r;

	if (chg & T32_TOP_EN_MDNS)
		op->mdns_start(ch);
	if (chg & T32_TOP_EN_LCE)
		op->lce_top_change_state(ch);
	r = op->reg_write(ch, addr, (en & T32_TOP_EN_MASK) | (top & ~T32_TOP_EN_MASK));
	if (chg & T32_TOP_EN_MDNS)
		return op->mdns_reg_trig(ch);
	return r;
}

/* tisp_s_raw_rw_control */
static inline int t32_s_raw_rw_control(const struct t32_api_ctl_ops *op, u32 ch, const u8 *buf)
{
	u32 mode = t32_ac_rd32(buf + 8);

	(void)ch;
	if (mode > 1) {
		op->warn();
		op->warn();
	}
	return 0;
}

/* tisp_s_raw_row_control */
static inline int t32_s_raw_row_control(const struct t32_api_ctl_ops *op, u32 ch, const u8 *buf)
{
	(void)ch;
	if (t32_ac_rd32(buf + 16))
		op->warn();
	if (t32_ac_rd32(buf + 32))
		op->warn();
	return 0;
}

#endif
