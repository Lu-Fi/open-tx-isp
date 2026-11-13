/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tisp_s_* / tisp_set_* setters with their own logic: readable C of the
 * vendor's CSC, CCM, AWB, module ratio, WDR output mode, frame drop, FPS,
 * AE min/max and raw-rw setters.  Like tx_isp_t32_api_set.h the block
 * functions and the register access are reached through an ops table.
 *
 *  - The AE/AWB attribute base (*t32_data_f0904, "attr" below) is shared:
 *    AWB uses ch * 76 + 1872 / + 1924 and ch * 44 + 4004 (44 B), the module
 *    ratio cache is ch * 128 + 1060 (5 x { u32 mode, u8 value }, 8 B each).
 *  - Register addresses: with the register shadow on (t32_data_d03a0 != 0)
 *    the vendor hands the shadow index, else the bus address, as below.
 *
 * Vendor quirks kept (beyond-vendor changes need the user's OK):
 *  - tisp_s_ccm_attr builds an identity CCM { u8 sat, u8 hue, 2 pad bytes,
 *    3x3 Q16 } on its stack; the two pad bytes are uninitialised stack in
 *    the vendor, 0 here.
 *  - tisp_set_fps divides (fps >> 16) by (fps & 0xffff) without a zero test
 *    (MIPS div by 0 is undefined), and the sensor callback gets a 128 B
 *    stack block in which only +8 (channel) is set (the rest is
 *    uninitialised in the vendor, 0 here).
 *  - tisp_set_ae_minmax copies 164 B from +27 of the block into a stack
 *    buffer it never uses; the only effect is the word 164 at +4.
 *  - tisp_s_raw_rw_control_internal fills an 80 B stack descriptor from the
 *    channel state (t32_data_dc284 + ch * 904) and the user block and
 *    returns without using it: no effect, returns 0.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_apiattr_oracle).
 */
#ifndef TX_ISP_T32_API_ATTR_H
#define TX_ISP_T32_API_ATTR_H

#include "tx_isp_t32_tp.h"

struct t32_api_attr_ops {
	u32 (*reg_read)(u32 ch, u32 addr);
	void (*reg_write)(u32 ch, u32 addr, u32 val);
	void (*warn)(void);			/* the vendor's isp_printf error line */
	int (*csc_api_set)(u32 ch, const void *buf);
	int (*bcsh_refresh_by_csc)(u32 ch);
	int (*clm_refresh_by_csc)(u32 ch);
	int (*ccm_api_set)(u32 ch, const void *buf);
	int (*bcsh_api_set_ccm)(u32 ch, const void *ccm);
	int (*awb_set_mode)(u32 ch, const u32 *mode3);
	int (*awb_set_start)(u32 ch, const void *start);
	int (*awb_set_frz)(u32 ch, u32 frz);
	int (*awb_set_ct)(u32 ch, const void *ct);
	int (*sdns_ratio)(u32 ch, u32 ratio);
	int (*mdns_ratio)(u32 ch, u32 ratio);
	int (*tmo_api_strength_ctrl)(u32 ch, u32 ratio);
	int (*dpc_ratio)(u32 ch, u32 ratio);
	int (*defog_ratio)(u32 ch, u32 ratio);
	int (*wdr_api_set_output_mode)(u32 ch, u32 mode);
	int (*ae_api_set_fps)(u32 ch, u32 *fps);
	/* the channel's sensor fps entry (word +144 of its 232 B block t32_data_dd2d4 + ch * 232) */
	int (*sensor_set_fps)(u32 ch, u32 fps, u8 *info);
	int (*ae_clac_deflicker_cfg)(u32 ch);
};

static inline u32 t32_aa_rd32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline void t32_aa_wr32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* tisp_set_csc_attr */
static inline int t32_set_csc_attr(const struct t32_api_attr_ops *op, u32 ch, const void *buf)
{
	op->csc_api_set(ch & 0xff, buf);
	op->bcsh_refresh_by_csc(ch);
	op->clm_refresh_by_csc(ch);
	return 0;
}

#define T32_TOP_CCM_BYPASS	0x2000	/* top enable word (0x40 / shadow 2): CCM in the front path */
#define T32_TOP_BCSH_CCM	0x40	/* the BCSH CCM stage */

/* tisp_s_ccm_attr: buf = { u8 sat, u8 hue, .., 3x3 } */
static inline int t32_s_ccm_attr(const struct t32_api_attr_ops *op, u8 shadow, u32 ch, const u8 *buf)
{
	u32 top = op->reg_read(ch, shadow ? 2 : 0x40);
	u8 id[40] = { 0 };

	id[0] = buf[0];
	id[1] = buf[1];
	t32_aa_wr32(id + 4, 0x10000);
	t32_aa_wr32(id + 20, 0x10000);
	t32_aa_wr32(id + 36, 0x10000);
	if (top & T32_TOP_CCM_BYPASS) {
		op->ccm_api_set(ch, buf);
		return (top & T32_TOP_BCSH_CCM) ? -1 : 0;
	}
	if (top & T32_TOP_BCSH_CCM) {
		op->bcsh_api_set_ccm(ch, buf);
		return 0;
	}
	op->ccm_api_set(ch, buf);
	op->bcsh_api_set_ccm(ch, id);
	return -1;
}

