/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T31_SHARPNESS_H
#define TX_ISP_T31_SHARPNESS_H

/*
 * IMP_ISP_Tuning_SetSharpness(0..255) on the T31: libimp 1.1.x passes the
 * byte unchanged as V4L2 control 0x98091b; the OEM driver (tisp_set_sharpness
 * -> tisp_dmsc_sharpness_set 0x263bc) rescales five demosaic strength curves
 * of the tuning bank (sp_d_w/sp_d_b/sp_ud_w/sp_ud_b, uu) word by word:
 *   sharp <= 128: src * sharp / 128 (128 = the tuned curve, 0 = off)
 *   sharp >  128: src + (max - src) * (sharp - 128) / 128, max 0x258 for the
 *                 sp_* curves and 0x320 for uu
 * All arithmetic is unsigned 32 bit as in the OEM.  255 therefore drives
 * every gain point to ~0x253 (uu ~0x31a) whatever the tuned value is: on a
 * bin whose high-gain points are 40 that is ~15x the tuned strength, so in
 * low light the demosaic sharpening amplifies the sensor noise.  This is the
 * stock behaviour, not a scaling error (tests/t31_sharpness_host_test.c).
 */
#define T31_DMSC_SHARP_SP_MAX 0x258u
#define T31_DMSC_SHARP_UU_MAX 0x320u
#define T31_DMSC_SHARP_NEUTRAL 0x80u

static inline u32 t31_dmsc_sharpness_scale(u32 sharp, u32 src, u32 max)
{
	sharp &= 0xffu;
	if (sharp > T31_DMSC_SHARP_NEUTRAL)
		return (((max - src) * (sharp - T31_DMSC_SHARP_NEUTRAL)) >> 7) + src;
	return (sharp * src) >> 7;
}

#endif
