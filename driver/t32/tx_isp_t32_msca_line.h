/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA line output: readable C of tisp_api_msca_set_line(ch, cnt).  It pushes the pending
 * line configs (sb + 888 = changed bits for the 20 lines, line i at sb + 28 * i + 116:
 * +0 mode (0 plain, 1 with extra bits, 2 free line from (x0,y0) to (x1,y1)), +4 enable,
 * +6 x0, +8 y0, +10 x1, +12 y1, +16 u32 colour, +20 width, +21 / +22 / +24 options) and
 * the line colour sb[676] to the hardware.  Each line is four words:
 *   w0 = x0 << 16 | y0  w1 = x1 << 16 | y1  w2 = colour (24 bit) | width << 24  w3 = flags
 * mode 0 / 1 store the config as it is.  Mode 2 turns the segment into a start corner
 * (min x, min y), a size (dx, dy as signed 16 bit, normalised to dx >= 0) and a slope: the
 * larger of |dx|, |dy| divided into the smaller << 8, packed into w3 with a 2 bit selector
 * (bit 0 dy < 0, bit 1 |dx| < |dy|); a horizontal / vertical line gets its missing size from
 * the width byte instead.  A line number >= 8 with the enable byte set and a real slope is
 * refused with isp_printf(1, fmt, i) and nothing is written for it; modes >= 3 write nothing.
 *   direct mode (t32_data_d03a0 == 0): per changed line (bit cleared first) the colour pair
 *       (value, 0xf0058) and then (value, 0xf04c0 + 16 i + 4 k) for the four words, all through
 *       the RAM port 0xf8004; *cnt counts 1 for the colour pair and 4 per line written;
 *   shadow mode: only the FIRST changed line's bit is cleared and then tisp_reg_write(ch, 23,
 *       colour) and all 20 lines (changed or not) are written as indices 184 + 4 i + k; the other
 *       changed bits stay set.
 * The vendor's repeated d03a0 tests are dropped (dead); its return value is a leftover register.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscao_oracle).
 */
#ifndef TX_ISP_T32_MSCA_LINE_H
#define TX_ISP_T32_MSCA_LINE_H

#include "tx_isp_t32_msca_api.h"
#include "tx_isp_t32_msca_io.h"

/* the four words of line i (l = its config); returns 0 = ok, 1 = refused (isp_printf), 2 = no words */
static inline int t32_msca_line_words(const u8 *l, u32 i, u32 w[4])
{
	u32 mode = l[116];
	u32 col = t32_msa_r32(l + 132) & 0xffffff;

	w[0] = (u32)t32_msa_r16(l + 122) << 16 | t32_msa_r16(l + 124);
	w[1] = (u32)t32_msa_r16(l + 126) << 16 | t32_msa_r16(l + 128);
	w[2] = col | (u32)l[136] << 24;
	if (mode == 0) {
		w[3] = l[120] | (u32)l[137] << 8 | (u32)l[138] << 31;
		return 0;
	}
	if (mode == 1) {
		w[3] = (u32)l[116] << 4 | (u32)l[137] << 8 | l[120] | (u32)t32_msa_r16(l + 138) << 16 |
		       (u32)l[140] << 31;
		return 0;
	}
	if (mode != 2)
		return 2;
	{
		u32 x0 = t32_msa_r16(l + 122), y0 = t32_msa_r16(l + 124);
		u32 x1 = t32_msa_r16(l + 126), y1 = t32_msa_r16(l + 128);
		u32 dx = (x1 - x0) & 0xffff, dy = (y1 - y0) & 0xffff;
		u32 minx = x0, miny = y0 < y1 ? y0 : y1;
		int v1, s7;

		if (x1 < x0) {
			dx = -dx;
			dy = -dy;
			minx = x1;
		}
		v1 = (int16_t)dx;
		s7 = (int16_t)dy;
		if (s7 == 0 || v1 == 0) {
			s7 = s7 < 0 ? -s7 : s7;
			s7 = (int16_t)s7;
			if (s7 == 0)
				s7 = l[136];
			if (v1 == 0)
				v1 = l[136];
			w[0] = minx << 16 | miny;
			w[1] = (u32)v1 << 16 | (u32)s7;
			w[3] = (u32)l[137] << 8 | l[120];
			return 0;
		}
		{
			u32 sel = 0, q = 0;

			if (i >= 8 && l[120])
				return 1;
			if (l[120]) {
				if (s7 < 0) {
					s7 = (int16_t)-s7;
					sel = 1;
				}
				if (v1 < s7) {
					q = (u32)((int)((u32)v1 << 8) / s7);
					sel |= 2;
				} else {
					q = (u32)((int)((u32)s7 << 8) / v1);
				}
				q &= 0xffff;
			}
			w[0] = minx << 16 | miny;
			w[1] = (u32)v1 << 16 | (u32)s7;
			w[3] = ((sel << 9 | q) << 16) | (u32)l[137] << 8 | ((l[120] | 0x20) & 0xff);
		}
	}
	return 0;
}

/*
 * sb = block of channel ch; cnt: direct mode only.  Lines refused with isp_printf are stored
 * to skipped[] (number returned in *nskip).
 */
static inline void t32_msca_api_set_line(struct t32_ms_io *io, u8 *sb, u32 ch, u16 *cnt,
					 u32 *skipped, unsigned int *nskip)
{
	u32 i, k, w[4], f;
	int st;

	ch &= 0xff;
	*nskip = 0;
	if (io->shadow) {
		for (i = 0; i < 20; i++) {
			f = t32_msa_r32(sb + 888);
			if (f & 1u << i)
				break;
		}
		if (i == 20)
			return;
		t32_msa_w32(sb + 888, f & ~(1u << i));
		t32_ms_tw(io, ch, 23, 0, sb[676]);
		for (i = 0; i < 20; i++) {
			st = t32_msca_line_words(sb + 28 * i, i, w);
			if (st == 1)
				skipped[(*nskip)++] = i;
			if (st)
				continue;
			for (k = 0; k < 4; k++)
				t32_ms_tw(io, ch, 184 + 4 * i + k, 0, w[k]);
		}
		return;
	}
	for (i = 0; i < 20; i++) {
		f = t32_msa_r32(sb + 888);
		if (!(f & 1u << i))
			continue;
		t32_msa_w32(sb + 888, f & ~(1u << i));
		t32_ms_sw(io, 0xf8004, sb[676]);
		t32_ms_sw(io, 0xf8004, 0xf0058);
		*cnt = (u16)(*cnt + 1);
		st = t32_msca_line_words(sb + 28 * i, i, w);
		if (st == 1)
			skipped[(*nskip)++] = i;
		if (st)
			continue;
		for (k = 0; k < 4; k++) {
			t32_ms_sw(io, 0xf8004, w[k]);
			t32_ms_sw(io, 0xf8004, 0xf04c0 + 16 * i + 4 * k);
		}
		*cnt = (u16)(*cnt + 4);
	}
}

#endif /* TX_ISP_T32_MSCA_LINE_H */
