/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) small API entry points of the vendor module (openimp #99, oracle block r11E):
 * CCM attribute, scaler level control, autozoom, frame drop, module control, CSC-CR mode and the IPC memory
 * size / configuration.  Block functions and register writes are the proven vendor callees (extern, vendor names).
 */
#ifndef TX_ISP_T33_R11E_H
#define TX_ISP_T33_R11E_H

#include "tx_isp_t33_blk.h"

extern u32 tisp_top_get_bypass(u32 ch);
extern int tisp_top_set_bypass(u32 ch, u32 val);
extern int tisp_ccm_api_set(u32 ch, const u8 *buf);
extern int tisp_msca_t33_api_set_autozoom(u32 ch, const u8 *buf);
extern int tisp_msca_api_get_frame_drop(u32 ch, u32 idx, u8 *out);
extern int tisp_msca_api_set_frame_drop(u32 ch, u32 idx, const u8 *cfg);
extern int tisp_mdns_start(u32 ch);
extern int tisp_lce_top_change_state(u32 ch);
extern int tisp_mdns_t33_reg_trig(u32 ch);
extern int tisp_csccr_api_get(u32 ch, u8 *buf);
extern int tisp_msca_t33_ch_curve_write(u32 ch, u32 set);
extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
extern int system_reg_write(u32 addr, u32 val);
extern int isp_printf(int level, const char *fmt, ...);

/* tisp_s_ccm_attr: top bypass bit 14 = CCM block, bit 7 = BCSH CCM */
static inline int t33_s_ccm_attr(u32 ch, const u8 *buf)
{
	u32 v = tisp_top_get_bypass(ch & 0xff);

	if (v & 0x4000) {
		tisp_ccm_api_set(ch, buf);
		return (v & 0x80) ? -1 : 0;
	}
	if (!(v & 0x80)) {
		tisp_ccm_api_set(ch, buf);
		return -1;
	}
	return 0;
}

static inline void t33_r11e_w16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/*
 * tisp_set_scaler_level_control_set (tisp_set_scaler_level_control tail-calls it): buf = { u8 set (0..2), ..,
 * u32 mode at +4, u8 level at +8 }; msca_info[ch] points at the MSCA info whose word 0 is the block pointer.
 */
static inline int t33_set_scaler_level_control_set(u8 *const *msca_info, u8 cfg_mode, u32 ch, const u8 *buf)
{
	u32 s = buf[0], mode, a, d, q0, q1, q2, flags, lv[3], x, y, z;
	u8 *b, *t0, *a3;

	if (s > 2) {
		isp_printf(2, "scaler level: bad set\n");
		return -1;
	}
	b = *(u8 **)msca_info[ch];
	if (b[202 + s] != 1) {
		isp_printf(2, "scaler level: set %d not enabled\n", s);
		return -1;
	}
	t0 = b + (s == 1 ? 124 : s == 2 ? 154 : 100);
	a3 = b + (s == 1 ? 136 : s == 2 ? 166 : 112);
	mode = t33_le32(buf + 4);
	if (mode == 0) {
		b[211 + 2 * s] = 0;
		b[212 + 2 * s] = 0;
	} else if (mode == 1) {
		a = 3U * buf[8];
		if (a < 129) {
			lv[0] = 128 - a;
			lv[1] = a + 128;
			lv[2] = a + 128;
		} else if (a - 129 < 256) {
			lv[0] = 0;
			lv[1] = a + 128;
			lv[2] = 384 - a;
		} else {
			lv[0] = 0;
			lv[1] = 512;
			lv[2] = 512;
		}
		b[211 + 2 * s] = 1;
		b[212 + 2 * s] = 1;
		x = (lv[0] << 2) & 0xffff;
		y = (lv[1] << 2) & 0xffff;
		z = (lv[2] << 2) & 0xffff;
		t33_r11e_w16(t0, x);
		t33_r11e_w16(t0 + 2, y);
		t33_r11e_w16(t0 + 4, z);
		t33_r11e_w16(t0 + 6, x);
		t33_r11e_w16(t0 + 8, 1024);
		t33_r11e_w16(t0 + 10, 1024);
		if (s == 0) {
			t33_r11e_w16(a3, x);
			t33_r11e_w16(a3 + 2, y);
			t33_r11e_w16(a3 + 4, z);
			t33_r11e_w16(a3 + 6, x);
			t33_r11e_w16(a3 + 8, 1024);
			t33_r11e_w16(a3 + 10, 1024);
		} else {
			d = 3 * lv[0] + 2 * lv[1] + lv[2];
			q0 = (lv[0] << 11) / d;
			q1 = (lv[1] << 11) / d;
			q2 = (lv[2] << 11) / d;
			t33_r11e_w16(a3, q0);
			t33_r11e_w16(a3 + 2, q1);
			t33_r11e_w16(a3 + 4, q2);
			t33_r11e_w16(a3 + 6, q0);
			t33_r11e_w16(a3 + 8, q0);
			t33_r11e_w16(a3 + 10, 2048 - 3 * (q0 & 0xffff) - q1 - q2);
			t33_r11e_w16(a3 + 12, 682);
			t33_r11e_w16(a3 + 14, 682);
			t33_r11e_w16(a3 + 16, 684);
		}
	} else {
		isp_printf(2, "scaler level: bad mode\n");
		return -1;
	}
	flags = (b[211] & 1) | (b[213] & 1) << 1 | (b[215] & 1) << 2 |
		(b[212] & 1) << 4 | (b[214] & 1) << 5 | (b[216] & 1) << 6;
	tisp_reg_write_t33(ch, 23, cfg_mode ? 6 : 0xf004c, flags);
	tisp_msca_t33_ch_curve_write(ch, s);
	tisp_reg_write_t33(ch, 23, cfg_mode ? 1 : 0xf0010, 1);
	return 0;
}

/* tisp_s_autozoom_control */
static inline int t33_s_autozoom_control(u32 ch, const u8 *buf)
{
	tisp_msca_t33_api_set_autozoom(ch & 0xff, buf);
	return 0;
}

/* tisp_get_frame_drop */
static inline int t33_get_frame_drop(u32 ch, u32 idx, u8 *out)
{
	tisp_msca_api_get_frame_drop(ch, idx, out);
	return 0;
}

/* tisp_set_frame_drop */
static inline int t33_set_frame_drop(u32 ch, u32 idx, const u8 *cfg)
{
	tisp_msca_api_set_frame_drop(ch, idx, cfg);
	return 0;
}

/* tisp_g_module_control */
static inline u32 t33_g_module_control(u32 ch, u32 *out)
{
	u32 v = tisp_top_get_bypass(ch & 0xff) & 0x3fffff;

	*out = v;
	return v;
}

/* tisp_s_module_control: returns the reg_trig result when MDNS (bit 12) changed, else the set_bypass result */
static inline int t33_s_module_control(u32 ch, u32 en)
{
	u32 s1 = ch & 0xff, v = tisp_top_get_bypass(s1), chg = en ^ v;
	u32 nv = (en & 0x3fffff) | (v & 0xffc00000);
	int r;

	if (chg & 0x1000)
		tisp_mdns_start(ch);
	if (chg & 0x820)
		tisp_lce_top_change_state(ch);
	r = tisp_top_set_bypass(s1, nv);
	if (chg & 0x1000)
		return tisp_mdns_t33_reg_trig(s1);
	return r;
}

/* tisp_g_csccr_mode */
static inline int t33_g_csccr_mode(u32 ch, u8 *buf)
{
	return tisp_csccr_api_get(ch & 0xff, buf);
}

/* tisp_top_ipc_get_mem_size: stMemInfo word 13 (+52) = (3 * w * h + 1) / 2 rounded down to 8 KiB */
static inline u32 t33_ipc_get_mem_size(u32 w, u32 h, u32 *mi)
{
	u32 a = (w & 0xffff) * (h & 0xffff), size;
	int x = (int)(3 * a + 1);

	size = ((u32)(x / 2) >> 13) << 13;
	mi[13] = size;
	return size;
}

/* tisp_top_ipc_set_mem_cfg: stMemInfo word 9 (+36) = base; returns the last register write's result */
static inline int t33_ipc_set_mem_cfg(u32 base, u32 *mi)
{
	u32 size;

	mi[9] = base;
	system_reg_write(4244, base);
	size = mi[13];
	isp_printf(0, "t33_ipc_set_mem_cfg %d %x %x\n", 227, size, base);
	system_reg_write(4248, 0x200000 | (size >> 13));
	system_reg_write(4292, (mi[13] << 1) / 18);
	return system_reg_write(4296, (mi[13] << 1) / 18);
}

#endif /* TX_ISP_T33_R11E_H */
