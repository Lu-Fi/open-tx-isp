/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tuning API getters: readable C of the vendor's get_* / tisp_get_* /
 * tisp_g_* functions (69 small functions of the T32 tx-isp module).
 *
 * Most of them are the libimp-facing front of a block API: they mask the
 * channel to 8 bits (or not), call the block's own getter and return 0 or
 * that getter's result.  The block getters are reached through
 * struct t32_get_ops (the driver fills it with the ported block functions;
 * the oracle with recording stubs).  The others read module parameters,
 * the ISP init parameters (vendor stIspInitPar) or the tuning attribute
 * block (vendor tisp_tattr) directly.
 *
 * Every function follows the vendor binary (instruction order checked with
 * the disassembly of tx-isp-t32.ko); checked against Matt Davis' recovered C
 * (PR #15 @ 2e2e06bc) by tests/t32_oracle (t32_getp_oracle, t32_getd_oracle,
 * t32_geth_oracle).
 */
#ifndef TX_ISP_T32_API_GET_H
#define TX_ISP_T32_API_GET_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

/* ---- 1. module parameters (vendor .data +0x14 .. +0x3c and two .bss words) ---- */

/*
 * The vendor keeps its module parameters in .data: +0x14 isp_memopt,
 * +0x18 .. +0x28 the five clock rates (clkl, clkv, clka, clks, clk), +0x2c ..
 * +0x3c the five clock name pointers in the same order; isp_cache_line and
 * the stream-off mode (.bss +24) are .bss words.  Each getter is one load.
 */
struct t32_isp_modparams {
	u32 isp_memopt;
	u32 isp_clkl, isp_clkv, isp_clka, isp_clks, isp_clk;
	const char *clkl_name, *clkv_name, *clka_name, *clks_name, *clk_name;
	u32 isp_cache_line;
	u32 streamoff_mode;
};

/* get_clk_name .. get_clkl_name: lw v0, 60 / 56 / 52 / 48 / 44 (.data) */
static inline const char *t32_get_clk_name(const struct t32_isp_modparams *p)  { return p->clk_name; }
static inline const char *t32_get_clks_name(const struct t32_isp_modparams *p) { return p->clks_name; }
static inline const char *t32_get_clka_name(const struct t32_isp_modparams *p) { return p->clka_name; }
static inline const char *t32_get_clkv_name(const struct t32_isp_modparams *p) { return p->clkv_name; }
static inline const char *t32_get_clkl_name(const struct t32_isp_modparams *p) { return p->clkl_name; }
/* get_isp_clk .. get_isp_memopt: lw v0, 40 / 36 / 32 / 28 / 24 / 20 (.data) */
static inline u32 t32_get_isp_clk(const struct t32_isp_modparams *p)  { return p->isp_clk; }
static inline u32 t32_get_isp_clks(const struct t32_isp_modparams *p) { return p->isp_clks; }
static inline u32 t32_get_isp_clka(const struct t32_isp_modparams *p) { return p->isp_clka; }
static inline u32 t32_get_isp_clkv(const struct t32_isp_modparams *p) { return p->isp_clkv; }
static inline u32 t32_get_isp_clkl(const struct t32_isp_modparams *p) { return p->isp_clkl; }
static inline u32 t32_get_isp_memopt(const struct t32_isp_modparams *p) { return p->isp_memopt; }
/* get_isp_cache_line: lw v0, isp_cache_line; get_isp_streamoff_mode: lw v0, 24 (.bss) */
static inline u32 t32_get_isp_cache_line(const struct t32_isp_modparams *p) { return p->isp_cache_line; }
static inline u32 t32_get_isp_streamoff_mode(const struct t32_isp_modparams *p) { return p->streamoff_mode; }

/* ---- 2. state getters (init parameters, tuning attribute block, constants) ---- */

#define T32_INITPAR_WDR_EN	36	/* byte of stIspInitPar: WDR enabled */
#define T32_INITPAR_SENSOR_NUM	900	/* byte of stIspInitPar: number of sensors */
#define T32_RISCV_PARA_SZ	12800	/* per-channel RISC-V parameter block (stRiscvPara) */

/* tisp_get_ae_info: info word 1 = 164 (the AE info size), returns 0 */
static inline int t32_get_ae_info(u32 *info)
{
	info[1] = 164;
	return 0;
}

/* tisp_get_system_sensor_num: lbu stIspInitPar + 900 */
static inline u32 t32_get_system_sensor_num(const u8 *init_par)
{
	return init_par[T32_INITPAR_SENSOR_NUM];
}

