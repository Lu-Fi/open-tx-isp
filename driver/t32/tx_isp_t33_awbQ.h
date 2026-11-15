/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AWB parameter refresh and per-frame driver: tisp_awb_params_refresh,
 * tisp_awb_dn_params_refresh, tisp_awb_set_hardware_param and tisp_awb_process, on the raw byte
 * blocks awbInfo[ch] points at (par: the AWB section, st: the run state, hw: the hardware block).
 * The other module functions they call come in through t33_awbQ_ops.
 */
#ifndef TX_ISP_T33_AWBQ_H
#define TX_ISP_T33_AWBQ_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_awbL.h"

struct t33_awbQ_ops {
	void (*reg_write)(u32 ch, u32 mod, u32 slot, u32 val);	/* tisp_reg_write_t33 */
	void (*regional_threshold)(u32 ch);			/* tisp_awb_set_regional_threshold */
	void (*lum_th_freq)(u32 ch);				/* tisp_awb_set_lum_th_freq */
	u32 (*dma_state)(u32 mod);				/* tisp_rcfg_get_dma_state */
	void (*long_par_update)(u32 ch, u8 *tbl);		/* tisp_awb_long_par_update */
	void (*spec_calculate)(u32 ch);				/* tisp_awb_spec_calculate */
	void (*long_alg)(u8 *tbl, u32 ch);			/* tisp_t33_awb_long_alogrithm */
	void (*set_gain)(u32 ch);				/* tisp_awb_set_gain */
	void (*complete)(void *c);				/* private_complete */
	void (*event_push)(u32 ch, u32 *ev);			/* tisp_event_push */
};

/* tisp_awb_params_refresh: zone counts raised until a zone is at most 126 wide / high */
static inline void t33_awbQ_params_refresh(u8 *par, u8 *st)
{
	const u32 xs = t33_aL_r16(par, 3188), ys = t33_aL_r16(par, 3184);
	u32 nx = t33_aL_r16(par, 3190), ny = t33_aL_r16(par, 3186), zx, zy, k;
	const int xr = ((int)t33_aL_r16(st, 28690) - (int)xs + 1) / 2;
	const int yr = ((int)t33_aL_r16(st, 28692) - (int)ys + 1) / 2;

	for (;;) {
		zx = (u32)(xr / (int)nx) & 0xffff;
		if (zx < 127)
			break;
		nx = (nx + 1) & 0xffff;
	}
	for (;;) {
		zy = (u32)(yr / (int)ny) & 0xffff;
		if (zy < 127)
			break;
		ny = (ny + 1) & 0xffff;
	}
	t33_aL_w16(par, 3190, nx);
	t33_aL_w16(par, 3186, ny);
	if (!st[38057] && !st[66616]) {
		t33_aL_w32(par, 8, 1);
		t33_aL_w32(par, 12, 0);
	}
	st[38057] = 0;
	for (k = 0; k < 15; k++) {
		t33_aL_w16(par, 3192 + 2 * k, zx);
		t33_aL_w16(par, 3222 + 2 * k, zy);
	}
	t33_aL_w16(par, 3290, zx);
	t33_aL_w16(par, 3292, zy);
}

/* tisp_awb_set_hardware_param: list 29 words from the hardware block, then the threshold sets */
static inline void t33_awbQ_set_hw(const struct t33_awbQ_ops *o, u32 ch, const u8 *hw, u8 *refresh)
{
	o->reg_write(ch, 29, 3, (t33_aL_r16(hw, 24) & 0xfff) << 16 | (t33_aL_r16(hw, 12) & 0xfff));
	o->reg_write(ch, 29, 4, (u32)(hw[140] & 0x3f) << 8 | (hw[117] & 0x3f));
	o->reg_write(ch, 29, 15, 0x1010100u | (hw[126] & 3));
	o->reg_write(ch, 29, 42, (u32)(hw[128] & 0x7f) << 16 | (hw[104] & 0x7f));
	o->regional_threshold(ch);
	o->lum_th_freq(ch);
	*refresh = o->dma_state(29) ? 2 : 1;
}

/* tisp_awb_dn_params_refresh: par = tparamsP[ch] + 3400 (already stored in awbInfo[ch][0] by the caller) */
static inline void t33_awbQ_dn_refresh(const struct t33_awbQ_ops *o, u32 ch, u8 *st, u8 *par, const u8 *hw,
				       u8 *refresh)
{
	st[38056] = 0;
	st[38057] = 1;
	t33_awbQ_params_refresh(par, st);
	if (!st[66617])
		t33_awbQ_set_hw(o, ch, hw, refresh);
}

/* tisp_awb_process: the frame sequence, event 11 with the colour temperature, the 0..100 counter */
static inline void t33_awbQ_process(const struct t33_awbQ_ops *o, u32 ch, u8 *ct_tbl, u32 custom_en,
				    void *algo_comp, const u8 *par, u16 *count)
{
	u32 ev[6], c;

	o->long_par_update(ch, ct_tbl);
	o->spec_calculate(ch);
	o->long_alg(ct_tbl, ch);
	if (custom_en == 1)
		o->complete(algo_comp);
	o->set_gain(ch);
	ev[0] = 0;
	ev[1] = 0;
	ev[2] = 11;
	ev[3] = 0;
	ev[4] = t33_le32(par + 44);
	ev[5] = 0;
	o->event_push(ch, ev);
	c = (*count + 1) & 0xffff;
	*count = c < 101 ? c : 0;
}

#endif /* TX_ISP_T33_AWBQ_H */
