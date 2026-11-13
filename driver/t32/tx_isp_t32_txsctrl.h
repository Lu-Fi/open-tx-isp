/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core set-control (vendor tx_isp_core_ops_s_ctrl): control ids
 * 0x8000000 + n, the value or user pointer at +4 of the control.  Each id
 * hands the user pointer to an attribute helper (.isra; gamma, module
 * ratio, AF weight, statis config and switch bin are ours, the AE / AWB
 * ones are hooks) or copies a block in and calls a tisp setter; BCSH bytes
 * are kept in the ISP device (+16 + 32 ch: contrast, saturation,
 * brightness, sharpness, hue).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txsctrl_oracle).
 */
#ifndef TX_ISP_T32_TXSCTRL_H
#define TX_ISP_T32_TXSCTRL_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txattr.h"
#include "tx_isp_t32_txbin.h"
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

#define T32_SC_BASE		0x8000000U
#define T32_SC_COPY_ERR		"[ %s:%d ] copy error!!!\n"
#define T32_SC_SENSOR_FPS	0x200000aU

typedef int (*t32_tx_sc_set_fn)(u32 ch, void *buf);
typedef int (*t32_tx_sc_val_fn)(u32 ch, u32 v);
typedef int (*t32_tx_sc_ev_fn)(u32 sd, u32 ev, void *msg);

/* provided by the driver glue (kernel) or by the oracle (host) */
extern u32 t32_tx_link_mode;		/* d9014 */
int t32_tx_os_awb_attr_s_attr(u32 ch, u32 user);
int t32_tx_os_awb_weight_s_attr(u32 ch, u32 user);
int t32_tx_os_ae_weight_s_attr(u32 ch, u32 user);
int t32_tx_os_ae_expoinfo_s_attr(u32 ch, u32 user);
int t32_tx_os_ae_explist_s_attr(u32 ch, u32 user);
int t32_tx_os_ae_target_s_attr(u32 ch, u32 user);
int t32_tx_os_tisp_awb_face_set(u32 ch, void *buf);
int t32_tx_os_tisp_s_awb_ct_trend_offset(u32 ch, void *buf);
int t32_tx_os_tisp_s_ae_scence_attr(u32 ch, void *buf);
int t32_tx_os_tisp_g_ae_scence_attr(u32 ch, void *buf);
int t32_tx_os_tisp_s_antiflick(u32 ch, void *buf);
int t32_tx_os_tisp_s_face_ae_weiget(u32 ch, void *buf);
int t32_tx_os_tisp_ae_face_set(u32 ch, void *buf);
int t32_tx_os_tisp_set_wdr_output_mode(u32 ch, void *buf);
int t32_tx_os_tisp_s_tmo_faceae(u32 ch, void *buf);
int t32_tx_os_tisp_set_fps(u32 ch, u32 fps);
int t32_tx_os_tisp_s_module_control(u32 ch, u32 v);
int t32_tx_os_tisp_g_hv_flip(u32 ch, void *flags);
int t32_tx_os_tisp_s_hv_flip(u32 ch, void *flags);
int t32_tx_os_tisp_lsc_hvflip(u32 ch, u32 a, u32 b, u32 h, u32 v);
int t32_tx_os_tisp_s_mscaler_mask_block_attr(u32 ch, void *buf);
int t32_tx_os_tisp_s_autozoom_control(u32 ch, void *buf);
int t32_tx_os_tisp_s_ccm_attr(u32 ch, void *buf);
int t32_tx_os_tisp_set_hue(u32 ch, u32 v);
int t32_tx_os_tisp_set_brightness(u32 ch, u32 v);
int t32_tx_os_tisp_set_sharpness(u32 ch, u32 v);
int t32_tx_os_tisp_set_saturation(u32 ch, u32 v);
int t32_tx_os_tisp_set_contrast(u32 ch, u32 v);
int t32_tx_os_tisp_set_csc_attr(u32 ch, void *buf);
int t32_tx_os_tisp_s_csccr_mode(u32 ch, void *buf);
int t32_tx_os_tisp_s_coefft_wb(u32 ch, void *buf);
int t32_tx_os_tisp_s_draw_block_attr(u32 ch, void *buf);
int t32_tx_os_tisp_s_osd_attr(u32 ch, void *buf);
int t32_tx_os_tisp_s_osd_block_attr(u32 ch, void *buf);
int t32_tx_os_tisp_set_scaler_level_control(u32 ch, void *buf);
void t32_tx_os_lib_tisp_api_set_line_space(u32 v);
int t32_tx_os_wait_for_completion_timeout(u32 c, u32 t);

