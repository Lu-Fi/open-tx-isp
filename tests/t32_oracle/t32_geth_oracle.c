// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the API front ends of tx_isp_t32_api_get.h that call a
 * block getter (44 functions: tisp_g_* / tisp_get_* wrappers, the three
 * register getters tisp_g_module_control / tisp_g_ccm_attr /
 * tisp_get_frame_drop) against the vendor functions (Matt Davis' recovered
 * C, PR #15 @ 2e2e06bc).
 *
 * The block getters they call are recording stubs here (vendor symbol names
 * for the reference, struct t32_get_ops for ours): each call logs callee,
 * channel and the arguments the front end sets (pointers relative to the
 * caller's buffer), writes 8 scripted bytes into the buffer it gets (half of the calls; 24 / 4
 * bytes into the local record of tisp_af_api_get_attr /
 * tisp_wdr_api_get_output_mode) and returns a scripted value.  Compared per
 * call: return value, the call log (count, order, arguments), every byte of a
 * 256 byte random output buffer and the tuning attribute block (tisp_tattr).
 * Random 32-bit channels (masking), cfgMode (d03a0) 0 and 1, NULL buffer of
 * tisp_g_ae_statis_attr.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_get.h"

u32 t32_data_f0904 __attribute__((weak));		/* .bss +0x17904: tisp_tattr */

#define BUF	256
#define TATTR	0x10000
#define MAXC	8

/* callee ids, argument count the front end sets, pointer-argument mask, local record size (0: writes the caller's buffer) */
enum {
	C_REG, C_MIRR, C_FPS, C_BV, C_BRIGHT, C_CSC, C_CCM, C_BCSHCCM, C_ANTIFLICK, C_AUTOZOOM, C_FCROP, C_OSDATTR,
	C_OSDOUT, C_LINE, C_AFW, C_AWBW, C_CTTREND, C_AWBATTR, C_ZONE, C_GSTAT, C_SCENE, C_EXPR, C_AESTAT,
	C_AFSTAT, C_AEW, C_AFATTR, C_AWBSTABLE, C_STEP, C_TGAIN, C_AESTABLE, C_WDR, C_CSCCR, C_RGB, C_EVL,
	C_ATL, C_FLICK, C_BLC, C_GAMMA, C_METRIC, C_EXPL, C_FACEP, C_ROI, C_TMO, NCALLEE
};
static const struct { u8 nargs, ptrs, local; } ck[NCALLEE] = {
	[C_REG] = { 2, 0, 0 }, [C_GAMMA] = { 3, 2, 0 }, [C_METRIC] = { 4, 14, 0 }, [C_EXPL] = { 3, 6, 0 },
	[C_FACEP] = { 4, 14, 0 }, [C_ROI] = { 3, 6, 0 }, [C_TMO] = { 3, 4, 0 },
	[C_AFATTR] = { 1, 0, 24 }, [C_WDR] = { 1, 0, 4 },
};

struct call { u32 id, a[4]; };
struct side { unsigned int n, sc; struct call c[MAXC]; uintptr_t base; };
static struct side sv, so, *cur;
static u32 script[MAXC];
static u8 sbytes[MAXC][24];

static u32 hook(u32 id, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	unsigned int na = ck[id].nargs ? ck[id].nargs : 2, ptrs = ck[id].nargs ? ck[id].ptrs : 2, i, k = cur->sc++;
	uintptr_t a[4] = { a0, a1, a2, a3 };

	if (cur->n < MAXC) {
		cur->c[cur->n].id = id;
		for (i = 0; i < 4; i++)
			cur->c[cur->n].a[i] = i >= na ? 0 : (ptrs >> i & 1) ? (u32)(a[i] - cur->base) : (u32)a[i];
		cur->n++;
	}
	if (k >= MAXC)
		return 0;
	if (ck[id].local)
		memcpy((void *)a1, sbytes[k], ck[id].local);
	else if (id != C_REG && id != C_TMO && a1 && script[k] >> 31)	/* half of the calls leave the buffer alone */
		memcpy((void *)a1, sbytes[k], 8);
	else if (id == C_TMO)
		memcpy((void *)a2, sbytes[k], 8);
	return script[k];
}

/* vendor side: the symbols the reference calls (o32 shape) */
#define VSTUB(sym, id) u64 sym(u32 a0, u32 a1, u32 a2, u32 a3) { return hook(id, a0, a1, a2, a3); }
VSTUB(tisp_reg_read, C_REG) VSTUB(tisp_msca_get_mirr_flip, C_MIRR) VSTUB(tisp_ae_api_get_fps, C_FPS)
VSTUB(tisp_ae_get_bv, C_BV) VSTUB(tisp_bcsh_api_get_brightness, C_BRIGHT) VSTUB(tisp_csc_api_get, C_CSC)
VSTUB(tisp_ccm_api_get, C_CCM) VSTUB(tisp_bcsh_api_get_ccm, C_BCSHCCM) VSTUB(tisp_ae_api_get_antiflick, C_ANTIFLICK)
VSTUB(tisp_msca_api_get_autozoom, C_AUTOZOOM) VSTUB(tisp_msca_api_get_fcrop, C_FCROP)
VSTUB(tisp_msca_api_get_osd_out_attribute_cfg, C_OSDATTR) VSTUB(tisp_msca_api_get_osd_out_cfg, C_OSDOUT)
VSTUB(tisp_msca_api_get_line_out_cfg, C_LINE) VSTUB(tisp_af_api_get_weight, C_AFW) VSTUB(tisp_awb_get_weight, C_AWBW)
VSTUB(tisp_awb_api_get_ct_trend_offset, C_CTTREND) VSTUB(tisp_awb_get_attr, C_AWBATTR) VSTUB(tisp_awb_get_zone, C_ZONE)
VSTUB(tisp_awb_get_global_statis, C_GSTAT) VSTUB(tisp_ae_api_get_scene_par, C_SCENE)
VSTUB(api_ae_get_exprinfo_attr, C_EXPR) VSTUB(tisp_ae_get_ae_statis, C_AESTAT)
VSTUB(tisp_af_api_get_statis_info, C_AFSTAT) VSTUB(api_ae_get_weight, C_AEW) VSTUB(tisp_af_api_get_attr, C_AFATTR)
VSTUB(tisp_awb_api_get_awb_stable, C_AWBSTABLE) VSTUB(api_ae_get_antiflicker_step, C_STEP)
VSTUB(api_ae_get_tgain, C_TGAIN) VSTUB(tisp_ae_api_get_stable, C_AESTABLE) VSTUB(tisp_wdr_api_get_output_mode, C_WDR)
VSTUB(tisp_csccr_api_get, C_CSCCR) VSTUB(tisp_bcsh_api_get_offset_rgb, C_RGB) VSTUB(tisp_ae_get_ev_list, C_EVL)
VSTUB(tisp_ae_get_at_list, C_ATL) VSTUB(tisp_ae_api_get_flicker_flag, C_FLICK) VSTUB(tisp_gib_api_get_blc, C_BLC)
VSTUB(tisp_gamma_api_get, C_GAMMA) VSTUB(tisp_af_api_get_metric, C_METRIC) VSTUB(tisp_ae_api_get_exp_list, C_EXPL)
VSTUB(tisp_ae_api_get_face_ae_tune_params, C_FACEP) VSTUB(tisp_ae_api_get_gobal_roi_luma, C_ROI)
VSTUB(tisp_tmo_api_get_curve, C_TMO)

/* our side: struct t32_get_ops */
#define OHOOK(nm, id) static int o_##nm(u32 ch, void *b) { return (int)hook(id, ch, (uintptr_t)b, 0, 0); }
OHOOK(mirr, C_MIRR) OHOOK(fps, C_FPS) OHOOK(bv, C_BV) OHOOK(bright, C_BRIGHT) OHOOK(csc, C_CSC) OHOOK(ccm, C_CCM)
OHOOK(bcshccm, C_BCSHCCM) OHOOK(antiflick, C_ANTIFLICK) OHOOK(autozoom, C_AUTOZOOM) OHOOK(fcrop, C_FCROP)
OHOOK(osdattr, C_OSDATTR) OHOOK(osdout, C_OSDOUT) OHOOK(line, C_LINE) OHOOK(afw, C_AFW) OHOOK(awbw, C_AWBW)
OHOOK(cttrend, C_CTTREND) OHOOK(awbattr, C_AWBATTR) OHOOK(zone, C_ZONE) OHOOK(gstat, C_GSTAT) OHOOK(scene, C_SCENE)
OHOOK(expr, C_EXPR) OHOOK(aestat, C_AESTAT) OHOOK(afstat, C_AFSTAT) OHOOK(aew, C_AEW) OHOOK(afattr, C_AFATTR)
OHOOK(awbstable, C_AWBSTABLE) OHOOK(step, C_STEP) OHOOK(tgain, C_TGAIN) OHOOK(aestable, C_AESTABLE) OHOOK(wdr, C_WDR)
OHOOK(csccr, C_CSCCR) OHOOK(rgb, C_RGB) OHOOK(evl, C_EVL) OHOOK(atl, C_ATL) OHOOK(flick, C_FLICK) OHOOK(blc, C_BLC)
static u32 o_reg(u32 ch, u32 reg) { return hook(C_REG, ch, reg, 0, 0); }
static int o_gamma(u32 ch, void *b, u32 sel) { return (int)hook(C_GAMMA, ch, (uintptr_t)b, sel, 0); }
static int o_metric(u32 ch, void *b, void *p4, void *p8) { return (int)hook(C_METRIC, ch, (uintptr_t)b, (uintptr_t)p4, (uintptr_t)p8); }
static int o_expl(u32 ch, void *b, void *p4) { return (int)hook(C_EXPL, ch, (uintptr_t)b, (uintptr_t)p4, 0); }
static int o_facep(u32 ch, void *b, void *p4, void *p8) { return (int)hook(C_FACEP, ch, (uintptr_t)b, (uintptr_t)p4, (uintptr_t)p8); }
static int o_roi(u32 ch, void *b, void *p4) { return (int)hook(C_ROI, ch, (uintptr_t)b, (uintptr_t)p4, 0); }
static int o_tmo(u32 ch, u32 idx, void *c) { return (int)hook(C_TMO, ch, idx, (uintptr_t)c, 0); }

static const struct t32_get_ops ops = {
	.reg_read = o_reg, .msca_get_mirr_flip = o_mirr, .ae_api_get_fps = o_fps, .ae_get_bv = o_bv,
	.bcsh_api_get_brightness = o_bright, .csc_api_get = o_csc, .ccm_api_get = o_ccm, .bcsh_api_get_ccm = o_bcshccm,
	.ae_api_get_antiflick = o_antiflick, .msca_api_get_autozoom = o_autozoom, .msca_api_get_fcrop = o_fcrop,
	.msca_api_get_osd_out_attribute_cfg = o_osdattr, .msca_api_get_osd_out_cfg = o_osdout,
	.msca_api_get_line_out_cfg = o_line, .af_api_get_weight = o_afw, .awb_get_weight = o_awbw,
	.awb_api_get_ct_trend_offset = o_cttrend, .awb_get_attr = o_awbattr, .awb_get_zone = o_zone,
	.awb_get_global_statis = o_gstat, .ae_api_get_scene_par = o_scene, .api_ae_get_exprinfo_attr = o_expr,
	.ae_get_ae_statis = o_aestat, .af_api_get_statis_info = o_afstat, .api_ae_get_weight = o_aew,
	.af_api_get_attr = o_afattr, .awb_api_get_awb_stable = o_awbstable, .api_ae_get_antiflicker_step = o_step,
	.api_ae_get_tgain = o_tgain, .ae_api_get_stable = o_aestable, .wdr_api_get_output_mode = o_wdr,
	.csccr_api_get = o_csccr, .bcsh_api_get_offset_rgb = o_rgb, .ae_get_ev_list = o_evl, .ae_get_at_list = o_atl,
	.ae_api_get_flicker_flag = o_flick, .gib_api_get_blc = o_blc, .gamma_api_get = o_gamma,
	.af_api_get_metric = o_metric, .ae_api_get_exp_list = o_expl, .ae_api_get_face_ae_tune_params = o_facep,
	.ae_api_get_gobal_roi_luma = o_roi, .tmo_api_get_curve = o_tmo,
};

/* the reference front ends */
#define R4(f) extern u64 f(u32, u32, u32, u32);
R4(tisp_g_module_control) R4(tisp_g_hv_flip) R4(tisp_g_bv) R4(tisp_g_ccm_attr) R4(tisp_g_Gamma) R4(tisp_g_antiflick)
R4(tisp_g_autozoom_control) R4(tisp_g_osd_attr) R4(tisp_g_draw_block_attr) R4(tisp_g_af_weight_attr)
R4(tisp_g_awb_weight_attr) R4(tisp_g_awb_ct_trend_offset) R4(tisp_g_awb_attr) R4(tisp_g_awb_statis_attr)
R4(tisp_g_awb_global_statis_attr) R4(tisp_g_ae_scence_attr) R4(tisp_g_ae_exprinfo_attr) R4(tisp_g_ae_statis_attr)
R4(tisp_g_af_statis_attr) R4(tisp_g_ae_weight_attr) R4(tisp_g_statis_config_attr) R4(tisp_g_af_metric_attr)
R4(tisp_g_awb_stable) R4(tisp_get_antiflicker_step) R4(tisp_get_wdr_output_mode) R4(tisp_get_frame_drop)
R4(tisp_g_face_ae_luma) R4(tisp_g_tmo_curve)
extern int tisp_get_fps(u32, uintptr_t), tisp_get_brightness(int, char *), tisp_get_csc_attr(u32, uintptr_t),
	   tisp_g_fcrop_control(u32, uintptr_t), tisp_g_osd_block_attr(u32, uintptr_t), tisp_get_ae_tgain(int, int *),
	   tisp_get_ae_stable(u32, uintptr_t), tisp_g_csccr_mode(u32, u32), tisp_g_ae_explist(int, int *),
	   tisp_g_coefft_wb(int, int), tisp_g_face_ae_weiget(char, int *), tisp_g_ev_list(u32, uintptr_t),
	   tisp_g_at_list(u32, uintptr_t), tisp_g_ae_flicker_flag(u32, uintptr_t), tisp_g_gib_blc(int, int);

#define NF 44
static const char *const names[NF] = {
	"g_module_control", "g_hv_flip", "get_fps", "g_bv", "get_brightness", "get_csc_attr", "g_ccm_attr", "g_Gamma",
	"g_antiflick", "g_autozoom_control", "g_fcrop_control", "g_osd_attr", "g_osd_block_attr", "g_draw_block_attr",
	"g_af_weight_attr", "g_awb_weight_attr", "g_awb_ct_trend_offset", "g_awb_attr", "g_awb_statis_attr",
	"g_awb_global_statis_attr", "g_ae_scence_attr", "g_ae_exprinfo_attr", "g_ae_statis_attr", "g_af_statis_attr",
	"g_ae_weight_attr", "g_statis_config_attr", "g_af_metric_attr", "g_awb_stable", "get_antiflicker_step",
	"get_ae_tgain", "get_ae_stable", "get_wdr_output_mode", "get_frame_drop", "g_csccr_mode", "g_ae_explist",
	"g_coefft_wb", "g_face_ae_weiget", "g_face_ae_luma", "g_ev_list", "g_at_list", "g_ae_flicker_flag",
	"g_tmo_curve", "g_gib_blc", "g_ae_statis_attr(NULL)"
};

static u32 vcall(u64 (*f)(u32, u32, u32, u32), u32 a0, u32 a1, u32 a2)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))f, a0, a1, a2, 0x5eed0003u, 0);
}

