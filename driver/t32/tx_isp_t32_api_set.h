/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tisp_s_* / tisp_set_* setter entry points: readable C of the vendor's
 * thin API setters.  Each one forwards a user request to the block API
 * function that does the work (BCSH, MSCA, AE, AWB, AF, GIB, gamma, TMO,
 * CSC-CR, sharpness); the block function is reached through the ops table
 * below, so a driver binds the table to the block ports and the host oracle
 * binds it to recording stubs.
 *
 * Argument shapes follow the vendor calls (o32: channel in a0, user buffer
 * pointer in a1).  Where the vendor masks the channel to a byte (andi 0xff)
 * we mask it, where it passes the full word we pass it.  Notes:
 *  - tisp_set_brightness/contrast/saturation/hue store the value byte in
 *    their own stack frame and pass its address: the callee sees a pointer
 *    to one byte.
 *  - tisp_s_csccr_mode / tisp_s_osd_block_attr forward only the channel byte.
 *  - tisp_s_fcrop_control sets byte 19 of the channel's 32 B mask attribute
 *    block (t32_data_f3258 + ch * 32, the full channel word is scaled) before
 *    the tail call; tisp_s_ae_exprinfo_attr copies the first user word into
 *    +20 of the channel's 220 B AE attribute block (*t32_data_f0904 + ch * 220).
 *  - tisp_s_awb_ct returns the CT byte, tisp_set_sharpness the YSP result,
 *    most others 0 or the callee's result as listed per function.
 *  - tisp_s_raw_rw_handler, tisp_s_mdns_riscvEn and
 *    tisp_set_scaler_level_control are empty (return 0) on T32.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_apiset_oracle).
 */
#ifndef TX_ISP_T32_API_SET_H
#define TX_ISP_T32_API_SET_H

#include "tx_isp_t32_tp.h"

struct t32_api_set_ops {
	int (*gib_api_set_blc)(u32 ch, u32 arg);
	int (*bcsh_api_set_offset_rgb)(u32 ch, u32 arg);
	int (*bcsh_api_set_brightness)(u32 ch, const u8 *v);
	int (*bcsh_api_set_contrast)(u32 ch, const u8 *v);
	int (*bcsh_api_set_saturation)(u32 ch, const u8 *v);
	int (*bcsh_api_set_hue)(u32 ch, const u8 *v);
	int (*ae_api_set_face_ae_tune_params)(u32 ch, const void *p0, const void *p1, const void *p2);
	int (*csccr_api_set)(u32 ch);
	int (*msca_api_set_osd_out_cfg)(u32 ch);
	int (*ae_set_at_list)(u32 ch, u32 n, const void *list);
	int (*ae_set_ev_list)(u32 ch, u32 n, const void *list);
	int (*ae_api_set_exp_list)(u32 ch, u32 n, const void *list);
	int (*ae_set_weight)(u32 ch, const void *buf);
	int (*gamma_api_set)(u32 ch, const void *buf, u32 flag);
	int (*awb_set_weight)(u32 ch, const void *buf);
	int (*msca_set_mirr_flip)(u32 ch, const void *buf);
	int (*af_api_set_weight)(u32 ch, const void *buf);
	int (*ae_api_set_antiflick)(u32 ch, const void *buf);
	int (*ae_api_set_scene_par)(u32 ch, const void *buf);
	int (*msca_api_set_autozoom)(u32 ch, const void *buf);
	int (*msca_api_set_line_out_cfg)(u32 ch, const void *buf);
	int (*awb_api_set_ct_trend_offset)(u32 ch, const void *buf);
	int (*msca_api_set_osd_out_attribute_cfg)(u32 ch, const void *buf);
	int (*tmo_api_set_curve)(u32 ch, u32 mode, const void *curve);
	int (*awb_api_set_ct)(u32 ch, u32 ct);
	int (*dmsc_sharpness_set)(u32 ch, u32 level);
	int (*ysp_sharpness_set)(u32 ch, u32 level);
	int (*msca_api_set_fcrop)(u32 ch, u32 w1, u32 w2, u32 w3);
	int (*ae_set_exprinfo_attr)(u32 ch, const void *buf);
	int (*tmo_faceae_refresh)(u32 ch, u32 en, const void *p0, const void *p1,
				  const void *p2, const void *p3, const void *p4);
	int (*set_scaler_level_control_set)(u32 ch, const void *buf);
};

