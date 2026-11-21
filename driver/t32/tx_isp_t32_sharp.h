/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tisp_set_sharpness (vendor tx-isp-t32.ko 0xb0c44, callers
 * tx_isp_core_ops_s_ctrl and tisp_api_param_array_set; openimp #711).
 *
 * The vendor function, in order:
 *  - the level byte (low 8 bits of the control value);
 *  - tisp_dmsc_sharpness_set: dmsc_info[ch] +0x10 = level, tisp_dmsc_intp at
 *    the context gain (+8; the level is the tisp_ratio byte of five sharpness
 *    words), tisp_dmsc_noref_reg_cfg, tisp_dmsc_ref_reg_cfg, the 0x8010
 *    trigger (t32_dmsc_sharpness_set);
 *  - the tail of tisp_ysp_sharpness_set, inlined: ysp_ratio = level, the 32 x 11
 *    strength bytes at section +0x89d (step 11) rebuilt from ysp_paramsP, the
 *    copy of section 21 tisp_ysp_init made (level <= 0x80: p * level >> 7,
 *    above: p + ((255 - p) * (level - 128) >> 7)), tisp_ysp_intp at the context
 *    gain, tisp_ysp_noref_reg_cfg, tisp_ysp_ref_reg_cfg, the 0x11010 trigger,
 *    the 14 words 0xdce0.., the 0xdd18 byte and the 0xd014 trigger
 *    (t32_ysp_sharpness_set).
 * Direct register mode (cfgMode 0) as the rest of the T32 driver.  A block
 * without a context (section missing in the IQ bin) is skipped.
 *
 * Checked against the vendor instructions by tests/t33_binoracle t32-sharp
 * (make t32_binoracle_sharp, T32_OEM_KO).
 */
#ifndef TX_ISP_T32_SHARP_H
#define TX_ISP_T32_SHARP_H

#include "tx_isp_t32_dmsc.h"
#include "tx_isp_t32_ysp.h"

#define T32_SHARP_MAX_WRITES	(T32_DMSC_MAX_WRITES + T32_YSP_MAX_WRITES)

/* w: T32_SHARP_MAX_WRITES entries; returns the number of writes */
static inline int t32_sharpness_set(struct t32_dmsc_ctx *d, struct t32_ysp_ctx *y, u8 ch,
				    const u8 *ysp_dn, u32 *ysp_ratio, u32 level,
				    struct t32_front_write *w)
{
	int n = 0;

	level &= 0xff;
	if (d)
		n = t32_dmsc_sharpness_set(d, level, w);
	if (y && ysp_dn)
		n += t32_ysp_sharpness_set(y, ch, ysp_dn, ysp_ratio, level, w + n);
	return n;
}

#endif /* TX_ISP_T32_SHARP_H */