/* copy in; a failed copy prints and returns its result (stop) or only prints (vendor: then goes on) */
static inline int t32_tx_sc_in(void *buf, u32 ctrl, u32 n, const char *fn, int line)
{
	int r = (int)t32_tx_os_copy_from_user(buf, t32_tx_ld(ctrl, 4), n);

	if (r)
		t32_tx_os_isp_printf(2, T32_SC_COPY_ERR, fn, line);
	return r;
}

/* copy in (stop on error), setter, a failed setter prints fail at level 1 and is returned */
static inline int t32_tx_sc_set(u32 ch, u32 ctrl, t32_tx_sc_set_fn set, u32 n, const char *fn, int line,
				const char *fail, int fline)
{
	u32 buf[24];
	int r = t32_tx_sc_in(buf, ctrl, n, fn, line);

	if (r)
		return r;
	r = set(ch, buf);
	if (r && fail)
		t32_tx_os_isp_printf(1, fail, fn, fline);
	return fail ? r : 0;
}

/* copy in (printed, not stopping), setter, the setter result decides */
static inline int t32_tx_sc_set_go_on(u32 ch, u32 ctrl, t32_tx_sc_set_fn set, u32 n, const char *fn, int line,
				      const char *fail, int fline)
{
	u32 buf[24];
	int r;

	t32_tx_sc_in(buf, ctrl, n, fn, line);
	r = set(ch, buf);
	if (r)
		t32_tx_os_isp_printf(1, fail, fn, fline);
	return r;
}

/* copy in (printed, not stopping), setter; vendor: the copy result is tested and returned */
static inline int t32_tx_sc_set_copyres(u32 ch, u32 ctrl, t32_tx_sc_set_fn set, u32 n, const char *fn, int line,
					const char *fail, int fline)
{
	u32 buf[24];
	int r = t32_tx_sc_in(buf, ctrl, n, fn, line);

	set(ch, buf);
	if (r)
		t32_tx_os_isp_printf(1, fail, fn, fline);
	return r;
}

/*
 * 0x2a SetFaceAeWeiget: as t32_tx_sc_set_copyres (the vendor returns the copy
 * result and ignores the setter's), but a window length the setter refuses
 * (-EINVAL, openimp #350, beyond vendor, user-approved 2026-11-11) reaches the
 * caller. In-range values: the setter returns 0, so the vendor's return code.
 */
static inline int t32_tx_sc_set_face_ae(u32 ch, u32 ctrl, t32_tx_sc_set_fn set, u32 n, const char *fn, int line,
					const char *fail, int fline)
{
	u32 buf[24];
	int r = t32_tx_sc_in(buf, ctrl, n, fn, line);
	int s = set(ch, buf);

	if (r)
		t32_tx_os_isp_printf(1, fail, fn, fline);
	return r ? r : (s == -EINVAL ? s : 0);
}

/* one BCSH byte: copy, setter, the byte into the ISP device */
static inline int t32_tx_sc_bcsh(u32 ch, u32 isp, u32 ctrl, t32_tx_sc_val_fn set, u32 off, const char *fn, int line)
{
	u8 v[96] = { 0 };
	int r = t32_tx_sc_in(v, ctrl, 1, fn, line);

	if (r)
		return r;
	set(ch, v[0]);
	t32_tx_wb(isp + 32 * ch + off, v[0]);
	return 0;
}

/* 0x70: sensor fps on every input whose fps differs (core +392 + 104 i), notify +152 of the core subdevice */
static inline int t32_tx_sc_fps(u32 isp, u32 ctrl)
{
	u32 sd = t32_tx_ld(isp, 0), core = t32_tx_ld(sd, 280), fps = t32_tx_ld(ctrl, 4), i, fn, msg[24];
	int r;

	if (!t32_tx_ld(core, 1196))
		return 0;
	for (i = 0; i < t32_tx_ld(core, 1196); i++) {
		if (t32_tx_ld(core, 392 + 104 * i) == fps)
			continue;
		msg[1] = fps;
		if (!sd) {
			r = -2;
		} else if (!(fn = t32_tx_ld(sd, 152))) {
			r = -515;
		} else {
			r = ((t32_tx_sc_ev_fn)T32_TX_P(fn))(sd, T32_SC_SENSOR_FPS, msg);
			if (!r) {
				t32_tx_os_tisp_set_fps(i, fps);
				t32_tx_st(isp, 24 + 32 * i, fps);
				continue;
			}
		}
		t32_tx_os_isp_printf(1, "Failed to set sensor fps=%u, ret=%d\n", fps, r);
		return r;
	}
	return 0;
}