static inline u32 t32_as_le32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* tisp_s_gib_blc */
static inline int t32_s_gib_blc(const struct t32_api_set_ops *op, u32 ch, u32 arg)
{
	return op->gib_api_set_blc(ch, arg);
}

/* tisp_s_coefft_wb */
static inline int t32_s_coefft_wb(const struct t32_api_set_ops *op, u32 ch, u32 arg)
{
	return op->bcsh_api_set_offset_rgb(ch, arg);
}

/* tisp_set_brightness: the value byte goes by address */
static inline int t32_set_brightness(const struct t32_api_set_ops *op, u32 ch, u32 val)
{
	u8 v = (u8)val;

	return op->bcsh_api_set_brightness(ch, &v);
}

/* tisp_set_contrast */
static inline int t32_set_contrast(const struct t32_api_set_ops *op, u32 ch, u32 val)
{
	u8 v = (u8)val;

	return op->bcsh_api_set_contrast(ch, &v);
}

/* tisp_set_saturation */
static inline int t32_set_saturation(const struct t32_api_set_ops *op, u32 ch, u32 val)
{
	u8 v = (u8)val;

	return op->bcsh_api_set_saturation(ch, &v);
}

/* tisp_set_hue */
static inline int t32_set_hue(const struct t32_api_set_ops *op, u32 ch, u32 val)
{
	u8 v = (u8)val;

	return op->bcsh_api_set_hue(ch, &v);
}

/* tisp_s_face_ae_weiget: three consecutive words of the user block */
static inline int t32_s_face_ae_weiget(const struct t32_api_set_ops *op, u32 ch, const u32 *p)
{
	return op->ae_api_set_face_ae_tune_params(ch & 0xff, p, p + 1, p + 2);
}

/* tisp_s_raw_rw_handler: empty on T32 */
static inline int t32_s_raw_rw_handler(void)
{
	return 0;
}

/* tisp_s_mdns_riscvEn: empty on T32 */
static inline int t32_s_mdns_riscv_en(u32 ch)
{
	(void)ch;
	return 0;
}

/* tisp_set_scaler_level_control: empty on T32 */
static inline int t32_set_scaler_level_control(void)
{
	return 0;
}

/* tisp_s_csccr_mode */
static inline int t32_s_csccr_mode(const struct t32_api_set_ops *op, u32 ch)
{
	return op->csccr_api_set(ch & 0xff);
}

/* tisp_s_osd_block_attr */
static inline int t32_s_osd_block_attr(const struct t32_api_set_ops *op, u32 ch)
{
	return op->msca_api_set_osd_out_cfg(ch & 0xff);
}

/* tisp_s_at_list: { u8 count, pad, list at +8 } */
static inline int t32_s_at_list(const struct t32_api_set_ops *op, u32 ch, const u8 *buf)
{
	return op->ae_set_at_list(ch & 0xff, buf[0], buf + 8);
}

/* tisp_s_ev_list: same layout as the AT list */
static inline int t32_s_ev_list(const struct t32_api_set_ops *op, u32 ch, const u8 *buf)
{
	return op->ae_set_ev_list(ch & 0xff, buf[0], buf + 8);
}

/* tisp_s_ae_explist: { u32 count, list at +4 }, full channel word */
static inline int t32_s_ae_explist(const struct t32_api_set_ops *op, u32 ch, const u8 *buf)
{
	return op->ae_api_set_exp_list(ch, t32_as_le32(buf), buf + 4);
}

/* tisp_s_ae_weight_attr */
static inline int t32_s_ae_weight_attr(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->ae_set_weight(ch, buf);
	return 0;
}

/* tisp_s_Gamma */
static inline int t32_s_gamma(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->gamma_api_set(ch, buf, 0);
	return 0;
}

