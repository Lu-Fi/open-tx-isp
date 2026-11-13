// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: core set-control (driver/t32/tx_isp_t32_txsctrl.h)
 * against the vendor tx_isp_core_ops_s_ctrl (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc, symbols renamed op3_* by op3_ren.sh; the gamma /
 * module ratio / AF weight / statis config setters and switch bin run as
 * vendor functions against our ports, the AE / AWB helpers are hooks).
 *
 * Per case one control id (every handled id and random others), input
 * 0..3, a random ISP / core world, user blocks, scripted copy / setter /
 * getter / helper / notify / completion results.  The day / night print
 * takes a stale stack word as vinum (not compared).  Compared: the event
 * log (setter blocks as hashes of the bytes the vendor copied in), every
 * world byte, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txsctrl.h"

#define NCASE	40000
#define ARENA	0x9000u
#define O_ISP	0x0000u		/* +16..+156 per input, +152 completion */
#define O_SD	0x0200u		/* +152 notify, +280 core */
#define O_CORE	0x0400u		/* up to +4044 + 60 * 4 */
#define O_CTRL	0x1800u
#define O_USER	0x2000u
#define O_VM	0x3000u

u32 op3_t32_data_d9014, t32_tx_link_mode, t32_tx_g_d9008, t32_tx_stream_count[4];
u8 t32_tx_first_bin;
#define S(n, v) char op3_t32_data_##n[] = v;
S(bdf78, "tx_isp_bcsh_rgb_offset_s_attr") S(bdf98, "tx_isp_s_csccr_attr") S(bdfac, "tx_isp_s_scaler_level")
S(bdfc4, "tx_isp_awb_s_ct_trend_offset") S(bdfe4, "tx_isp_tmo_s_faceae") S(bdff8, "tx_isp_faceawb_s_attr")
S(be010, "tx_isp_face_ae_weiget_s_attr") S(be030, "tx_isp_face_ae_s_attr") S(be048, "tx_isp_double_s_hb")
S(be05c, "tx_isp_ae_hlc_or_blc_s_attr") S(be0ac, "tx_isp_wdr_s_mode") S(be124, "tx_isp_ae_scence_s_attr")
S(be188, "tx_isp_draw_block_s_attr") S(be1a4, "tx_isp_osd_block_s_attr") S(be1bc, "tx_isp_osd_s_attr")
S(be1d0, "tx_isp_mask_block_s_attr") S(be1ec, "tx_isp_autozoom_s_attr") S(be204, "tx_isp_flicker_s_attr")
S(be24c, "tiziano_isp_ccm_attr_s_ctrl") S(be268, "tx_isp_csc_s_attr") S(be27c, "tx_isp_module_s_attr")
S(be294, "tx_isp_day_or_night_s_ctrl") S(be2b0, "tx_isp_sharp_s_control") S(be2c8, "tx_isp_contrast_s_control")
S(be2e4, "tx_isp_bright_s_control") S(be2fc, "tx_isp_sat_s_control") S(be314, "tx_isp_bcsh_hue_s_ctrl")
S(c4ad4, "[ %s:%d ] copy error!!!\n") S(c4b70, "Timeout waiting for completion\n") S(c4c2c, "[ %s:%d ] get ae scence failed!!!\n")
S(c4ce0, "copy error!!!\n") S(c4cf0, "ISP and Sensor are prohibited from using mirr or flip together!!!\n")
S(c4d34, "The direct mode disables the vertical flip of ISP!!!\n") S(c4d6c, "Failed to set sensor fps=%u, ret=%d\n")
S(c4d94, "%s:%d:@@@@ vinum is %d, dn is %d, ctrl is %d\n")
S(c4dc4, "[ %s:%d ] Both BCSH and CCM are enabled or disabled simultaneously !!!\n")
S(c4e0c, "[ %s:%d ] set control failed!!!\n") S(c4e30, "[ %s:%d ] Non-wdr mode, set wdr output failed!!!\n")
S(c4e64, "[ %s:%d ] set wdr output failed!!!\n") S(c4e88, "[ %s:%d ] HBLC_Mode=%d\n") S(c4ea0, "[ %s:%d ] set hlc or blc failed!!!\n")
S(c4ec4, "[ %s:%d ] set face ae failed!!!\n") S(c4ee8, "[ %s:%d ] set face ae weight failed!!!\n")
S(c4f10, "[ %s:%d ] set awb face failed!!!\n") S(c4f34, "[ %s:%d ] set rgb offset failed!!!\n")
S(c4f58, "[ %s:%d ] set scaler level failed!!!\n") S(c4f80, "[ %s:%d ] set csccr failed!!!\n")
S(c4fa0, "[ %s:%d ] set bcsh rgb offset failed!!!\n")
/* the attribute setters' / switch bin's own strings (see t32_txattr_oracle.c, t32_txbin_oracle.c) */
S(c4af0, "[ %s:%d ] set gamma failed!!!\n") S(c4b10, "[ %s:%d ] set failed!!!\n") S(be238, "tx_isp_gamma_s_attr")
S(be21c, "tx_isp_module_ratio_s_attr") S(be170, "tx_isp_af_weight_s_attr") S(be0d4, "tx_isp_statis_config_s_attr")
S(be0c0, "tx_isp_switch_bin") S(c4b50, "[ %s:%d ] vmalloc failed!!!\n") S(c4b90, "[ %s:%d ] The camera is not activated!!!\n")
S(c4bbc, "[ %s:%d ] switch bin failed!!!\n")
/* data.c lines: bdf78..be314 data.c:1025..1256, c4* data.c:7898..8205 */