static u32 vendor(int k, u32 ch, u8 *b, u32 a2)
{
	u32 p = T32O_PTR(b);

	switch (k) {
	case 0: return vcall(tisp_g_module_control, ch, p, a2);
	case 1: return vcall(tisp_g_hv_flip, ch, p, a2);
	case 2: return (u32)tisp_get_fps(ch, p);
	case 3: return vcall(tisp_g_bv, ch, p, a2);
	case 4: return (u32)tisp_get_brightness((int)ch, (char *)b);
	case 5: return (u32)tisp_get_csc_attr(ch, p);
	case 6: return vcall(tisp_g_ccm_attr, ch, p, a2);
	case 7: return vcall(tisp_g_Gamma, ch, p, a2);
	case 8: return vcall(tisp_g_antiflick, ch, p, a2);
	case 9: return vcall(tisp_g_autozoom_control, ch, p, a2);
	case 10: return (u32)tisp_g_fcrop_control(ch, p);
	case 11: return vcall(tisp_g_osd_attr, ch, p, a2);
	case 12: return (u32)tisp_g_osd_block_attr(ch, p);
	case 13: return vcall(tisp_g_draw_block_attr, ch, p, a2);
	case 14: return vcall(tisp_g_af_weight_attr, ch, p, a2);
	case 15: return vcall(tisp_g_awb_weight_attr, ch, p, a2);
	case 16: return vcall(tisp_g_awb_ct_trend_offset, ch, p, a2);
	case 17: return vcall(tisp_g_awb_attr, ch, p, a2);
	case 18: return vcall(tisp_g_awb_statis_attr, ch, p, a2);
	case 19: return vcall(tisp_g_awb_global_statis_attr, ch, p, a2);
	case 20: return vcall(tisp_g_ae_scence_attr, ch, p, a2);
	case 21: return vcall(tisp_g_ae_exprinfo_attr, ch, p, a2);
	case 22: return vcall(tisp_g_ae_statis_attr, ch, p, a2);
	case 23: return vcall(tisp_g_af_statis_attr, ch, p, a2);
	case 24: return vcall(tisp_g_ae_weight_attr, ch, p, a2);
	case 25: return vcall(tisp_g_statis_config_attr, ch, p, a2);
	case 26: return vcall(tisp_g_af_metric_attr, ch, p, a2);
	case 27: return vcall(tisp_g_awb_stable, ch, p, a2);
	case 28: return vcall(tisp_get_antiflicker_step, ch, p, a2);
	case 29: return (u32)tisp_get_ae_tgain((int)ch, (int *)b);
	case 30: return (u32)tisp_get_ae_stable(ch, p);
	case 31: return vcall(tisp_get_wdr_output_mode, ch, p, a2);
	case 32: return vcall(tisp_get_frame_drop, ch, a2, p);	/* (ch, idx, out) */
	case 33: return (u32)tisp_g_csccr_mode(ch, p);
	case 34: return (u32)tisp_g_ae_explist((int)ch, (int *)b);
	case 35: return (u32)tisp_g_coefft_wb((int)ch, (int)p);
	case 36: return (u32)tisp_g_face_ae_weiget((char)ch, (int *)b);
	case 37: return vcall(tisp_g_face_ae_luma, ch, p, a2);
	case 38: return (u32)tisp_g_ev_list(ch, p);
	case 39: return (u32)tisp_g_at_list(ch, p);
	case 40: return (u32)tisp_g_ae_flicker_flag(ch, p);
	case 41: return vcall(tisp_g_tmo_curve, ch, p, a2);
	case 42: return (u32)tisp_g_gib_blc((int)ch, (int)p);
	default: return vcall(tisp_g_ae_statis_attr, ch, 0, a2);
	}
}

