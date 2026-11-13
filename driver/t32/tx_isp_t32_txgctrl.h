/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core get-control (vendor tx_isp_core_ops_g_ctrl): control ids
 * 0x8000000 + n, the user pointer at +4 of the control.  Each id either
 * hands the control to an attribute helper (.isra, the five of
 * tx_isp_t32_txattr.h are ours, the AE / AWB / CCM ones are hooks) or reads
 * a tisp getter into a kernel buffer and copies it out.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txgctrl_oracle).  Not covered there:
 * the frame-done wait (0x91) after its first __copy_user, since the
 * recovered C keeps a2 across that call (the real one returns the left
 * count in it) and so always takes the failure path.
 */
#ifndef TX_ISP_T32_TXGCTRL_H
#define TX_ISP_T32_TXGCTRL_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txattr.h"

#define T32_GC_BASE		0x8000000U
#define T32_GC_COPY_ERR		"[ %s:%d ] copy error!!!\n"
#define T32_GC_GET_ERR		"[ %s:%d ] get control failed!!!\n"

typedef int (*t32_tx_gc_get_fn)(u32 ch, void *buf);

/* provided by the driver glue (kernel) or by the oracle (host) */
int t32_tx_os_awb_attr_g_attr(u32 ch, u32 arg);
int t32_tx_os_awb_weight_g_attr(u32 ch, u32 arg);
int t32_tx_os_awb_only_read_attr(u32 ch, u32 arg);
int t32_tx_os_ae_weight_g_attr(u32 ch, u32 arg);
int t32_tx_os_ae_expoinfo_g_attr(u32 ch, u32 arg);
int t32_tx_os_ae_scence_g_attr(u32 ch, u32 arg);
int t32_tx_os_ae_explist_g_attr(u32 ch, u32 arg);
int t32_tx_os_ae_only_read_g_attr(u32 ch, u32 arg);
int t32_tx_os_ae_target_g_attr(u32 ch, u32 arg);
int t32_tx_os_ae_hlc_or_blc_g_attr(u32 ch, u32 arg);
int t32_tx_os_ccm_attr_g_ctrl(u32 ch, u32 arg);
int t32_tx_os_tisp_g_awb_statis_attr(u32 ch, void *buf);
int t32_tx_os_tisp_g_awb_global_statis_attr(u32 ch, void *buf);
int t32_tx_os_tisp_awb_face_get(u32 ch, void *buf);
int t32_tx_os_tisp_g_awb_ct_trend_offset(u32 ch, void *buf);
int t32_tx_os_tisp_g_ae_statis_attr(u32 ch, void *buf);
int t32_tx_os_tisp_g_antiflick(u32 ch, void *buf);
int t32_tx_os_tisp_g_ae_flicker_flag(u32 ch, void *buf);
int t32_tx_os_tisp_g_face_ae_luma(u32 ch, void *buf);
int t32_tx_os_tisp_g_face_ae_weiget(u32 ch, void *buf);
int t32_tx_os_tisp_g_af_statis_attr(u32 ch, void *buf);
int t32_tx_os_tisp_g_af_metric_attr(u32 ch, void *buf);
int t32_tx_os_tisp_ae_face_get(u32 ch, void *buf);
int t32_tx_os_tisp_get_wdr_output_mode(u32 ch, void *buf);
int t32_tx_os_tisp_get_fps(u32 ch, u32 arg);
int t32_tx_os_tisp_g_module_control(u32 ch, void *buf);
int t32_tx_os_tisp_g_hv_flip(u32 ch, void *flags);
int t32_tx_os_tisp_g_mscaler_mask_block_attr(u32 ch, void *buf);
int t32_tx_os_tisp_get_csc_attr(u32 ch, void *buf);
int t32_tx_os_tisp_g_csccr_mode(u32 ch, void *buf);
int t32_tx_os_tisp_g_coefft_wb(u32 ch, void *buf);
int t32_tx_os_tisp_g_draw_block_attr(u32 ch, void *buf);
int t32_tx_os_tisp_g_osd_attr(u32 ch, void *buf);
int t32_tx_os_tisp_g_osd_block_attr(u32 ch, void *buf);
int t32_tx_os_lib_tisp_api_get_line_space(void *buf);
int t32_tx_os_isp_frame_done_wait(u32 ch, u32 wait, void *count);
u32 t32_tx_os_thread_limit(void);
u32 t32_tx_os_copy_user(void *dst, const void *src, u32 n);