u64 tx_isp_core_ops_s_ctrl(u32, u32, u32, u32);
u64 tx_isp_gamma_s_attr_isra_7(u32, u32, u32, u32);
u64 tx_isp_module_ratio_s_attr_isra_8(u32, u32, u32, u32);
u64 tx_isp_af_weight_s_attr_isra_14(u32, u32, u32, u32);
u64 tx_isp_statis_config_s_attr_isra_22(u32, u32, u32, u32);
u64 tx_isp_switch_bin_isra_23(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 fnvb(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static u32 ret_tab[8], n_ret, cfu_tab[4], n_cfu, seed, flip_v[3], n_flip, wait_ret, notify_tab[4], n_notify, vm_ok;
static u32 next_ret(void) { return ret_tab[n_ret++ & 7]; }
static u32 last_n;		/* the size of the last copy in: what a setter's block holds */

static u32 cfu_rec(void *d, u32 u, u32 n)
{
	op3_call("private_copy_from_user", 2, u, n, 0, 0);
	memcpy(d, T32O_HOST(u), n);
	last_n = n;
	return cfu_tab[n_cfu++ & 3];
}
static u32 set_rec(const char *name, u32 ch, const void *buf)
{
	u32 n = strcmp(name, "tisp_s_ae_scence_attr") ? last_n : 40;	/* the HLC path hands the AE scene block, not the copy */

	op3_call(name, 2, ch, fnvb(buf, n), 0, 0);
	return next_ret();
}
static u32 val_rec(const char *name, u32 ch, u32 v) { op3_call(name, 2, ch, v, 0, 0); return next_ret(); }
static u32 get_rec(const char *name, u32 ch, void *buf)
{
	u32 s = seed, i;

	op3_call(name, 1, ch, 0, 0, 0);
	for (i = 0; i < 64; i++) {
		s = s * 1103515245u + 12345u;
		((u8 *)buf)[i] = (u8)(s >> 16);
	}
	return next_ret();
}
static u32 helper_rec(const char *name, u32 ch, u32 user) { op3_call(name, 2, ch, user, 0, 0); return next_ret(); }
static u32 gflip_rec(u32 ch, u8 *f) { op3_call("tisp_g_hv_flip", 2, ch, *f & 3, 0, 0); *(u32 *)f = (flip_v[n_flip++ % 3] & ~3u) | (*f & 3); return 0; }
static u32 sflip_rec(u32 ch, u8 *f) { op3_call("tisp_s_hv_flip", 2, ch, *f, 0, 0); return 0; }
static u32 lsc_rec(u32 ch, u32 a, u32 b, u32 h, u32 v)
{
	u32 x[5] = { ch, a, b, h, v };

	op3_callv("tisp_lsc_hvflip", 5, x);
	return 0;
}
static u32 wait_rec(u32 c, u32 t) { op3_call("private_wait_for_completion_timeout", 2, c, t, 0, 0); return wait_ret; }
static u32 notify_rec(u32 sd, u32 ev, u32 msg) { op3_call("sd_notify", 3, sd, ev, ((u32 *)T32O_HOST(msg))[1], 0); return notify_tab[n_notify++ & 3]; }
static u32 ls_rec(u32 v) { op3_call("lib_tisp_api_set_line_space", 1, v & 0xffff, 0, 0, 0); return 0; }
static u32 fps_rec(u32 i, u32 f) { op3_call("tisp_set_fps", 2, i, f, 0, 0); return 0; }
static u32 vm_rec(u32 n) { op3_call("private_vmalloc", 1, n, 0, 0, 0); return vm_ok ? A(O_VM) : 0; }
static void path_rec(u32 ch, u32 p)
{
	u32 k;

	op3_call("lib_tisp_get_bin_path", 2, ch, p, 0, 0);
	for (k = 0; k < 6; k++)
		snprintf((char *)T32O_HOST(p) + T32_SWBIN_PATH(k), 100, "/etc/sensor/ch%u_slot%u.bin", ch, k);
}
static void spf_rec(char *d, const char *f) { op3_call("sprintf", 1, fnvb((const u8 *)f, (u32)strlen(f)), 0, 0, 0); strcpy(d, f); }
static u32 swbin_rec(u32 ch, u8 *b) { op3_call("tisp_switch_bin", 2, ch, fnvb(b, 136), 0, 0); return next_ret(); }

/* vendor stubs */
#define SET(n) u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3); \
	u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return set_rec(#n, a0, T32O_HOST(a1)); }
SET(tisp_awb_face_set) SET(tisp_s_awb_ct_trend_offset) SET(tisp_s_ae_scence_attr) SET(tisp_s_antiflick)
SET(tisp_s_face_ae_weiget) SET(tisp_ae_face_set) SET(tisp_set_wdr_output_mode) SET(tisp_s_tmo_faceae)
SET(tisp_s_mscaler_mask_block_attr) SET(tisp_s_autozoom_control) SET(tisp_s_ccm_attr) SET(tisp_set_csc_attr)
SET(tisp_s_csccr_mode) SET(tisp_s_coefft_wb) SET(tisp_s_draw_block_attr) SET(tisp_s_osd_attr) SET(tisp_s_osd_block_attr)
SET(tisp_set_scaler_level_control) SET(tisp_s_Gamma) SET(tisp_s_module_ratio_attr) SET(tisp_s_af_weight_attr)
SET(tisp_s_statis_config_attr)
#define VAL(n) u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3); \
	u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return val_rec(#n, a0, a1); }
VAL(tisp_s_module_control) VAL(tisp_set_hue) VAL(tisp_set_brightness) VAL(tisp_set_sharpness) VAL(tisp_set_saturation)
VAL(tisp_set_contrast)
#define HLP(n) u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3); \
	u64 op3_##n(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return helper_rec(#n, a0, a1); }
HLP(tx_isp_awb_attr_s_attr_isra_18) HLP(tx_isp_awb_weight_s_attr_isra_15) HLP(tx_isp_ae_weight_s_attr_isra_21)
HLP(tx_isp_ae_expoinfo_s_attr_isra_20) HLP(tx_isp_ae_explist_s_attr_isra_29) HLP(tx_isp_ae_target_s_attr_isra_28)
u64 op3_tisp_g_ae_scence_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return get_rec("tisp_g_ae_scence_attr", a0, T32O_HOST(a1)); }
u64 op3_tisp_g_hv_flip(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return gflip_rec(a0, T32O_HOST(a1)); }
u64 op3_tisp_s_hv_flip(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sflip_rec(a0, T32O_HOST(a1)); }
u64 op3_tisp_lsc_hvflip(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 op3_tisp_lsc_hvflip(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return lsc_rec(a0, a1, a2, a3, a4); }
u64 op3_private_copy_from_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return cfu_rec(T32O_HOST(a0), a1, a2); }
u64 op3_private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return wait_rec(a0, a1); }
u64 op3_lib_tisp_api_set_line_space(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return ls_rec(a0); }
u64 op3_tisp_set_fps(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return fps_rec(a0, a1); }
u64 op3_memcpy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memcpy(T32O_HOST(a0), T32O_HOST(a1), a2); return a0; }
u64 op3_private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return vm_rec(a0); }
OP3_STUB(private_vfree, 1)
u64 op3_lib_tisp_get_bin_path(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; path_rec(a0, a1); return 0; }
u64 op3_sprintf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; spf_rec(T32O_HOST(a0), T32O_HOST(a1)); return 0; }
u64 op3_tisp_switch_bin(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return swbin_rec(a0, T32O_HOST(a1)); }
u64 op3_tx_isp_gamma_s_attr_isra_7(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_gamma_s_attr_isra_7(a0, a1, a2, a3); }
u64 op3_tx_isp_module_ratio_s_attr_isra_8(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_module_ratio_s_attr_isra_8(a0, a1, a2, a3); }
u64 op3_tx_isp_af_weight_s_attr_isra_14(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_af_weight_s_attr_isra_14(a0, a1, a2, a3); }
u64 op3_tx_isp_statis_config_s_attr_isra_22(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_statis_config_s_attr_isra_22(a0, a1, a2, a3); }
u64 op3_tx_isp_switch_bin_isra_23(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_switch_bin_isra_23(a0, a1, a2, a3); }