static u32 ours(int k, u32 ch, u8 *b, u32 a2, u8 cfg, const u8 *tattr)
{
	const struct t32_get_ops *o = &ops;

	switch (k) {
	case 0: return t32_g_module_control(o, cfg, ch, (u32 *)b);
	case 1: return (u32)t32_g_hv_flip(o, ch, b);
	case 2: return (u32)t32_get_fps(o, ch, b);
	case 3: return (u32)t32_g_bv(o, ch, (u32 *)b);
	case 4: return (u32)t32_get_brightness(o, ch, b);
	case 5: return (u32)t32_get_csc_attr(o, ch, b);
	case 6: return (u32)t32_g_ccm_attr(o, cfg, ch, b);
	case 7: return (u32)t32_g_gamma(o, ch, b);
	case 8: return (u32)t32_g_antiflick(o, ch, b);
	case 9: return (u32)t32_g_autozoom_control(o, ch, b);
	case 10: return (u32)t32_g_fcrop_control(o, ch, b);
	case 11: return (u32)t32_g_osd_attr(o, ch, b);
	case 12: return (u32)t32_g_osd_block_attr(o, ch, b);
	case 13: return (u32)t32_g_draw_block_attr(o, ch, b);
	case 14: return (u32)t32_g_af_weight_attr(o, ch, b);
	case 15: return (u32)t32_g_awb_weight_attr(o, ch, b);
	case 16: return (u32)t32_g_awb_ct_trend_offset(o, ch, b);
	case 17: return (u32)t32_g_awb_attr(o, ch, b, tattr);
	case 18: return (u32)t32_g_awb_statis_attr(o, ch, b);
	case 19: return (u32)t32_g_awb_global_statis_attr(o, ch, b);
	case 20: return (u32)t32_g_ae_scence_attr(o, ch, b);
	case 21: return (u32)t32_g_ae_exprinfo_attr(o, ch, b, tattr);
	case 22: return (u32)t32_g_ae_statis_attr(o, ch, b);
	case 23: return (u32)t32_g_af_statis_attr(o, ch, b);
	case 24: return (u32)t32_g_ae_weight_attr(o, ch, b);
	case 25: return (u32)t32_g_statis_config_attr(o, ch, b, tattr);
	case 26: return (u32)t32_g_af_metric_attr(o, ch, b);
	case 27: return (u32)t32_g_awb_stable(o, ch, b);
	case 28: return (u32)t32_get_antiflicker_step(o, ch, b);
	case 29: return (u32)t32_get_ae_tgain(o, ch, b);
	case 30: return (u32)t32_get_ae_stable(o, ch, b);
	case 31: return (u32)t32_get_wdr_output_mode(o, ch, (u32 *)b);
	case 32: return (u32)t32_get_frame_drop(o, cfg, ch, a2, b);
	case 33: return (u32)t32_g_csccr_mode(o, ch, b);
	case 34: return (u32)t32_g_ae_explist(o, ch, b);
	case 35: return (u32)t32_g_coefft_wb(o, ch, b);
	case 36: return (u32)t32_g_face_ae_weiget(o, ch, b);
	case 37: return (u32)t32_g_face_ae_luma(o, ch, b);
	case 38: return (u32)t32_g_ev_list(o, ch, b);
	case 39: return (u32)t32_g_at_list(o, ch, b);
	case 40: return (u32)t32_g_ae_flicker_flag(o, ch, b);
	case 41: return (u32)t32_g_tmo_curve(o, ch, b);
	case 42: return (u32)t32_g_gib_blc(o, ch, b);
	default: return (u32)t32_g_ae_statis_attr(o, ch, NULL);
	}
}

