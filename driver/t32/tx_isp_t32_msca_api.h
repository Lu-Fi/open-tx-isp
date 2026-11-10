/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA API accessors: readable C of the vendor's tisp_msca_api_get_* /
 * set_* functions that move user configuration blocks to and from the
 * per-channel MSCA parameter block (t32_data_e2768[channel], 2636 bytes).
 *
 * Layout of that block as these functions use it:
 *   set s (0..2) at s * 872:  +20 mask/fcrop enable (mask dir d at +(d+2)*16 + 20),
 *                              +48 mirror/flip bits, +884 pending mask-set bits
 *   +116 + 28 * s line config (+676 line on/off), +680 + 20 * s osd config,
 *   +840 / +844.. osd attribute block
 * The functions take the resolved block (sb) and the user buffer (buf).
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscae_oracle).
 */
#ifndef TX_ISP_T32_MSCA_API_H
#define TX_ISP_T32_MSCA_API_H

#include "tx_isp_t32_tp.h"

#define T32_MSCA_SET_SZ		872
#define T32_MSCA_BLK_SZ		2636

static inline u16 t32_msa_r16(const u8 *p)
{
	return (u16)(p[0] | p[1] << 8);
}

static inline u32 t32_msa_r32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline void t32_msa_w16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static inline void t32_msa_w32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* tisp_msca_api_get_par: the whole block */
static inline void t32_msca_api_get_par(const u8 *sb, u8 *buf)
{
	unsigned int i;

	for (i = 0; i < T32_MSCA_BLK_SZ; i++)
		buf[i] = sb[i];
}

/* tisp_msca_api_get_mask_out_cfg */
static inline void t32_msca_api_get_mask_out_cfg(const u8 *sb, u8 *buf)
{
	const u8 *m = sb + buf[0] * T32_MSCA_SET_SZ + (buf[1] + 2) * 16;

	if (m[20] != 1) {
		buf[2] = 0;
		t32_msa_w16(buf + 4, 0);
		t32_msa_w16(buf + 6, 0);
		t32_msa_w16(buf + 8, 0);
		t32_msa_w16(buf + 10, 0);
		buf[19] = 0;
		buf[20] = 0;
		buf[21] = 0;
		return;
	}
	buf[2] = m[20];
	t32_msa_w16(buf + 4, t32_msa_r16(m + 24));
	t32_msa_w16(buf + 6, t32_msa_r16(m + 22));
	t32_msa_w16(buf + 8, t32_msa_r16(m + 26));
	t32_msa_w16(buf + 10, t32_msa_r16(m + 28));
	buf[19] = m[34];
	buf[20] = m[33];
	buf[21] = m[32];
}

/* tisp_msca_api_set_mask_out_cfg: enable (buf[2] == 1) stores the block, anything else disables it; either way the direction is marked pending */
static inline void t32_msca_api_set_mask_out_cfg(u8 *sb, const u8 *buf)
{
	u8 *s = sb + buf[0] * T32_MSCA_SET_SZ;
	u8 *m = s + (buf[1] + 2) * 16;

	if (buf[2] == 1) {
		m[20] = buf[2];
		t32_msa_w16(m + 24, t32_msa_r16(buf + 4));
		t32_msa_w16(m + 22, t32_msa_r16(buf + 6));
		t32_msa_w16(m + 26, t32_msa_r16(buf + 8));
		t32_msa_w16(m + 28, t32_msa_r16(buf + 10));
		t32_msa_w32(m + 32, (u32)buf[19] << 16 | (u32)buf[20] << 8 | buf[21]);
	} else {
		m[20] = 0;
	}
	t32_msa_w32(s + 884, t32_msa_r32(s + 884) | (1u << (buf[1] & 31)));
}

/* tisp_msca_api_get_line_out_cfg */
static inline void t32_msca_api_get_line_out_cfg(const u8 *sb, u8 *buf)
{
	const u8 *l = sb + buf[0] * 28;
	u32 mode = l[116];

	t32_msa_w32(buf + 4, mode);
	buf[1] = sb[676];
	if (mode == 2 || mode == 1 || mode == 0) {
		buf[12] = l[120];
		t32_msa_w16(buf + 14, t32_msa_r16(l + 122));
		t32_msa_w16(buf + 16, t32_msa_r16(l + 124));
		t32_msa_w16(buf + 18, t32_msa_r16(l + 126));
		t32_msa_w16(buf + 20, t32_msa_r16(l + 128));
		buf[28] = l[136];
		buf[29] = l[137];
	}
	if (mode == 1) {
		t32_msa_w16(buf + 30, t32_msa_r16(l + 138));
		buf[32] = l[140];
	} else if (mode == 0) {
		buf[30] = l[138];
	}
	if (mode <= 2) {
		buf[25] = l[134];
		buf[26] = l[133];
		buf[27] = l[132];
	}
}