/*
 * 0x71: day / night of input ch (+32 + 32 ch), switch pending at core
 * +1164 + 4 ch, wait for the switch.  Vendor: the vinum of the print is a
 * stale stack word.
 */
static inline int t32_tx_sc_day_night(u32 ch, u32 isp, u32 ctrl)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, 0), 280), v[24], old;
	int r = (int)t32_tx_os_copy_from_user(v, t32_tx_ld(ctrl, 4), 4);

	if (r) {
		t32_tx_os_isp_printf(2, T32_SC_COPY_ERR, "tx_isp_day_or_night_s_ctrl", 385);
		return r;
	}
	old = t32_tx_ld(isp, 32 + 32 * ch);
	t32_tx_os_isp_printf(0, "%s:%d:@@@@ vinum is %d, dn is %d, ctrl is %d\n", "tx_isp_day_or_night_s_ctrl", 387,
			     ch, v[0], old);
	if (old == v[0])
		return 0;
	t32_tx_st(isp, 32 + 32 * ch, v[0]);
	t32_tx_st(core, 4 * (ch + 290) + 4, 1);
	if (t32_tx_os_wait_for_completion_timeout(isp + 152, 100))
		return 0;
	t32_tx_os_isp_printf(1, "Timeout waiting for completion\n");
	return -145;
}

/* 0x73: ISP mirror / flip of the three channels of input ch, then the LSC flip */
static inline int t32_tx_sc_hv_flip(u32 ch, u32 isp, u32 ctrl)
{
	u32 core = t32_tx_ld(t32_tx_ld(isp, 0), 280), a = t32_tx_ld(core, 384 + 104 * ch), b = t32_tx_ld(core, 388 + 104 * ch);
	u32 v[24], f = 0, k, h, vv, in;
	int r = (int)t32_tx_os_copy_from_user(v, t32_tx_ld(ctrl, 4), 16);

	if (r) {
		t32_tx_os_isp_printf(2, "copy error!!!\n");
		return r;
	}
	if ((v[1] | v[2] | v[3]) & v[0]) {
		t32_tx_os_isp_printf(1, "ISP and Sensor are prohibited from using mirr or flip together!!!\n");
		return -1;
	}
	if (t32_tx_link_mode && (v[1] & 2)) {
		v[1] &= 0xfd;
		t32_tx_os_isp_printf(1, "The direct mode disables the vertical flip of ISP!!!\n");
	}
	for (k = 0; k < 3; k++) {
		u8 *fb = (u8 *)&f;

		*fb = (u8)((*fb & ~3U) | k);
		t32_tx_os_tisp_g_hv_flip(ch, &f);
		*fb = (u8)((*fb & ~0x3cU) | (v[1 + k] & 1) << 2 | (v[1 + k] & 1) << 3 | ((v[1 + k] >> 1) & 1) << 4 |
			   ((v[1 + k] >> 1) & 1) << 5);
		t32_tx_os_tisp_s_hv_flip(ch, &f);
	}
	in = t32_tx_ld(isp, 28 + 32 * ch);
	if (in == v[0])
		return 0;
	h = v[0] == 2 || v[0] == 3;
	vv = v[0] == 1 || v[0] == 3;
	t32_tx_os_tisp_lsc_hvflip(ch, a, b, h, vv);
	t32_tx_st(core, 1344 + 96 * ch, 1);
	t32_tx_st(core, 1348 + 96 * ch, v[0]);
	t32_tx_st(isp, 28 + 32 * ch, v[0]);
	return 0;
}

/* 0x2e: HLC / BLC mode { mode, strength } into the AE scene attribute */
static inline int t32_tx_sc_hlc_blc(u32 ch, u32 ctrl)
{
	u32 m[24], s[24];
	u8 *s8 = (u8 *)s, *m8 = (u8 *)m;
	int r;

	t32_tx_sc_in(m, ctrl, 8, "tx_isp_ae_hlc_or_blc_s_attr", 1370);
	r = t32_tx_os_tisp_g_ae_scence_attr(ch, s);
	if (r) {
		t32_tx_os_isp_printf(1, "[ %s:%d ] get ae scence failed!!!\n", "tx_isp_ae_hlc_or_blc_s_attr", 1375);
		return r;
	}
	if (m[0] == 0) {
		s[0] = 1;
		s[2] = 1;
		s8[4] = 0;
		s8[12] = 0;
	} else if (m[0] == 1) {
		s[2] = 1;
		s[0] = 3;
		s8[4] = m8[4];
		s8[12] = 0;
	} else if (m[0] == 2) {
		s[2] = 3;
		s[0] = 1;
		s8[4] = 0;
		s8[12] = m8[4];
	} else {
		t32_tx_os_isp_printf(1, "[ %s:%d ] HBLC_Mode=%d\n", "tx_isp_ae_hlc_or_blc_s_attr", 1395, m[0]);
	}
	r = t32_tx_os_tisp_s_ae_scence_attr(ch, s);
	if (r)
		t32_tx_os_isp_printf(1, "[ %s:%d ] set hlc or blc failed!!!\n", "tx_isp_ae_hlc_or_blc_s_attr", 1400);
	return r;
}

