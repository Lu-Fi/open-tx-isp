/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 csccr block: parameter API (vendor tisp_csccr_api_get / tisp_csccr_api_set /
 * tisp_csccr_para_refresh_by_mode).
 *
 * csccrInfo is the vendor's per-channel pointer array (16 bytes = 4 channels); each
 * entry points at a csccrInfo block whose word 3 is the section pointer and whose
 * words 0..2 hold the packed register values.  The callees (the register write, the
 * refresh / sync / bin update) are ported / proven elsewhere and are called by their
 * vendor names.
 */
#ifndef TX_ISP_T33_CSCR_H
#define TX_ISP_T33_CSCR_H

#include "tx_isp_t33_blk.h"

#ifndef T33_CSCR_GLUE
extern int tisp_csccr_sync_api_para(u32 idx, u32 one);
extern int tisp_csccr_bin_par_update(u32 idx);
extern int tisp_reg_write_t33(u32 idx, u32 list, u32 slot, u32 val);
#endif

extern u8 t33_csccr_default_mode[12];
extern u8 t33_csccr_stretch_mode[12];
extern u8 t33_csccr_compress_mode[12];
/* vendor CSWTCH.48 (enable byte per mode 0..3) and CSWTCH.47 (source pointer per mode) */
extern u8 t33_csccr_alt_en[4];
extern const u8 *t33_csccr_alt_src[4];

static inline void t33_cscR_copy12(u8 *d, const u8 *s)
{
	unsigned int i;

	for (i = 0; i < 12; i++)
		d[i] = s[i];
}

/* tisp_csccr_api_get: copy the 12-byte parameter block of channel ch to out */
static inline int t33_csccr_api_get(u32 ch, u8 *out, u32 *const *csccr_info)
{
	u32 *info = csccr_info[ch & 0xff];

	if (!info)
		return -1;
	t33_cscR_copy12(out, (u8 *)info + 16);
	return 0;
}

/* tisp_csccr_para_refresh_by_mode: copy the selected mode table into dest */
static inline void t33_csccr_para_refresh_by_mode(u32 mode, u32 one, u8 *dest)
{
	const u8 *src;
	unsigned int i;

	if ((one & 0xff) != 1) {
		if (mode < 4 && t33_csccr_alt_en[mode] == 1) {
			dest[4] = t33_csccr_alt_src[mode][0];
			for (i = 0; i < 8; i++)
				dest[5 + i] = t33_csccr_alt_src[mode][4 + i];
		}
		return;
	}
	if (mode == 1) {
		src = t33_csccr_stretch_mode;
	} else if (mode == 0) {
		src = t33_csccr_default_mode;
	} else if (mode == 2) {
		src = t33_csccr_compress_mode;
	} else {
		return;
	}
	t33_cscR_copy12(dest, src);
}

/* tisp_csccr_api_set */
static inline int t33_csccr_api_set(u32 ch, const u32 *in, u32 *const *csccr_info, const u8 *cfg_mode,
				    u8 *refresh)
{
	u32 idx = ch & 0xff;
	u32 *info = csccr_info[idx];
	u8 *sec = (u8 *)info[3];
	u32 q0, q1, w2;
	u8 b8, b9, b10;
	int s3, s5;

	sec[14] = 2;
	t33_cscR_copy12((u8 *)info + 16, (const u8 *)in);
	t33_csccr_para_refresh_by_mode(t33_le32((const u8 *)in), 1, (u8 *)info + 16);
	tisp_csccr_sync_api_para(idx, 0);
	tisp_csccr_bin_par_update(idx);
	info = csccr_info[idx];
	q0 = (u16)info[0];
	q1 = (u16)info[1];
	w2 = info[2];
	b8 = w2 & 0xff;
	b9 = (w2 >> 8) & 0xff;
	b10 = (w2 >> 16) & 0xff;
	s3 = (int)(short)(u16)(info[0] >> 16);
	s5 = (int)(short)(u16)(info[1] >> 16);
	tisp_reg_write_t33(idx, 22, *cfg_mode ? 3 : 0x14040, q1 & 0x7ff);
	tisp_reg_write_t33(idx, 22, *cfg_mode ? 4 : 0x14044, (u32)b10 << 8 | (u32)b8 << 16 | b9);
	tisp_reg_write_t33(idx, 22, *cfg_mode ? 6 : 0x14050, q0 & 0x7ff);
	tisp_reg_write_t33(idx, 22, *cfg_mode ? 7 : 0x14054, ((u32)(s3 & 0x1ff) << 16) | (u32)(s5 & 0x1ff));
	tisp_reg_write_t33(idx, 22, *cfg_mode ? 1 : 0x14010, 3);
	*refresh = 1;
	return 0;
}

#endif /* TX_ISP_T33_CSCR_H */