/* getter, a failed getter prints "get control failed" (or fail) at level, then the copy out */
static inline int t32_tx_gc_get(u32 ch, u32 ctrl, t32_tx_gc_get_fn get, u32 n, const char *fn, int line,
				u32 level, const char *fail)
{
	u32 buf[24];		/* the vendor's 96 byte stack block */
	int r = get(ch, buf);

	if (r) {
		t32_tx_os_isp_printf(level, fail ? fail : T32_GC_GET_ERR, fn, line);
		return r;
	}
	r = (int)t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), buf, n);
	if (r)
		t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, fn, line + 6);
	return r;
}

/* getter without a result test, then the copy out */
static inline int t32_tx_gc_get_nochk(u32 ch, u32 ctrl, t32_tx_gc_get_fn get, u32 n, const char *fn, int line)
{
	u32 buf[24];
	int r;

	get(ch, buf);
	r = (int)t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), buf, n);
	if (r)
		t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, fn, line);
	return r;
}

/* statistics into a vmalloc buffer.  Vendor: a failed getter returns without the vfree (leak) */
static inline int t32_tx_gc_statis(u32 ch, u32 ctrl, t32_tx_gc_get_fn get, u32 n, int clear, const char *fn, int line)
{
	u32 b = t32_tx_os_vmalloc(n);
	int r;

	if (clear)
		t32_tx_os_memset(b, 0, n);
	r = get(ch, T32_TX_P(b));
	if (r) {
		t32_tx_os_isp_printf(1, T32_GC_GET_ERR, fn, line);
		return r;
	}
	r = (int)t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), T32_TX_P(b), n);
	if (r)
		t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, fn, line + 6);
	t32_tx_os_vfree(b);
	return r;
}

/* 0x73: mirror / flip state of the three channels of input ch */
static inline int t32_tx_gc_hv_flip(u32 ch, u32 isp, u32 ctrl)
{
	u32 buf[24] = { 0, 0, 0, 0 }, k, f = 0, *flags = &f;

	for (k = 0; k < 3; k++) {
		*(u8 *)flags = (u8)((*(u8 *)flags & ~3U) | k);
		t32_tx_os_tisp_g_hv_flip(ch, flags);
		if (((*flags >> 2) & 1) != ((*flags >> 3) & 1))
			t32_tx_os_isp_printf(1, "The mirr status of ch%d is abnormal.", k);
		else if (*flags & 4)
			buf[1 + k] |= 1;
		if (((*flags >> 4) & 1) != ((*flags >> 5) & 1))
			t32_tx_os_isp_printf(1, "The flip status of ch%d is abnormal.", k);
		else if (*flags & 0x10)
			buf[1 + k] |= 2;
	}
	buf[0] = t32_tx_ld(isp, 28 + 32 * ch);
	if (t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), buf, 16))
		t32_tx_os_isp_printf(2, "copy error!!!\n");
	return 0;
}

/*
 * 0x91: wait for a frame done.  The user block { wait, ... } in, { wait,
 * x, count lo, count hi, 0, .. } out (24 bytes) through the inlined
 * access_ok + __copy_user; a failed check or copy returns the left count.
 */
static inline int t32_tx_gc_frame_done(u32 ch, u32 ctrl)
{
	u32 buf[6], user = t32_tx_ld(ctrl, 4), left, cnt[2];

	cnt[0] = 0;
	cnt[1] = 0;
	if (((user + 24) | user) & t32_tx_os_thread_limit()) {
		t32_tx_os_memset(T32_TX_A(buf), 0, 24);
		t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_wait_frame_done", 92);
		return 24;
	}
	left = t32_tx_os_copy_user(buf, T32_TX_P(user), 24);
	if (left) {
		t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_wait_frame_done", 92);
		return (int)left;
	}
	t32_tx_os_isp_frame_done_wait(ch, buf[0], cnt);
	user = t32_tx_ld(ctrl, 4);
	buf[2] = cnt[0];
	buf[3] = cnt[1];
	buf[4] = 0;
	if (((user + 24) | user) & t32_tx_os_thread_limit()) {
		t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_wait_frame_done", 103);
		return 24;
	}
	left = t32_tx_os_copy_user(T32_TX_P(user), buf, 24);
	if (left)
		t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_wait_frame_done", 103);
	return (int)left;
}

