/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA channel stream on/off and the OSD / line out-config getters:
 * tisp_msca_t33_api_chx_stream_on / _stream_off / _get_osd_out_cfg / _get_line_out_cfg.
 * attr = the vendor's stMscaAttr (one attribute block per channel group of three).
 */
#ifndef TX_ISP_T33_MSCASTRM_H
#define TX_ISP_T33_MSCASTRM_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_osd.h"

/* common body of tisp_msca_t33_api_chx_stream_on / _off */
static inline void t33_msca_chx_stream(u8 *const *attr, u8 *refresh, u32 ch, u32 on,
				       void (*chx_en)(u32, u32, u32))
{
	u32 c = ch & 0xff;

	attr[c / 3][6032 + c % 3] = (u8)on;
	chx_en(c / 3, c % 3, on);
	*refresh = 1;
}

/* tisp_msca_t33_api_chx_stream_on */
static inline void t33_msca_stream_on(u8 *const *attr, u8 *refresh, u32 ch, void (*chx_en)(u32, u32, u32))
{
	t33_msca_chx_stream(attr, refresh, ch, 1, chx_en);
}

/* tisp_msca_t33_api_chx_stream_off */
static inline void t33_msca_stream_off(u8 *const *attr, u8 *refresh, u32 ch, void (*chx_en)(u32, u32, u32))
{
	t33_msca_chx_stream(attr, refresh, ch, 0, chx_en);
}

/* tisp_msca_t33_api_get_osd_out_cfg */
static inline void t33_msca_get_osd_cfg(u8 *const *attr, u32 ch, u8 *p)
{
	const u8 *a = attr[ch & 0xff] + 712 + 20 * p[0];

	if (a[0] == 1) {
		p[1] = 1;
		t33_osd_p16(p + 2, t33_osd_u16(a + 2));
		t33_osd_p16(p + 4, t33_osd_u16(a + 4));
		t33_osd_p16(p + 6, t33_osd_u16(a + 6));
		t33_osd_p16(p + 8, t33_osd_u16(a + 8));
		t33_osd_p32(p + 12, (u32)(unsigned long)(a + 12));
		t33_osd_p16(p + 16, t33_osd_u16(a + 16));
	} else {
		p[1] = 0;
		t33_osd_p16(p + 2, 0);
		t33_osd_p16(p + 4, 0);
		t33_osd_p16(p + 6, 0);
		t33_osd_p16(p + 8, 0);
		t33_osd_p32(p + 12, 0);
		t33_osd_p16(p + 16, 0);
	}
}

/* tisp_msca_t33_api_get_line_out_cfg */
static inline void t33_msca_get_line_cfg(u8 *const *attr, u32 ch, u8 *p)
{
	const u8 *a = attr[ch & 0xff] + 912 + 36 * p[0];
	u32 t = t33_le32(a);

	t33_osd_p32(p + 4, t);
	p[25] = a[4];
	p[26] = a[5];
	p[27] = a[6];
	if (t > 3)
		return;
	p[12] = a[24];
	if (t == 2) {
		t33_osd_p16(p + 14, t33_osd_u16(a + 8));
		t33_osd_p16(p + 16, t33_osd_u16(a + 10));
		t33_osd_p16(p + 18, t33_osd_u16(a + 12));
		t33_osd_p16(p + 20, t33_osd_u16(a + 14));
	} else {
		t33_osd_p16(p + 14, t33_osd_u16(a + 16));
		t33_osd_p16(p + 16, t33_osd_u16(a + 18));
		t33_osd_p16(p + 18, t33_osd_u16(a + 20));
		t33_osd_p16(p + 20, t33_osd_u16(a + 22));
	}
	p[28] = a[27];
	p[29] = a[31];
	if (t == 1) {
		p[32] = a[26];
		t33_osd_p16(p + 30, a[28]);
	} else if (t != 2) {
		p[30] = a[26];
	}
}

#endif /* TX_ISP_T33_MSCASTRM_H */
