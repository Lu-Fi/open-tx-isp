/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AE API accessors (anti-flicker, face AE), the ISP core register window
 * accessors, the AWB trigger and the irq callback table.
 * Ports of tisp_ae_api_get_antiflick / _set_antiflick / _get_faceae,
 * system_reg_read / _write, system_reg_set_awb_trig, system_irq_func_set.
 */
#ifndef TX_ISP_T33_R12D_H
#define TX_ISP_T33_R12D_H

#include "tx_isp_t33_blk.h"

static inline u8 *t33_r12d_ptr(const u8 *p, u32 off)
{
	return (u8 *)(uintptr_t)t33_le32(p + off);
}

static inline u32 t33_r12d_le16(const u8 *p)
{
	return p[0] | p[1] << 8;
}

static inline void t33_r12d_wr32(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
	p[off + 2] = (u8)(v >> 16);
	p[off + 3] = (u8)(v >> 24);
}

static inline void t33_r12d_wr16(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
}

/* tisp_ae_api_get_antiflick: out = { u32 mode (0 off, 1, 2), u8 value } */
static inline void t33_ae_get_antiflick(u8 *const *ae_info, u8 *const *tp, u32 ch, u8 *out)
{
	const u8 *tune = t33_r12d_ptr(ae_info[ch], 1744);
	u32 v;

	if (!t33_r12d_le16(tune + 2920)) {
		t33_r12d_wr32(out, 0, 0);
		out[4] = 0;
		return;
	}
	if (!t33_r12d_le16(tune + 2922)) {
		t33_r12d_wr32(out, 0, 2);
		v = t33_le32(tune + 1660);
	} else {
		t33_r12d_wr32(out, 0, 1);
		v = t33_le32(tp[ch] + 1772);
	}
	out[4] = (u8)v;
}

/* tisp_ae_api_get_faceae: out[1..5] = cache + 1500.., out[0] = cache + 1520 */
static inline u32 t33_ae_get_faceae(u8 *const *ae_cache, u32 ch, u8 *out)
{
	const u8 *c = ae_cache[ch];

	t33_r12d_wr32(out, 4, t33_le32(c + 1500));
	t33_r12d_wr32(out, 8, t33_le32(c + 1504));
	t33_r12d_wr32(out, 12, t33_le32(c + 1508));
	t33_r12d_wr32(out, 16, t33_le32(c + 1512));
	t33_r12d_wr32(out, 20, t33_le32(c + 1516));
	t33_r12d_wr32(out, 0, t33_le32(c + 1520));
	return 0;
}

/* tisp_ae_api_set_antiflick: in = { u32 mode, u8 value }; deflicker = tisp_ae_clac_deflicker_cfg */
static inline void t33_ae_set_antiflick(u8 *const *ae_info, u8 *const *ae_cache, u32 ch, const u8 *in,
					void (*deflicker)(u32 ch))
{
	u8 *tune = t33_r12d_ptr(ae_info[ch], 1744);
	u32 mode = t33_le32(in);

	if (!mode) {
		t33_r12d_wr16(tune, 2920, 0);
	} else {
		t33_r12d_wr16(tune, 2920, 1);
		if (mode == 2)
			t33_r12d_wr16(tune, 2922, 0);
		else
			t33_r12d_wr16(tune, 2922, 1);
		t33_r12d_wr32(tune, 1660, in[4]);
	}
	deflicker(ch);
	ae_cache[ch][1285] = 1;
}

/* system_reg_read: sd = ispcore_sd (register base at +240) */
static inline u32 t33_sys_reg_read(const u8 *sd, u32 off)
{
	const u8 *base = t33_r12d_ptr(sd, 240);

	return t33_le32(base + off);
}

/* system_reg_write */
static inline u32 t33_sys_reg_write(const u8 *sd, u32 off, u32 val)
{
	u8 *base = t33_r12d_ptr(sd, 240);

	t33_r12d_wr32(base, off, val ^ 0);
	return 0;
}

/*
 * system_reg_set_awb_trig: mode 1 writes register 29 of module 1 through
 * tisp_reg_write_t33; mode 2 tail-jumps to another module function (hook),
 * else the mode is returned.
 */
static inline u32 t33_sys_reg_set_awb_trig(u32 mode, u32 val, u32 (*wr)(u32, u32, u32, u32),
					   u32 (*mode2)(u32))
{
	if (mode == 1)
		return wr(val, 29, 1, 1);
	if (mode == 2)
		return mode2(val);
	return mode;
}

/* system_irq_func_set: cb = irq_func_cb (128 words) */
static inline void t33_irq_func_set(u32 *cb, u32 idx, u32 fn)
{
	cb[idx] = fn;
}

#endif /* TX_ISP_T33_R12D_H */