/* tx_isp_core_ops_g_ctrl(ch, isp, ctrl) */
static inline int t32_tx_core_ops_g_ctrl(u32 ch, u32 isp, u32 ctrl)
{
	u32 core, buf[24], v;
	int r;

	switch (t32_tx_ld(ctrl, 0) - T32_GC_BASE) {
	case 0x00: return t32_tx_statis_config_g_attr(ch, ctrl + 4);
	case 0x10: return t32_tx_os_awb_attr_g_attr(ch, ctrl + 4);
	case 0x11:
		return t32_tx_gc_statis(ch, ctrl, t32_tx_os_tisp_g_awb_statis_attr, 12288, 1, "tx_isp_awb_statis_g_attr", 914);
	case 0x12: return t32_tx_os_awb_weight_g_attr(ch, ctrl + 4);
	case 0x13: {
		u32 b[24] = { 0, 0, 0, 0 };

		r = t32_tx_os_tisp_g_awb_global_statis_attr(ch, b);
		if (r) {
			t32_tx_os_isp_printf(1, T32_GC_GET_ERR, "tx_isp_awb_global_statis_g_attr", 937);
			return r;
		}
		r = (int)t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), b, 16);
		if (r)
			t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_awb_global_statis_g_attr", 943);
		return r;
	}
	case 0x14:
		return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_awb_face_get, 24, "tx_isp_faceawb_g_attr", 1565, 1,
				     "[ %s:%d ] get awb face attr failed!!!\n");
	case 0x15: return t32_tx_os_awb_only_read_attr(ch, ctrl + 4);
	case 0x17: return t32_tx_gc_get_nochk(ch & 0xff, ctrl, t32_tx_os_tisp_g_awb_ct_trend_offset, 28,
					      "tx_isp_awb_g_ct_trend_offset", 1642);
	case 0x21: return t32_tx_os_ae_weight_g_attr(ch, ctrl + 4);
	case 0x22:
		return t32_tx_gc_statis(ch, ctrl, t32_tx_os_tisp_g_ae_statis_attr, 1934, 1, "tx_isp_ae_statis_g_attr", 1050);
	case 0x23: return t32_tx_os_ae_expoinfo_g_attr(ch, ctrl + 4);
	case 0x24: return t32_tx_os_ae_scence_g_attr(ch, ctrl + 4);
	case 0x25: return t32_tx_gamma_g_attr(ch, ctrl + 4);
	case 0x26: return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_antiflick, 8, "tx_isp_flicker_g_attr", 607, 2, NULL);
	case 0x27: return t32_tx_os_ae_explist_g_attr(ch, ctrl + 4);
	case 0x28: return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_ae_flicker_flag, 8, "tx_isp_ae_g_flicker_flag", 1773, 2, NULL);
	case 0x29:
		return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_face_ae_luma, 8, "tx_isp_face_ae_luma_g_attr", 1547, 1,
				     "[ %s:%d ] get face ae luma failed!!!\n");
	case 0x2a:
		return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_face_ae_weiget, 12, "tx_isp_face_ae_weiget_g_attr", 1529, 1,
				     "[ %s:%d ] get face ae weight failed!!!\n");
	case 0x2b: return t32_tx_os_ae_only_read_g_attr(ch, ctrl + 4);
	case 0x2d: return t32_tx_os_ae_target_g_attr(ch, ctrl + 4);
	case 0x2e: return t32_tx_os_ae_hlc_or_blc_g_attr(ch, ctrl + 4);
	case 0x31:
		return t32_tx_gc_statis(ch, ctrl, t32_tx_os_tisp_g_af_statis_attr, 5400, 0, "tx_isp_af_statis_g_attr", 1026);
	case 0x32: return t32_tx_af_weight_g_attr(ch, ctrl + 4);
	case 0x33: {	/* sensor attributes of input ch (core +376 + 104 ch: sensor, +384 .. +392) */
		u32 in = t32_tx_ld(t32_tx_ld(isp, 0), 280) + 104 * ch, sns = t32_tx_ld(in, 376);

		buf[0] = t32_tx_h(sns + 276);
		buf[1] = t32_tx_h(sns + 278);
		buf[2] = t32_tx_ld(in, 392);
		buf[3] = t32_tx_ld(in, 384);
		buf[4] = t32_tx_ld(in, 388);
		r = (int)t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), buf, 20);
		if (r)
			t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_sensor_g_attr", 24);
		return r;
	}
	case 0x34:
		return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_af_metric_attr, 12, "tx_isp_af_metrics_g_attr", 1144, 1, NULL);
	case 0x35:
		return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_ae_face_get, 24, "tx_isp_face_ae_g_attr", 1492, 1,
				     "[ %s:%d ] get face ae failed!!!\n");
	case 0x54:	/* WDR output mode; vendor: the getter result is tested after the copy */
		core = t32_tx_ld(t32_tx_ld(isp, 0), 280);
		if (t32_tx_ld(core, 4 * (ch + 294) + 4) != 1) {
			t32_tx_os_isp_printf(1, "[ %s:%d ] Non-wdr mode, get wdr output failed!!!\n", "tx_isp_wdr_g_mode", 1699);
			return 0;
		}
		r = t32_tx_os_tisp_get_wdr_output_mode(ch, buf);
		t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), buf, 4);
		if (r)
			t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_wdr_g_mode", 1705);
		return r;
	case 0x70:
		t32_tx_os_tisp_get_fps(ch & 0xff, ctrl + 4);
		return 0;
	case 0x71:
		t32_tx_st(ctrl, 4, t32_tx_ld(isp, 32 + 32 * ch));
		return 0;
	case 0x72: return t32_tx_gc_get_nochk(ch, ctrl, t32_tx_os_tisp_g_module_control, 4, "tx_isp_module_g_attr", 356);
	case 0x73: return t32_tx_gc_hv_flip(ch, isp, ctrl);
	case 0x74: return t32_tx_gc_get_nochk(ch, ctrl, t32_tx_os_tisp_g_mscaler_mask_block_attr, 24, "tx_isp_mask_block_g_attr", 679);
	case 0x77: return t32_tx_autozoom_g_attr(ch, ctrl + 4);
	case 0x78:	/* maximum fps of input ch (core +400 + 104 ch) */
		v = t32_tx_ld(t32_tx_ld(isp, 0), 280) + 104 * ch + 400;
		r = (int)t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), T32_TX_P(v), 4);
		if (r)
			t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_max_fps_g_attr", 124);
		return r;
	case 0x80: return t32_tx_os_ccm_attr_g_ctrl(ch, ctrl + 4);
	case 0x91: return t32_tx_gc_frame_done(ch, ctrl);
	case 0x96: {	/* vendor: a failed copy is printed and 0 returned */
		u32 b[24];

		t32_tx_os_tisp_get_csc_attr(ch, b);
		if (t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), b, 92))
			t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_csc_g_attr", 444);
		return 0;
	}
	case 0x97:
		return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_csccr_mode, 12, "tx_isp_g_csccr_attr", 1755, 1,
				     "[ %s:%d ] get csccr failed!!!\n");
	case 0x9b:
		return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_coefft_wb, 6, "tx_isp_bcsh_rgb_offset_g_attr", 554, 1,
				     "[ %s:%d ] get bcsh rgb offset failed!!!\n");
	case 0xa0: return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_draw_block_attr, 36, "tx_isp_draw_block_g_attr", 776, 1, NULL);
	case 0xa1: return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_osd_attr, 32, "tx_isp_osd_g_attr", 707, 1, NULL);
	case 0xa2: return t32_tx_gc_get(ch, ctrl, t32_tx_os_tisp_g_osd_block_attr, 20, "tx_isp_osd_block_g_attr", 742, 1, NULL);
	case 0xa4: return t32_tx_module_ratio_g_attr(ch, ctrl + 4);
	case 0xa7: {
		u16 ls[48] = { 0, 0 };

		t32_tx_os_lib_tisp_api_get_line_space(ls);
		r = (int)t32_tx_os_copy_to_user(t32_tx_ld(ctrl, 4), ls, 2);
		if (r)
			t32_tx_os_isp_printf(2, T32_GC_COPY_ERR, "tx_isp_double_g_hb", 1461);
		return r;
	}
	default:
		return -1;
	}
}

#endif