/* tisp_get_msca_osd_info: the MSCA OSD state byte (.bss +0x98bc) */
static inline u32 t32_get_msca_osd_info(const u8 *osd_state)
{
	return *osd_state;
}

/* tisp_get_tuning: the tuning attribute block pointer (tisp_tattr) */
static inline void *t32_get_tuning(void *const *tattr)
{
	return *tattr;
}

/* tisp_g_wdr_en(out): *out = stIspInitPar byte 36, returns 0 */
static inline int t32_g_wdr_en(u32 *out, const u8 *init_par)
{
	*out = init_par[T32_INITPAR_WDR_EN];
	return 0;
}

/*
 * tisp_g_module_ratio_attr(ch, buf): five { u32 ratio, u8 enable } pairs of
 * the 128 byte channel record at tattr + 1060 to buf + 8k (the three pad
 * bytes of each pair stay), returns 0.  The channel is not masked.
 */
static inline int t32_g_module_ratio_attr(u32 ch, u8 *buf, const u8 *tattr)
{
	const u8 *r = tattr + (ch << 7) + 1060;
	unsigned int k;

	for (k = 0; k < 5; k++) {
		memcpy(buf + 8 * k, r + 8 * k, 4);
		buf[8 * k + 4] = r[8 * k + 4];
	}
	return 0;
}

/* tisp_g_raw_rw_handler and the sensor-setting stubs: no body, return 0 */
static inline int t32_g_raw_rw_handler(void) { return 0; }
static inline int t32_get_sensor_start_iq_index(void) { return 0; }
static inline int t32_get_sensor_setting(void) { return 0; }
static inline int t32_get_sensor_section_num(void) { return 0; }
static inline int t32_get_sensor_setting_len(void) { return 0; }
/*
 * get_sensor_setting_md5: the vendor body is "jr ra; nop", so its return
 * value is whatever v0 held (undefined); Matt's C and ours return 0.
 */
static inline int t32_get_sensor_setting_md5(void) { return 0; }

/*
 * tisp_g_riscv_info(ch, en, src): despite the name a store: the RISC-V
 * enable byte of the channel (riscvEnable[ch]) = (u8)en; for en == 1 the
 * 12800 byte parameter block src is copied to stRiscvPara + ch * 12800.
 * Returns 0.  The channel is not masked (vendor: ch >= 4 lands on the
 * neighbouring .bss words).
 */
static inline int t32_g_riscv_info(u32 ch, u32 en, const void *src, u8 *riscv_en, u8 *riscv_para)
{
	riscv_en[ch] = (u8)en;
	if ((u8)en == 1)
		memcpy(riscv_para + ch * T32_RISCV_PARA_SZ, src, T32_RISCV_PARA_SZ);
	return 0;
}

/* ---- 3. block getters (through struct t32_get_ops) ---- */

typedef int (*t32_get_fn)(u32 ch, void *buf);

struct t32_get_ops {
	/* register read of the tisp core (vendor tisp_reg_read(ch, reg)) */
	u32 (*reg_read)(u32 ch, u32 reg);
	/* 2-argument block getters (ch, buf) */
	t32_get_fn msca_get_mirr_flip, ae_api_get_fps, ae_get_bv, bcsh_api_get_brightness, csc_api_get,
		   ccm_api_get, bcsh_api_get_ccm, ae_api_get_antiflick, msca_api_get_autozoom,
		   msca_api_get_fcrop, msca_api_get_osd_out_attribute_cfg, msca_api_get_osd_out_cfg,
		   msca_api_get_line_out_cfg, af_api_get_weight, awb_get_weight,
		   awb_api_get_ct_trend_offset, awb_get_attr, awb_get_zone, awb_get_global_statis,
		   ae_api_get_scene_par, api_ae_get_exprinfo_attr, ae_get_ae_statis,
		   af_api_get_statis_info, api_ae_get_weight, af_api_get_attr, awb_api_get_awb_stable,
		   api_ae_get_antiflicker_step, api_ae_get_tgain, ae_api_get_stable,
		   wdr_api_get_output_mode, csccr_api_get, bcsh_api_get_offset_rgb, ae_get_ev_list,
		   ae_get_at_list, ae_api_get_flicker_flag, gib_api_get_blc;
	/* getters with more arguments */
	int (*gamma_api_get)(u32 ch, void *buf, u32 sel);
	int (*af_api_get_metric)(u32 ch, void *buf, void *p4, void *p8);
	int (*ae_api_get_exp_list)(u32 ch, void *buf, void *p4);
	int (*ae_api_get_face_ae_tune_params)(u32 ch, void *buf, void *p4, void *p8);
	int (*ae_api_get_gobal_roi_luma)(u32 ch, void *buf, void *p4);
	int (*tmo_api_get_curve)(u32 ch, u32 idx, void *curve);
};