/* ours */
#define SETO(o, n) int o(u32 ch, void *buf) { return (int)set_rec(#n, ch, buf); }
SETO(t32_tx_os_tisp_awb_face_set, tisp_awb_face_set) SETO(t32_tx_os_tisp_s_awb_ct_trend_offset, tisp_s_awb_ct_trend_offset)
SETO(t32_tx_os_tisp_s_ae_scence_attr, tisp_s_ae_scence_attr) SETO(t32_tx_os_tisp_s_antiflick, tisp_s_antiflick)
SETO(t32_tx_os_tisp_s_face_ae_weiget, tisp_s_face_ae_weiget) SETO(t32_tx_os_tisp_ae_face_set, tisp_ae_face_set)
SETO(t32_tx_os_tisp_set_wdr_output_mode, tisp_set_wdr_output_mode) SETO(t32_tx_os_tisp_s_tmo_faceae, tisp_s_tmo_faceae)
SETO(t32_tx_os_tisp_s_mscaler_mask_block_attr, tisp_s_mscaler_mask_block_attr)
SETO(t32_tx_os_tisp_s_autozoom_control, tisp_s_autozoom_control) SETO(t32_tx_os_tisp_s_ccm_attr, tisp_s_ccm_attr)
SETO(t32_tx_os_tisp_set_csc_attr, tisp_set_csc_attr) SETO(t32_tx_os_tisp_s_csccr_mode, tisp_s_csccr_mode)
SETO(t32_tx_os_tisp_s_coefft_wb, tisp_s_coefft_wb) SETO(t32_tx_os_tisp_s_draw_block_attr, tisp_s_draw_block_attr)
SETO(t32_tx_os_tisp_s_osd_attr, tisp_s_osd_attr) SETO(t32_tx_os_tisp_s_osd_block_attr, tisp_s_osd_block_attr)
SETO(t32_tx_os_tisp_set_scaler_level_control, tisp_set_scaler_level_control)
SETO(t32_tx_os_tisp_s_gamma, tisp_s_Gamma) SETO(t32_tx_os_tisp_s_module_ratio, tisp_s_module_ratio_attr)
SETO(t32_tx_os_tisp_s_af_weight, tisp_s_af_weight_attr) SETO(t32_tx_os_tisp_s_statis_config, tisp_s_statis_config_attr)
#define VALO(o, n) int o(u32 ch, u32 v) { return (int)val_rec(#n, ch, v); }
VALO(t32_tx_os_tisp_s_module_control, tisp_s_module_control) VALO(t32_tx_os_tisp_set_hue, tisp_set_hue)
VALO(t32_tx_os_tisp_set_brightness, tisp_set_brightness) VALO(t32_tx_os_tisp_set_sharpness, tisp_set_sharpness)
VALO(t32_tx_os_tisp_set_saturation, tisp_set_saturation) VALO(t32_tx_os_tisp_set_contrast, tisp_set_contrast)
#define HLPO(o, n) int o(u32 ch, u32 u) { return (int)helper_rec(#n, ch, u); }
HLPO(t32_tx_os_awb_attr_s_attr, tx_isp_awb_attr_s_attr_isra_18) HLPO(t32_tx_os_awb_weight_s_attr, tx_isp_awb_weight_s_attr_isra_15)
HLPO(t32_tx_os_ae_weight_s_attr, tx_isp_ae_weight_s_attr_isra_21) HLPO(t32_tx_os_ae_expoinfo_s_attr, tx_isp_ae_expoinfo_s_attr_isra_20)
HLPO(t32_tx_os_ae_explist_s_attr, tx_isp_ae_explist_s_attr_isra_29) HLPO(t32_tx_os_ae_target_s_attr, tx_isp_ae_target_s_attr_isra_28)
int t32_tx_os_tisp_g_ae_scence_attr(u32 ch, void *b) { return (int)get_rec("tisp_g_ae_scence_attr", ch, b); }
int t32_tx_os_tisp_g_hv_flip(u32 ch, void *f) { return (int)gflip_rec(ch, f); }
int t32_tx_os_tisp_s_hv_flip(u32 ch, void *f) { return (int)sflip_rec(ch, f); }
int t32_tx_os_tisp_lsc_hvflip(u32 ch, u32 a, u32 b, u32 h, u32 v) { return (int)lsc_rec(ch, a, b, h, v); }
u32 t32_tx_os_copy_from_user(void *d, u32 u, u32 n) { return cfu_rec(d, u, n); }
u32 t32_tx_os_copy_to_user(u32 u, const void *s, u32 n) { (void)u; (void)s; (void)n; return 0; }
int t32_tx_os_wait_for_completion_timeout(u32 c, u32 t) { return (int)wait_rec(c, t); }
void t32_tx_os_lib_tisp_api_set_line_space(u32 v) { ls_rec(v); }
int t32_tx_os_tisp_set_fps(u32 i, u32 f) { return (int)fps_rec(i, f); }
u32 t32_tx_os_vmalloc(u32 n) { return vm_rec(n); }
void t32_tx_os_vfree(u32 a) { op3_call("private_vfree", 1, a, 0, 0, 0); }
void t32_tx_os_get_bin_path(u32 ch, u32 b) { path_rec(ch, b); }
void t32_tx_os_sprintf(char *d, const char *f) { spf_rec(d, f); }
int t32_tx_os_tisp_switch_bin(u32 ch, void *b) { return (int)swbin_rec(ch, b); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset(T32O_HOST(a), (int)c, n); }
/* unused hooks of the included headers */
int t32_tx_os_tisp_g_gamma(u32 c, void *b) { (void)c; (void)b; return 0; }
int t32_tx_os_tisp_g_module_ratio(u32 c, void *b) { (void)c; (void)b; return 0; }
int t32_tx_os_tisp_g_af_weight(u32 c, void *b) { (void)c; (void)b; return 0; }
int t32_tx_os_tisp_g_statis_config(u32 c, void *b) { (void)c; (void)b; return 0; }
int t32_tx_os_tisp_g_autozoom(u32 c, void *b) { (void)c; (void)b; return 0; }
u32 t32_tx_os_sensor_start_iq_index(u32 s) { return s; }
u32 t32_tx_os_sensor_section_num(u32 s, u32 i) { (void)i; return s; }
u32 t32_tx_os_sensor_setting(u32 s, u32 i, u32 k) { (void)i; (void)k; return s; }
u32 t32_tx_os_sensor_setting_len(u32 s, u32 i, u32 k) { (void)i; (void)k; return s; }
void t32_tx_os_sensor_setting_md5(u32 s, u32 i, u32 k) { (void)s; (void)i; (void)k; }
void t32_tx_os_tisp_get_bin_info(u32 s, void *b) { (void)s; (void)b; }
u32 t32_tx_os_tisp_day_or_night(u32 s) { return s; }
u32 t32_tx_os_tisp_wdr_enable(u32 s) { return s; }