static u32 rs = 0x6a09e667u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int c, i, cmp = 0;
	static u8 ibuf[BUF], obuf[BUF], tin[TATTR];
	int k;

	for (c = 0; c < n; c++)
		for (k = 0; k < NF; k++) {
			u8 *vbuf, *tattr, cfg = (u8)(rnd() & 1);
			u32 ch = rnd(), a2 = rnd(), rv, ro;

			int tk = k == 17 || k == 21 || k == 25;	/* tattr-indexed: channels 0..7 and 0x100..0x107 (masking) */
			unsigned int tsz = tk ? TATTR : 256;

			if (tk || rnd() % 4 == 0)
				ch = (ch & 7) | (tk && rnd() % 2 ? 0x100 : 0);
			if (k == 32 && rnd() % 2)
				a2 &= 7;		/* frame-drop output index */
			if (cfg && rnd() % 4 == 0)
				cfg = (u8)(rnd() | 1);	/* any non-zero cfgMode */
			for (i = 0; i < BUF; i++)
				ibuf[i] = (u8)rnd();
			for (i = 0; i < tsz; i++)
				tin[i] = (u8)rnd();
			for (i = 0; i < MAXC; i++) {
				script[i] = rnd();
				if (rnd() % 3 == 0)
					script[i] = rnd() % 4 == 0 ? 49 + (rnd() & 1) : (rnd() & 0x2040);
				memcpy(sbytes[i], ibuf + (rnd() & 0x7f), 24);
			}
			t32o_reset();
			vbuf = t32o_alloc(BUF);
			tattr = t32o_alloc(tsz);
			memcpy(vbuf, ibuf, BUF);
			memcpy(obuf, ibuf, BUF);
			memcpy(tattr, tin, tsz);
			t32_data_f0904 = T32O_PTR(tattr);
			t32_data_d03a0[0] = cfg;

			memset(&sv, 0, sizeof(sv));
			sv.base = (uintptr_t)vbuf;
			cur = &sv;
			rv = vendor(k, ch, vbuf, a2);
			memset(&so, 0, sizeof(so));
			so.base = (uintptr_t)obuf;
			cur = &so;
			ro = ours(k, ch, obuf, a2, cfg, tin);
			if (rv != ro || sv.n != so.n || memcmp(sv.c, so.c, sizeof(sv.c)) || memcmp(vbuf, obuf, BUF) ||
			    memcmp(tattr, tin, tsz)) {
				printf("t32_geth_oracle: case %u %s (ch 0x%x cfg %u a2 0x%x): return vendor 0x%x ours 0x%x, calls %u/%u%s%s%s\n",
				       c, names[k], ch, cfg, a2, rv, ro, sv.n, so.n,
				       memcmp(sv.c, so.c, sizeof(sv.c)) ? ", call log differs" : "",
				       memcmp(vbuf, obuf, BUF) ? ", buffer differs" : "", memcmp(tattr, tin, tsz) ? ", tattr changed" : "");
				for (i = 0; i < sv.n || i < so.n; i++)
					printf("  call %u: vendor id %u (%x %x %x %x) ours id %u (%x %x %x %x)\n", i,
					       sv.c[i].id, sv.c[i].a[0], sv.c[i].a[1], sv.c[i].a[2], sv.c[i].a[3],
					       so.c[i].id, so.c[i].a[0], so.c[i].a[1], so.c[i].a[2], so.c[i].a[3]);
				return 1;
			}
			cmp++;
		}
	printf("t32_geth_oracle: %u calls compared (44 front ends: return, callee log, 256 B buffer, tuning block), 0 mismatches\n", cmp);
	return 0;
}
