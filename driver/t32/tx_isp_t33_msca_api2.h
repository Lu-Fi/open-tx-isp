/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) MSCA API helpers: channel start, channel enable readback and
 * the autozoom attribute copy (openimp #99, block msca-api2).
 *
 * lib_tisp_msca_channel_start splits a channel byte into (ch / 3, ch % 3)
 * and hands both to tisp_msca_t33_chx_cfg_load (ported in tx_isp_t32_hw.c).
 * tisp_msca_t33_get_msca_chx_en reads the system register 0xf0040 and tests
 * bit (ch & 0xff) (shift count mod 32, as the MIPS sllv).
 * tisp_msca_t33_api_get_autozoom reads the attribute block pointer
 * stMscaAttr[ch] and copies 3 entries (22 bytes each) of five u16 fields
 * into a 15-word array, field-major.
 */
#ifndef TX_ISP_T33_MSCA_API2_H
#define TX_ISP_T33_MSCA_API2_H

#include "tx_isp_t33_blk.h"

/* tisp_msca_t33_chx_cfg_load: ported in tx_isp_t32_hw.c */
extern int tisp_msca_t33_chx_cfg_load(u32 a, u32 b);
/* system_reg_read: shared T32 runtime */
extern u32 system_reg_read(u32 reg);

/* lib_tisp_msca_channel_start: (ch / 3, ch % 3) -> chx_cfg_load */
static inline int t33_msca_channel_start(u32 ch)
{
	u32 c = ch & 0xff;

	return tisp_msca_t33_chx_cfg_load(c / 3, c % 3);
}

/* tisp_msca_t33_get_msca_chx_en: bit (ch & 0xff) of system register 0xf0040 */
static inline int t33_msca_chx_en(u32 ch)
{
	u32 v = system_reg_read(0xf0040);

	return (v & (1U << (ch & 0x1f))) != 0;
}

/* tisp_msca_t33_api_get_autozoom: attr = stMscaAttr[ch & 0xff] (66 bytes) */
static inline void t33_msca_get_autozoom(const u8 *attr, u32 *out)
{
	unsigned int k;
	const u8 *a;

	for (k = 0; k < 3; k++) {
		a = attr + 22 * k;
		out[k] = t32_tp_le16(a);
		out[k + 3] = t32_tp_le16(a + 6);
		out[k + 6] = t32_tp_le16(a + 8);
		out[k + 9] = t32_tp_le16(a + 2);
		out[k + 12] = t32_tp_le16(a + 4);
	}
}

#endif /* TX_ISP_T33_MSCA_API2_H */
