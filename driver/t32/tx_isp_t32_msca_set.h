/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA configuration setters: readable C of tisp_msca_api_set_line_out_cfg,
 * tisp_msca_api_set_osd_out_cfg and tisp_msca_api_set_autozoom.  They copy a
 * user configuration block into the per-channel parameter block (sb, 2636
 * bytes, layout in tx_isp_t32_msca_api.h) and mark what changed:
 *   sb + 888 : bit n = line config n changed     sb + 880 : bit n = osd config n changed
 *   autozoom: the three sets (872 bytes apart) get their window words at
 *             +20..+28 and the autozoom-changed flag (t32_data_e2760[ch]) is set.
 * The vendor line setter returns the block pointer left in v0 (not a status);
 * the port returns nothing.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscag_oracle).
 */
#ifndef TX_ISP_T32_MSCA_SET_H
#define TX_ISP_T32_MSCA_SET_H

#include "tx_isp_t32_msca_api.h"

/* tisp_msca_api_set_line_out_cfg: buf[0] = line index, buf[4] = mode (0, 1, 2; others only mark) */
static inline void t32_msca_api_set_line_out_cfg(u8 *sb, const u8 *buf)
{
	u8 *l = sb + buf[0] * 28;
	u32 mode = t32_msa_r32(buf + 4);

	l[116] = (u8)mode;
	sb[676] = buf[1];
	if (mode <= 2) {
		l[120] = buf[12];
		t32_msa_w16(l + 122, t32_msa_r16(buf + 14));
		t32_msa_w16(l + 124, t32_msa_r16(buf + 16));
		t32_msa_w16(l + 126, t32_msa_r16(buf + 18));
		t32_msa_w16(l + 128, t32_msa_r16(buf + 20));
		t32_msa_w32(l + 132, (u32)buf[25] << 16 | (u32)buf[26] << 8 | buf[27]);
		l[136] = buf[28];
		l[137] = buf[29];
		if (mode == 1) {
			t32_msa_w16(l + 138, t32_msa_r16(buf + 30));
			l[140] = buf[32];
		} else if (mode == 0) {
			l[138] = buf[30];
		}
	}
	t32_msa_w32(sb + 888, t32_msa_r32(sb + 888) | 1u << buf[0]);
}

/*
 * tisp_msca_api_set_osd_out_cfg: buf[0] = osd index, buf[1] = enable.  An enabled window must
 * lie inside the picture size at sb+44 / sb+46 (the first check also reads the stored size,
 * the second the new one).  *msgs = number of isp_printf messages the vendor issues (0, 1, 2);
 * returns 0, or -1 when the new window does not fit.  The vendor's clamp of width / height to
 * the picture after the stores is dead code (the check above already guarantees it) and dropped.
 */
static inline int t32_msca_api_set_osd_out_cfg(u8 *sb, const u8 *buf, unsigned int *msgs)
{
	u8 *e = sb + buf[0] * 20;
	u32 w = t32_msa_r16(sb + 44), h = t32_msa_r16(sb + 46);

	*msgs = 0;
	if (buf[1] == 1) {
		u32 x = t32_msa_r16(buf + 2), y = t32_msa_r16(buf + 4);
		u32 ww = t32_msa_r16(buf + 6), hh = t32_msa_r16(buf + 8);

		if (w < t32_msa_r16(e + 686) || h < t32_msa_r16(e + 688)) {
			*msgs = 1;
			return 0;
		}
		if ((int)w < (int)(x + ww) || (int)h < (int)(y + hh)) {
			*msgs = 2;
			return -1;
		}
		e[680] = buf[1];
		t32_msa_w16(e + 682, x);
		t32_msa_w16(e + 684, y);
		t32_msa_w16(e + 686, ww);
		t32_msa_w16(e + 688, hh);
		t32_msa_w32(e + 692, t32_msa_r32(buf + 12));
		t32_msa_w16(e + 696, t32_msa_r16(buf + 16));
	} else {
		e[680] = buf[1];
		t32_msa_w16(e + 682, 0);
		t32_msa_w16(e + 684, 0);
		t32_msa_w16(e + 686, 0);
		t32_msa_w16(e + 688, 0);
		t32_msa_w32(e + 692, 0);
		t32_msa_w16(e + 696, 0);
	}
	t32_msa_w32(sb + 880, t32_msa_r32(sb + 880) | 1u << buf[0]);
	return 0;
}

/*
 * tisp_msca_api_set_autozoom: buf is 5 x 3 words (word k*3 + n = field k of set n).
 * Per set n: buf[n] == 1 takes the fields from buf, 0 keeps the saved block (e2650[ch], same
 * layout), anything else takes the channel defaults (def, dc284[ch]: words 0 and 1 as +26/+28).
 * Sets *flag (t32_data_e2760[ch]); returns 1 (the vendor's v0).
 */
static inline u32 t32_msca_api_set_autozoom(u8 *sb, const u8 *buf, const u8 *saved, const u8 *def, u8 *flag)
{
	unsigned int n;

	for (n = 0; n < 3; n++) {
		u8 *s = sb + n * T32_MSCA_SET_SZ;
		const u8 *b = buf + n * 4, *v = saved + n * 4;
		u32 last;

		if (t32_msa_r32(b) == 1) {
			t32_msa_w16(s + 20, 1);
			t32_msa_w16(s + 22, t32_msa_r32(b + 12));
			t32_msa_w16(s + 24, t32_msa_r32(b + 24));
			t32_msa_w16(s + 26, t32_msa_r32(b + 36));
			last = t32_msa_r32(b + 48);
		} else if (t32_msa_r32(v) != 0) {
			t32_msa_w16(s + 20, 0);
			t32_msa_w16(s + 22, t32_msa_r32(v + 12));
			t32_msa_w16(s + 24, t32_msa_r32(v + 24));
			t32_msa_w16(s + 26, t32_msa_r32(v + 36));
			last = t32_msa_r32(v + 48);
		} else {
			t32_msa_w16(s + 20, 0);
			t32_msa_w16(s + 22, 0);
			t32_msa_w16(s + 24, 0);
			t32_msa_w16(s + 26, t32_msa_r32(def));
			last = t32_msa_r32(def + 4);
		}
		t32_msa_w16(s + 28, last);
	}
	*flag = 1;
	return 1;
}

#endif /* TX_ISP_T32_MSCA_SET_H */