/* tisp_s_awb_attr: buf = { u32 mode[3], u32 frz, ct at +16, 44 B at +20, u32 start_en +64, start +68 } */
static inline int t32_s_awb_attr(const struct t32_api_attr_ops *op, u8 *attr, u32 ch, const u8 *buf)
{
	u32 mode[3], i;
	u8 *a76 = attr + ch * 76, *a44 = attr + ch * 44 + 4004;

	mode[0] = t32_aa_rd32(buf);
	mode[1] = t32_aa_rd32(buf + 4);
	mode[2] = t32_aa_rd32(buf + 8);
	op->awb_set_mode(ch, mode);
	if (t32_aa_rd32(buf + 64) == 1)
		op->awb_set_start(ch, buf + 68);
	op->awb_set_frz(ch, buf[12]);
	op->awb_set_ct(ch, buf + 16);
	for (i = 0; i < 44; i++)
		a44[i] = buf[20 + i];
	t32_aa_wr32(a76 + 1924, t32_aa_rd32(buf + 64));
	t32_aa_wr32(a76 + 1872, t32_aa_rd32(buf + 12));
	return 0;
}

/*
 * tisp_s_module_ratio_attr: buf = 5 x { u32 mode, u8 ratio, 3 pad } for
 * SDNS, MDNS, TMO, DPC, defog; mode 0 = default ratio 128, 1 = manual.  A
 * module is set again only when its mode changes or (manual) its ratio does.
 */
static inline int t32_s_module_ratio_attr(const struct t32_api_attr_ops *op, u8 *attr, u32 ch, const u8 *buf)
{
	u8 *c = attr + (u32)(ch << 7) + 1060;
	unsigned int i;

	for (i = 0; i < 5; i++, c += 8, buf += 8) {
		u32 mode = t32_aa_rd32(buf);
		u32 ratio;

		if (t32_aa_rd32(c) == mode && (mode == 0 || (mode == 1 && c[4] == buf[4])))
			continue;
		t32_aa_wr32(c, mode);
		ratio = mode ? buf[4] : 128;
		c[4] = (u8)ratio;
		switch (i) {
		case 0: op->sdns_ratio(ch, ratio); break;
		case 1: op->mdns_ratio(ch, ratio); break;
		case 2: op->tmo_api_strength_ctrl(ch & 0xff, ratio); break;
		case 3: op->dpc_ratio(ch, ratio); break;
		default: op->defog_ratio(ch, ratio); break;
		}
	}
	return 0;
}

/* tisp_set_wdr_output_mode: modes 0..3, anything else is reported and taken as 3 */
static inline int t32_set_wdr_output_mode(const struct t32_api_attr_ops *op, u32 ch, const u8 *buf)
{
	u32 mode = t32_aa_rd32(buf);

	if (mode > 3) {
		op->warn();
		mode = 3;
	}
	op->wdr_api_set_output_mode(ch, mode);
	return 0;
}

/* tisp_set_frame_drop: cfg = { u32 enable, u8 lsize (< 32), .., u32 fmark at +8 } for output idx */
static inline int t32_set_frame_drop(const struct t32_api_attr_ops *op, u8 shadow, u32 ch, u32 idx,
				     const u8 *cfg)
{
	u32 r0 = shadow ? (idx << 5) + 48 : (idx << 8) + 0xf0120;
	u32 r1 = shadow ? (idx << 5) + 49 : (idx << 8) + 0xf0124;

	if (t32_aa_rd32(cfg) != 1) {
		op->reg_write(ch, r0, 0);
		op->reg_write(ch, r1, 0xffffffff);
		return 0;
	}
	if (cfg[4] >= 32) {
		op->warn();
		return -1;
	}
	op->reg_write(ch, r0, cfg[4]);
	op->reg_write(ch, r1, t32_aa_rd32(cfg + 8));
	return 0;
}

/* tisp_set_fps: fps = num << 16 | den; the AE may adjust it before the sensor gets num / den */
static inline int t32_set_fps(const struct t32_api_attr_ops *op, u32 ch, u32 fps)
{
	u8 info[128] = { 0 };
	int q;

	op->ae_api_set_fps(ch, &fps);
	q = (int)fps >> 16;
	q /= (int)(fps & 0xffff);
	t32_aa_wr32(info + 8, ch);
	if (op->sensor_set_fps(ch, (u32)q & 0xff, info) < 0) {
		op->warn();
		return -1;
	}
	op->ae_clac_deflicker_cfg(ch);
	return 0;
}

/* tisp_set_ae_minmax */
static inline int t32_set_ae_minmax(u8 *blk)
{
	t32_aa_wr32(blk + 4, 164);
	return 0;
}

/* tisp_s_raw_rw_control_internal: no effect on T32 (see above) */
static inline int t32_s_raw_rw_control_internal(u32 ch, const void *buf)
{
	(void)ch;
	(void)buf;
	return 0;
}

#endif