static const u32 ids[] = { 0x00, 0x10, 0x12, 0x14, 0x17, 0x21, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2a, 0x2d, 0x2e, 0x32,
	0x35, 0x54, 0x62, 0x70, 0x71, 0x72, 0x73, 0x74, 0x77, 0x80, 0x81, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x9b, 0xa0,
	0xa1, 0xa2, 0xa4, 0xa5, 0xa6, 0xa7 };

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_ret = n_cfu = n_flip = n_notify = 0;
	last_n = 0;
}

static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a3; (void)a4; return (u32)t32_tx_core_ops_s_ctrl(a0, a1, a2); }

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 ret;
};
static struct snap sv, so;

int main(void)
{
	unsigned int i, k;
	u32 base, lm;

	op3_mask_fmt = op3_t32_data_c4d94;
	op3_mask_args = 1u << 2;
	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		u32 m = op3_rnd(), ch = op3_rnd() % 4, id;

		id = (m & 7) == 0 ? op3_rnd() % 0x100 : ids[op3_rnd() % (sizeof(ids) / sizeof(ids[0]))];
		id = (m & 0xff) == 8 ? op3_rnd() : id + T32_SC_BASE;
		for (k = 0; k < ARENA; k += 4)
			sw(k, op3_rnd());
		sw(O_ISP, base + O_SD);
		sw(O_SD + 280, base + O_CORE);
		sw(O_SD + 152, (m & 0x100) ? 0 : T32O_PTR(notify_rec));
		sw(O_CORE + 1196, (m & 0x200) ? 0 : 1 + op3_rnd() % 4);
		for (k = 0; k < 4; k++) {
			sw(O_CORE + 4 * (k + 294) + 4, (op3_rnd() & 1) ? 1 : op3_rnd() % 3);
			sw(O_CORE + 392 + 104 * k, op3_rnd() % 3);
			sw(O_CORE + T32_CORE_IN_STATE(k), (op3_rnd() & 1) ? 4 : op3_rnd() % 6);
			sw(O_ISP + 28 + 32 * k, op3_rnd() % 4);
			sw(O_ISP + 32 + 32 * k, op3_rnd() % 3);
		}
		sw(O_CTRL, id);
		sw(O_CTRL + 4, base + O_USER);
		if (id == T32_SC_BASE + 0x70)
			sw(O_CTRL + 4, op3_rnd() % 3);
		/* user blocks: small values where the vendor branches on them */
		sw(O_USER, op3_rnd() % 4);
		for (k = 1; k < 4; k++)
			sw(O_USER + 4 * k, (op3_rnd() & 1) ? 0 : op3_rnd() % 4);
		tmpl[O_USER + 4] = (u8)(op3_rnd() % 6);
		seed = op3_rnd();
		for (k = 0; k < 8; k++)
			ret_tab[k] = (op3_rnd() & 3) ? 0 : op3_rnd();
		for (k = 0; k < 4; k++) {
			cfu_tab[k] = (op3_rnd() & 7) ? 0 : op3_rnd() % 40 + 1;
			notify_tab[k] = (op3_rnd() & 3) ? 0 : ((op3_rnd() & 1) ? (u32)-515 : op3_rnd());
		}
		for (k = 0; k < 3; k++)
			flip_v[k] = op3_rnd();
		wait_ret = (op3_rnd() & 1) ? 0 : op3_rnd();
		vm_ok = (op3_rnd() & 7) != 0;
		lm = (op3_rnd() & 1) ? op3_rnd() : 0;

		setup();
		op3_t32_data_d9014 = lm;
		sv.ret = OP3_VC(tx_isp_core_ops_s_ctrl, ch, base + O_ISP, base + O_CTRL, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		setup();
		t32_tx_link_mode = lm;
		so.ret = OP3_VC(ours, ch, base + O_ISP, base + O_CTRL, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		if (op3_log_cmp("core_ops_s_ctrl", &sv.log, &so.log)) {
			fprintf(stderr, "  id %x ch %u\n", id, ch);
			return 1;
		}
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA)) {
			u32 j;

			for (j = 0; j < ARENA && sv.w[j] == so.w[j]; j++)
				;
			fprintf(stderr, "core_ops_s_ctrl: id %x ret %x/%x, first world diff 0x%x\n", id, sv.ret, so.ret, j);
			return 1;
		}
	}
	printf("t32_txsctrl_oracle: %u cases equal (%u control ids)\n", NCASE, (unsigned int)(sizeof(ids) / sizeof(ids[0])));
	return 0;
}
