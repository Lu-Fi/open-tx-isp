/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA API getters/setters (vendor tisp_msca_t33_api_*).  The per-channel
 * attribute block (vendor stMscaAttr[chn], a pointer) is passed as a byte pointer.
 */
#ifndef TX_ISP_T33_MSCA_API4_H
#define TX_ISP_T33_MSCA_API4_H

#include "tx_isp_t33_blk.h"

static inline void t33_msca_put_le32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/*
 * tisp_msca_t33_api_get_mirr_flip: p[0] bits 0..1 select the scaler block
 * (1984 bytes each); bits 2..7 become bits 0..5 of its word at +80.
 */
static inline void t33_msca_get_mirr_flip(const u8 *attr, u8 *p)
{
	u8 sel = p[0] & 3;
	u32 w = t33_le32(attr + sel * 1984 + 80);
	u32 bits = w & 0x3f;

	p[0] = (u8)(sel | bits << 2);
}

/*
 * tisp_msca_t33_api_get_osd_out_attribute_cfg: three u32 from bytes 876 / 874 / 878,
 * 16 bytes 880..895 at +12, byte 872 at +28.
 */
static inline void t33_msca_get_osd_out(const u8 *attr, u8 *out)
{
	unsigned int i;

	t33_msca_put_le32(out, attr[876]);
	out[28] = attr[872];
	t33_msca_put_le32(out + 4, attr[874]);
	t33_msca_put_le32(out + 8, attr[878]);
	for (i = 0; i < 16; i++)
		out[12 + i] = attr[880 + i];
}

/* tisp_msca_t33_api_set_osd_out_attribute_cfg: the inverse of the getter (low bytes of the words) */
static inline void t33_msca_set_osd_out(u8 *attr, const u8 *in)
{
	unsigned int k;

	attr[876] = in[0];
	attr[872] = in[28];
	attr[874] = in[4];
	attr[878] = in[8];
	for (k = 0; k < 16; k++)
		attr[880 + k] = in[12 + k];
}

/* tisp_msca_t33_api_set_msca_dma_stride: init flag 1 is the error path (printk, nothing written) */
static inline int t33_msca_stride_refused(u32 init_flag)
{
	return init_flag == 1;
}

static inline u32 t33_msca_stride_value(u32 a, u32 b)
{
	return (b + 15 + a) & ~15U;
}

/* register slot of the first (which 0) / second (which 1) stride write of channel ch */
static inline u32 t33_msca_stride_slot(u8 cfg_mode, u32 ch, int which)
{
	ch &= 0xff;
	if (which == 0)
		return cfg_mode ? 30 * ch + 41 : 0xf0140 + (ch << 8);
	return cfg_mode ? 30 * ch + 49 : 0xf0160 + (ch << 8);
}

#endif /* TX_ISP_T33_MSCA_API4_H */