/* the top enable register: index 2 in shadow mode (vendor cfgMode != 0), byte address 0x40 otherwise */
static inline u32 t32_get_top_reg(u8 cfg_mode)
{
	return cfg_mode ? 2 : 64;
}

/* tisp_g_module_control(ch, out): *out = the 22 module enable bits; returns them too */
static inline u32 t32_g_module_control(const struct t32_get_ops *o, u8 cfg_mode, u32 ch, u32 *out)
{
	u32 v = o->reg_read(ch, t32_get_top_reg(cfg_mode)) & 0x3fffff;

	*out = v;
	return v;
}

/*
 * tisp_g_ccm_attr(ch, buf): the CCM attribute from the CCM block, or from
 * the BCSH block when the top register has the BCSH CCM (bit 6) on and the
 * CCM block (bit 13) off.  Returns 0 only when exactly one of the two is on
 * (the CCM block on and the BCSH CCM off, or the other way round), -1 else
 * (after the CCM block getter ran).
 */
static inline int t32_g_ccm_attr(const struct t32_get_ops *o, u8 cfg_mode, u32 ch, void *buf)
{
	u32 top = o->reg_read(ch, t32_get_top_reg(cfg_mode));
	int ccm_on = (top & 0x2000) != 0, bcsh_ccm_on = (top & 0x40) != 0;

	if (!ccm_on && bcsh_ccm_on) {
		o->bcsh_api_get_ccm(ch, buf);
		return 0;
	}
	o->ccm_api_get(ch, buf);
	return (ccm_on && !bcsh_ccm_on) ? 0 : -1;
}

/*
 * tisp_get_frame_drop(ch, idx, out): the frame-drop setting of MSCA output
 * idx: out byte 4 = (u8)reg(skip), out word 2 = reg(mask), out word 0 = 1.
 * Registers: 0xf0120 / 0xf0124 + idx * 256 (direct), index idx * 32 + 48 / 49
 * in shadow mode.  Returns 0.
 */
static inline int t32_get_frame_drop(const struct t32_get_ops *o, u8 cfg_mode, u32 ch, u32 idx, u8 *out)
{
	u32 r_skip, r_mask, v;

	if (cfg_mode) {
		r_skip = (idx << 5) + 48;
		r_mask = (idx << 5) + 49;
	} else {
		r_skip = (idx << 8) + 0xf0120;
		r_mask = (idx << 8) + 0xf0124;
	}
	out[4] = (u8)o->reg_read(ch, r_skip);
	v = o->reg_read(ch, r_mask);
	memcpy(out + 8, &v, 4);
	v = 1;
	memcpy(out, &v, 4);
	return 0;
}

/* front ends that pass the full channel word and return 0 */
static inline int t32_g_hv_flip(const struct t32_get_ops *o, u32 ch, void *buf)          { o->msca_get_mirr_flip(ch, buf); return 0; }
static inline int t32_g_antiflick(const struct t32_get_ops *o, u32 ch, void *buf)        { o->ae_api_get_antiflick(ch, buf); return 0; }
static inline int t32_g_af_weight_attr(const struct t32_get_ops *o, u32 ch, void *buf)   { o->af_api_get_weight(ch, buf); return 0; }
static inline int t32_g_awb_weight_attr(const struct t32_get_ops *o, u32 ch, void *buf)  { o->awb_get_weight(ch, buf); return 0; }
static inline int t32_g_awb_statis_attr(const struct t32_get_ops *o, u32 ch, void *buf)  { o->awb_get_zone(ch, buf); return 0; }
static inline int t32_g_awb_global_statis_attr(const struct t32_get_ops *o, u32 ch, void *buf) { o->awb_get_global_statis(ch, buf); return 0; }
static inline int t32_g_ae_scence_attr(const struct t32_get_ops *o, u32 ch, void *buf)   { o->ae_api_get_scene_par(ch, buf); return 0; }
static inline int t32_g_af_statis_attr(const struct t32_get_ops *o, u32 ch, void *buf)   { o->af_api_get_statis_info(ch, buf); return 0; }
static inline int t32_g_ae_weight_attr(const struct t32_get_ops *o, u32 ch, void *buf)   { o->api_ae_get_weight(ch, buf); return 0; }
static inline int t32_get_antiflicker_step(const struct t32_get_ops *o, u32 ch, void *buf) { o->api_ae_get_antiflicker_step(ch, buf); return 0; }
/* tisp_g_Gamma: the API curve (selector 0) */
static inline int t32_g_gamma(const struct t32_get_ops *o, u32 ch, void *buf)            { o->gamma_api_get(ch, buf, 0); return 0; }