/* tisp_s_awb_weight_attr */
static inline int t32_s_awb_weight_attr(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->awb_set_weight(ch, buf);
	return 0;
}

/* tisp_s_hv_flip */
static inline int t32_s_hv_flip(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->msca_set_mirr_flip(ch, buf);
	return 0;
}

/* tisp_s_af_weight_attr */
static inline int t32_s_af_weight_attr(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->af_api_set_weight(ch, buf);
	return 0;
}

/* tisp_s_antiflick */
static inline int t32_s_antiflick(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->ae_api_set_antiflick(ch, buf);
	return 0;
}

/* tisp_s_ae_scence_attr */
static inline int t32_s_ae_scence_attr(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->ae_api_set_scene_par(ch, buf);
	return 0;
}

/* tisp_s_autozoom_control */
static inline int t32_s_autozoom_control(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->msca_api_set_autozoom(ch & 0xff, buf);
	return 0;
}

/* tisp_s_draw_block_attr */
static inline int t32_s_draw_block_attr(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->msca_api_set_line_out_cfg(ch & 0xff, buf);
	return 0;
}

/* tisp_s_awb_ct_trend_offset */
static inline int t32_s_awb_ct_trend_offset(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->awb_api_set_ct_trend_offset(ch & 0xff, buf);
	return 0;
}

/* tisp_s_osd_attr */
static inline int t32_s_osd_attr(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	op->msca_api_set_osd_out_attribute_cfg(ch & 0xff, buf);
	return 0;
}

/* tisp_s_tmo_curve: { u8 ?, u8 mode, curve at +2 } */
static inline int t32_s_tmo_curve(const struct t32_api_set_ops *op, u32 ch, const u8 *buf)
{
	op->tmo_api_set_curve(ch & 0xff, buf[1], buf + 2);
	return 0;
}

/* tisp_s_awb_ct: returns the CT byte */
static inline int t32_s_awb_ct(const struct t32_api_set_ops *op, u32 ch, u32 ct)
{
	op->awb_api_set_ct(ch & 0xff, ct & 0xff);
	return ct & 0xff;
}

/* tisp_set_sharpness: DMSC first, then YSP (its result is returned) */
static inline int t32_set_sharpness(const struct t32_api_set_ops *op, u32 ch, u32 level)
{
	op->dmsc_sharpness_set(ch, level & 0xff);
	return op->ysp_sharpness_set(ch, level & 0xff);
}

/* tisp_s_fcrop_control: mark the channel's mask attribute block, then set the crop */
static inline int t32_s_fcrop_control(const struct t32_api_set_ops *op, u8 *f3258, u32 ch,
				      u32 w1, u32 w2, u32 w3)
{
	f3258[(u32)(ch << 5) + 19] = 1;
	return op->msca_api_set_fcrop(ch & 0xff, w1, w2, w3);
}

/* tisp_s_ae_exprinfo_attr: ae_attr = *t32_data_f0904 (220 B per channel) */
static inline int t32_s_ae_exprinfo_attr(const struct t32_api_set_ops *op, u8 *ae_attr, u32 ch,
					 const u8 *buf)
{
	u8 *a = ae_attr + ch * 220;
	u32 v = t32_as_le32(buf);

	a[20] = (u8)v;
	a[21] = (u8)(v >> 8);
	a[22] = (u8)(v >> 16);
	a[23] = (u8)(v >> 24);
	op->ae_set_exprinfo_attr(ch, buf);
	return 0;
}

/* tisp_s_tmo_faceae: { u8 enable, pad, 5 x 12 B face windows at +2 .. +50 } */
static inline int t32_s_tmo_faceae(const struct t32_api_set_ops *op, u32 ch, const u8 *buf)
{
	op->tmo_faceae_refresh(ch, buf[0], buf + 2, buf + 14, buf + 26, buf + 38, buf + 50);
	return 0;
}

/* tisp_set_scaler_level: tail call */
static inline int t32_set_scaler_level(const struct t32_api_set_ops *op, u32 ch, const void *buf)
{
	return op->set_scaler_level_control_set(ch, buf);
}

#endif