/* tx_isp_core_ops_s_ctrl(ch, isp, ctrl) */
static inline int t32_tx_core_ops_s_ctrl(u32 ch, u32 isp, u32 ctrl)
{
	u32 core, b[24], user = t32_tx_ld(ctrl, 4);
	int r;

	switch (t32_tx_ld(ctrl, 0) - T32_SC_BASE) {
	case 0x00: return t32_tx_statis_config_s_attr(ch, user);
	case 0x10: return t32_tx_os_awb_attr_s_attr(ch, user);
	case 0x12: return t32_tx_os_awb_weight_s_attr(ch, user);
	case 0x14:
		return t32_tx_sc_set(ch, ctrl, t32_tx_os_tisp_awb_face_set, 24, "tx_isp_faceawb_s_attr", 1584,
				     "[ %s:%d ] set awb face failed!!!\n", 1588);
	case 0x17:
		return t32_tx_sc_set_copyres(ch & 0xff, ctrl, t32_tx_os_tisp_s_awb_ct_trend_offset, 28,
					     "tx_isp_awb_s_ct_trend_offset", 1617, "[ %s:%d ] set face ae weight failed!!!\n", 1622);
	case 0x21: return t32_tx_os_ae_weight_s_attr(ch, user);
	case 0x23: return t32_tx_os_ae_expoinfo_s_attr(ch, user);
	case 0x24:
		return t32_tx_sc_set(ch, ctrl, t32_tx_os_tisp_s_ae_scence_attr, 40, "tx_isp_ae_scence_s_attr", 956, NULL, 0);
	case 0x25: return t32_tx_gamma_s_attr(ch, user);
	case 0x26:
		return t32_tx_sc_set(ch, ctrl, t32_tx_os_tisp_s_antiflick, 8, "tx_isp_flicker_s_attr", 590,
				     "[ %s:%d ] set control failed!!!\n", 594);
	case 0x27: return t32_tx_os_ae_explist_s_attr(ch, user);
	case 0x2a:
		return t32_tx_sc_set_face_ae(ch, ctrl, t32_tx_os_tisp_s_face_ae_weiget, 12, "tx_isp_face_ae_weiget_s_attr",
					     1510, "[ %s:%d ] set face ae weight failed!!!\n", 1515);
	case 0x2d: return t32_tx_os_ae_target_s_attr(ch, user);
	case 0x2e: return t32_tx_sc_hlc_blc(ch, ctrl);
	case 0x32: return t32_tx_af_weight_s_attr(ch, user);
	case 0x35:
		return t32_tx_sc_set_copyres(ch, ctrl, t32_tx_os_tisp_ae_face_set, 24, "tx_isp_face_ae_s_attr", 1473,
					     "[ %s:%d ] set face ae failed!!!\n", 1478);
	case 0x54:	/* WDR output mode; vendor: the copy is not tested */
		core = t32_tx_ld(t32_tx_ld(isp, 0), 280);
		if (t32_tx_ld(core, 4 * (ch + 294) + 4) != 1) {
			t32_tx_os_isp_printf(1, "[ %s:%d ] Non-wdr mode, set wdr output failed!!!\n", "tx_isp_wdr_s_mode", 1675);
			return 0;
		}
		t32_tx_os_copy_from_user(b, user, 4);
		r = t32_tx_os_tisp_set_wdr_output_mode(ch, b);
		if (r)
			t32_tx_os_isp_printf(1, "[ %s:%d ] set wdr output failed!!!\n", "tx_isp_wdr_s_mode", 1684);
		return r;
	case 0x62:
		return t32_tx_sc_set(ch, ctrl, t32_tx_os_tisp_s_tmo_faceae, 56, "tx_isp_tmo_s_faceae", 1600,
				     "[ %s:%d ] set rgb offset failed!!!\n", 1604);
	case 0x70: return t32_tx_sc_fps(isp, ctrl);
	case 0x71: return t32_tx_sc_day_night(ch, isp, ctrl);
	case 0x72:
		r = t32_tx_sc_in(b, ctrl, 4, "tx_isp_module_s_attr", 341);
		if (r)
			return r;
		t32_tx_os_tisp_s_module_control(ch, b[0]);
		return 0;
	case 0x73: return t32_tx_sc_hv_flip(ch, isp, ctrl);
	case 0x74:	/* vendor: a failed copy is printed and 0 returned */
		if (t32_tx_os_copy_from_user(b, user, 24)) {
			t32_tx_os_isp_printf(2, T32_SC_COPY_ERR, "tx_isp_mask_block_s_attr", 663);
			return 0;
		}
		t32_tx_os_tisp_s_mscaler_mask_block_attr(ch, b);
		return 0;
	case 0x77:
		core = t32_tx_ld(t32_tx_ld(isp, 0), 280);
		r = t32_tx_sc_in(b, ctrl, 60, "tx_isp_autozoom_s_attr", 628);
		if (r)
			return r;
		memcpy(T32_TX_P(core + 60 * ch + 4044), b, 60);
		t32_tx_os_tisp_s_autozoom_control(ch, b);
		return 0;
	case 0x80:	/* vendor: a failed setter is printed and 0 returned */
		r = t32_tx_sc_in(b, ctrl, 40, "tiziano_isp_ccm_attr_s_ctrl", 469);
		if (r)
			return r;
		if (t32_tx_os_tisp_s_ccm_attr(ch, b))
			t32_tx_os_isp_printf(1, "[ %s:%d ] Both BCSH and CCM are enabled or disabled simultaneously !!!\n",
					     "tiziano_isp_ccm_attr_s_ctrl", 474);
		return 0;
	case 0x81: return t32_tx_sc_bcsh(ch, isp, ctrl, t32_tx_os_tisp_set_hue, 20, "tx_isp_bcsh_hue_s_ctrl", 325);
	case 0x92: return t32_tx_sc_bcsh(ch, isp, ctrl, t32_tx_os_tisp_set_brightness, 18, "tx_isp_bright_s_control", 168);
	case 0x93: return t32_tx_sc_bcsh(ch, isp, ctrl, t32_tx_os_tisp_set_sharpness, 19, "tx_isp_sharp_s_control", 136);
	case 0x94: return t32_tx_sc_bcsh(ch, isp, ctrl, t32_tx_os_tisp_set_saturation, 17, "tx_isp_sat_s_control", 152);
	case 0x95: return t32_tx_sc_bcsh(ch, isp, ctrl, t32_tx_os_tisp_set_contrast, 16, "tx_isp_contrast_s_control", 184);
	case 0x96:
		r = t32_tx_sc_in(b, ctrl, 92, "tx_isp_csc_s_attr", 428);
		return r ? r : t32_tx_os_tisp_set_csc_attr(ch, b);
	case 0x97:
		return t32_tx_sc_set_go_on(ch, ctrl, t32_tx_os_tisp_s_csccr_mode, 12, "tx_isp_s_csccr_attr", 1736,
					   "[ %s:%d ] set csccr failed!!!\n", 1741);
	case 0x9b:
		return t32_tx_sc_set(ch, ctrl, t32_tx_os_tisp_s_coefft_wb, 6, "tx_isp_bcsh_rgb_offset_s_attr", 573,
				     "[ %s:%d ] set bcsh rgb offset failed!!!\n", 577);
	case 0xa0: return t32_tx_sc_set(ch, ctrl, t32_tx_os_tisp_s_draw_block_attr, 36, "tx_isp_draw_block_s_attr", 761, NULL, 0);
	case 0xa1: return t32_tx_sc_set(ch, ctrl, t32_tx_os_tisp_s_osd_attr, 32, "tx_isp_osd_s_attr", 692, NULL, 0);
	case 0xa2: return t32_tx_sc_set(ch, ctrl, t32_tx_os_tisp_s_osd_block_attr, 20, "tx_isp_osd_block_s_attr", 727, NULL, 0);
	case 0xa4: return t32_tx_module_ratio_s_attr(ch, user);
	case 0xa5: return t32_tx_switch_bin(ch, isp, user);
	case 0xa6:
		return t32_tx_sc_set_go_on(ch, ctrl, t32_tx_os_tisp_set_scaler_level_control, 12, "tx_isp_s_scaler_level", 1717,
					   "[ %s:%d ] set scaler level failed!!!\n", 1722);
	case 0xa7: {	/* vendor: returns the copy result */
		u16 ls[48] = { 0 };

		r = t32_tx_sc_in(ls, ctrl, 2, "tx_isp_double_s_hb", 1446);
		t32_tx_os_lib_tisp_api_set_line_space(ls[0]);
		return r;
	}
	default:
		return -1;
	}
}

#endif