/* front ends that mask the channel to 8 bits and return 0 */
static inline int t32_g_autozoom_control(const struct t32_get_ops *o, u32 ch, void *buf) { o->msca_api_get_autozoom(ch & 0xff, buf); return 0; }
static inline int t32_g_osd_attr(const struct t32_get_ops *o, u32 ch, void *buf)         { o->msca_api_get_osd_out_attribute_cfg(ch & 0xff, buf); return 0; }
static inline int t32_g_draw_block_attr(const struct t32_get_ops *o, u32 ch, void *buf)  { o->msca_api_get_line_out_cfg(ch & 0xff, buf); return 0; }
static inline int t32_g_awb_ct_trend_offset(const struct t32_get_ops *o, u32 ch, void *buf) { o->awb_api_get_ct_trend_offset(ch & 0xff, buf); return 0; }
static inline int t32_g_awb_stable(const struct t32_get_ops *o, u32 ch, void *buf)       { o->awb_api_get_awb_stable(ch & 0xff, buf); return 0; }

/* tail calls: the block getter's result is returned; channel masked to 8 bits */
static inline int t32_get_fps(const struct t32_get_ops *o, u32 ch, void *buf)            { return o->ae_api_get_fps(ch & 0xff, buf); }
static inline int t32_get_csc_attr(const struct t32_get_ops *o, u32 ch, void *buf)       { return o->csc_api_get(ch & 0xff, buf); }
static inline int t32_g_fcrop_control(const struct t32_get_ops *o, u32 ch, void *buf)    { return o->msca_api_get_fcrop(ch & 0xff, buf); }
static inline int t32_g_osd_block_attr(const struct t32_get_ops *o, u32 ch, void *buf)   { return o->msca_api_get_osd_out_cfg(ch & 0xff, buf); }
static inline int t32_get_ae_stable(const struct t32_get_ops *o, u32 ch, void *buf)      { return o->ae_api_get_stable(ch & 0xff, buf); }
static inline int t32_g_csccr_mode(const struct t32_get_ops *o, u32 ch, void *buf)       { return o->csccr_api_get(ch & 0xff, buf); }
static inline int t32_g_ev_list(const struct t32_get_ops *o, u32 ch, void *buf)          { return o->ae_get_ev_list(ch & 0xff, buf); }
static inline int t32_g_at_list(const struct t32_get_ops *o, u32 ch, void *buf)          { return o->ae_get_at_list(ch & 0xff, buf); }
static inline int t32_g_ae_flicker_flag(const struct t32_get_ops *o, u32 ch, void *buf)  { return o->ae_api_get_flicker_flag(ch & 0xff, buf); }
/* tail calls with the full channel word */
static inline int t32_get_brightness(const struct t32_get_ops *o, u32 ch, void *buf)     { return o->bcsh_api_get_brightness(ch, buf); }
static inline int t32_get_ae_tgain(const struct t32_get_ops *o, u32 ch, void *buf)       { return o->api_ae_get_tgain(ch, buf); }
static inline int t32_g_coefft_wb(const struct t32_get_ops *o, u32 ch, void *buf)        { return o->bcsh_api_get_offset_rgb(ch, buf); }
static inline int t32_g_gib_blc(const struct t32_get_ops *o, u32 ch, void *buf)          { return o->gib_api_get_blc(ch, buf); }

/* tisp_g_ae_explist(ch, buf): tail call with the list words buf and buf + 4 */
static inline int t32_g_ae_explist(const struct t32_get_ops *o, u32 ch, u8 *buf)
{
	return o->ae_api_get_exp_list(ch, buf, buf + 4);
}

/* tisp_g_face_ae_weiget(ch, buf): tail call with buf, buf + 4, buf + 8; channel masked */
static inline int t32_g_face_ae_weiget(const struct t32_get_ops *o, u32 ch, u8 *buf)
{
	return o->ae_api_get_face_ae_tune_params(ch & 0xff, buf, buf + 4, buf + 8);
}