/* tisp_msca_api_get_osd_out_cfg */
static inline void t32_msca_api_get_osd_out_cfg(const u8 *sb, u8 *buf)
{
	const u8 *o = sb + buf[0] * 20;

	if (o[680] != 1) {
		buf[1] = 0;
		t32_msa_w16(buf + 2, 0);
		t32_msa_w16(buf + 4, 0);
		t32_msa_w16(buf + 6, 0);
		t32_msa_w16(buf + 8, 0);
		t32_msa_w32(buf + 12, 0);
		t32_msa_w16(buf + 16, 0);
		return;
	}
	buf[1] = o[680];
	t32_msa_w16(buf + 2, t32_msa_r16(o + 682));
	t32_msa_w16(buf + 4, t32_msa_r16(o + 684));
	t32_msa_w16(buf + 6, t32_msa_r16(o + 686));
	t32_msa_w16(buf + 8, t32_msa_r16(o + 688));
	t32_msa_w32(buf + 12, t32_msa_r32(o + 692) + 0x80000000u);
	t32_msa_w16(buf + 16, t32_msa_r16(o + 696));
}

/* tisp_msca_api_get_osd_out_attribute_cfg: set buf[29] */
static inline void t32_msca_api_get_osd_out_attr_cfg(const u8 *sb, u8 *buf)
{
	unsigned int i = buf[29], k;

	t32_msa_w32(buf + 0, sb[i + 844]);
	buf[28] = sb[840];
	t32_msa_w32(buf + 4, sb[i + 842]);
	t32_msa_w32(buf + 8, sb[i + 846]);
	for (k = 0; k < 16; k++)
		buf[12 + k] = sb[i * 16 + 848 + k];
}

/* tisp_msca_api_get_fcrop */
static inline void t32_msca_api_get_fcrop(const u8 *sb, u8 *buf)
{
	buf[0] = t32_msa_r16(sb + 20) > 0;
	t32_msa_w32(buf + 8, t32_msa_r16(sb + 22));
	t32_msa_w32(buf + 4, t32_msa_r16(sb + 24));
	t32_msa_w32(buf + 12, t32_msa_r16(sb + 26));
	t32_msa_w32(buf + 16, t32_msa_r16(sb + 28));
}

/* tisp_msca_api_get_autozoom: the five crop words of sets 0..2 */
static inline void t32_msca_api_get_autozoom(const u8 *sb, u8 *buf)
{
	unsigned int k;

	for (k = 0; k < 3; k++) {
		const u8 *s = sb + k * T32_MSCA_SET_SZ + 20;

		t32_msa_w32(buf + 4 * k, t32_msa_r16(s));
		t32_msa_w32(buf + 4 * k + 12, t32_msa_r16(s + 2));
		t32_msa_w32(buf + 4 * k + 24, t32_msa_r16(s + 4));
		t32_msa_w32(buf + 4 * k + 36, t32_msa_r16(s + 6));
		t32_msa_w32(buf + 4 * k + 48, t32_msa_r16(s + 8));
	}
}

/* tisp_msca_get_mirr_flip: mirror/flip bits of set (buf[0] & 3) into buf[0] bits 7:2 */
static inline void t32_msca_get_mirr_flip(const u8 *sb, u8 *buf)
{
	u32 w = t32_msa_r32(sb + (buf[0] & 3) * T32_MSCA_SET_SZ + 48);

	buf[0] = (buf[0] & ~0xfcu) | (u8)((w & 0x3f) << 2);
}

/* tisp_msca_set_mirr_flip: the inverse; also raises the channel's refresh flag (e275c[ch]) */
static inline void t32_msca_set_mirr_flip(u8 *sb, const u8 *buf, u8 *refresh)
{
	u8 *p = sb + (buf[0] & 3) * T32_MSCA_SET_SZ + 48;

	*p = (*p & ~0x3fu) | (u8)((buf[0] >> 2) & 0x3f);
	*refresh = 1;
}

/* tisp_s_mscaler_mask_block_attr: marks the block (f3258[ch * 32 + 14]) and sets the mask */
static inline void t32_s_mscaler_mask_block_attr(u8 *sb, const u8 *buf, u8 *marked)
{
	*marked = 1;
	t32_msca_api_set_mask_out_cfg(sb, buf);
}

#endif /* TX_ISP_T32_MSCA_API_H */