/* tisp_g_face_ae_luma(ch, buf): tail call with buf, buf + 4; channel masked */
static inline int t32_g_face_ae_luma(const struct t32_get_ops *o, u32 ch, u8 *buf)
{
	return o->ae_api_get_gobal_roi_luma(ch & 0xff, buf, buf + 4);
}

/* tisp_g_af_metric_attr(ch, buf): getter with buf, buf + 4, buf + 8, returns 0 */
static inline int t32_g_af_metric_attr(const struct t32_get_ops *o, u32 ch, u8 *buf)
{
	o->af_api_get_metric(ch, buf, buf + 4, buf + 8);
	return 0;
}

/* tisp_g_tmo_curve(ch, buf): curve number buf[1], curve at buf + 2; returns 0 */
static inline int t32_g_tmo_curve(const struct t32_get_ops *o, u32 ch, u8 *buf)
{
	o->tmo_api_get_curve(ch & 0xff, buf[1], buf + 2);
	return 0;
}

/* tisp_g_bv(ch, out): *out = the brightness value of the AE block, which is also returned */
static inline int t32_g_bv(const struct t32_get_ops *o, u32 ch, u32 *out)
{
	int bv = o->ae_get_bv(ch & 0xff, out);

	*out = (u32)bv;
	return bv;
}

/* tisp_g_ae_statis_attr(ch, buf): nothing for a NULL buf; returns 0 */
static inline int t32_g_ae_statis_attr(const struct t32_get_ops *o, u32 ch, void *buf)
{
	if (buf)
		o->ae_get_ae_statis(ch, buf);
	return 0;
}

/* tisp_get_wdr_output_mode(ch, out): the WDR block reports into a local word, copied to *out; returns 0 */
static inline int t32_get_wdr_output_mode(const struct t32_get_ops *o, u32 ch, u32 *out)
{
	u32 mode;

	o->wdr_api_get_output_mode(ch, &mode);
	*out = mode;
	return 0;
}

/*
 * tisp_g_awb_attr(ch, buf): AWB block attribute, then from the tuning block:
 * 44 bytes at tattr + ch * 44 + 4004 to buf + 20, word tattr + ch * 76 + 1924
 * to buf + 64 and word tattr + ch * 76 + 1872 to buf + 12.  Returns 0.
 */
static inline int t32_g_awb_attr(const struct t32_get_ops *o, u32 ch, u8 *buf, const u8 *tattr)
{
	const u8 *a = tattr + ch * 76;

	o->awb_get_attr(ch, buf);
	memcpy(buf + 20, tattr + ch * 44 + 4004, 44);
	memcpy(buf + 64, a + 1924, 4);
	memcpy(buf + 12, a + 1872, 4);
	return 0;
}

/*
 * tisp_g_ae_exprinfo_attr(ch, buf): buf word 0 = tattr word (ch * 220 + 20),
 * then the AE exposure info getter; returns 0 when its result (low byte) is
 * below 50, -512 otherwise.
 */
static inline int t32_g_ae_exprinfo_attr(const struct t32_get_ops *o, u32 ch, u8 *buf, const u8 *tattr)
{
	memcpy(buf, tattr + ch * 220 + 20, 4);
	return ((u8)o->api_ae_get_exprinfo_attr(ch, buf) < 50) ? 0 : -512;
}

/*
 * tisp_g_statis_config_attr(ch, buf): 72 bytes of the tuning block (tattr +
 * ch * 72 + 1572) to buf, then the AF window attribute of the AF block into a
 * local 24 byte record, of which 11 fields go to buf (vendor widths: u8
 * fields at 8, 18, 19 widen to words).  Returns 0.
 */
static inline int t32_g_statis_config_attr(const struct t32_get_ops *o, u32 ch, u8 *buf, const u8 *tattr)
{
	u8 af[24];
	u32 w;

	memcpy(buf, tattr + ch * 72 + 1572, 72);
	o->af_api_get_attr(ch, af);
	w = af[8];
	memcpy(buf + 44, &w, 4);
	buf[60] = af[9];
	memcpy(buf + 62, af + 10, 2);
	memcpy(buf + 64, af + 12, 2);
	memcpy(buf + 66, af + 14, 2);
	memcpy(buf + 68, af + 16, 2);
	w = af[18];
	memcpy(buf + 48, &w, 4);
	w = af[19];
	memcpy(buf + 52, &w, 4);
	buf[56] = af[20];
	buf[57] = af[21];
	memcpy(buf + 70, af + 22, 2);
	return 0;
}

#endif
