/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA OSD output: readable C of tisp_api_msca_set_osd(ch, cnt).  It pushes the pending
 * OSD window configs (sb + 880 = changed bits for the 8 windows, window i at sb + 20 * i + 680:
 * +0 enable, +2 x, +4 y, +6 w, +8 h, +12 u32, +16 u16) and the OSD parameter bytes at
 * sb + 840 .. 879 to the hardware.
 *   direct mode (t32_data_d03a0 == 0): a changed window first gets its geometry aligned (x, y
 *       even; w, h even, or w a multiple of 4 and h even for the 4-pixel-aligned windows: window
 *       numbers >= 4 when sb[844] == 2, numbers < 4 when sb[845] == 2) and each fix bumps
 *       t32_data_e28bc; then (value, register) pairs go through the RAM port 0xf8004 (window i:
 *       x << 16 | y -> 0xf0780 + 16 i, w << 16 | h -> +4, u32 -> +8, u16 -> +12), then the
 *       enable mask (0xf0048), the byte groups (0xf0750 / 0xf0754 / 0xf0760 + 4 r); *cnt counts 4
 *       per window and 1 per pair after the loop;
 *   shadow mode: tisp_reg_write(ch, index, value) with index 280 + 4 i + 0..3 for the windows,
 *       19 for the mask, 269 / 270 / 272 + r for the byte groups and, only here, the constants
 *       265 .. 268; no alignment fixes.
 * The vendor's repeated d03a0 tests inside each mode are dropped (dead); its return value is a
 * leftover register and not modelled.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscan_oracle).
 */
#ifndef TX_ISP_T32_MSCA_OSD_H
#define TX_ISP_T32_MSCA_OSD_H

#include "tx_isp_t32_msca_api.h"
#include "tx_isp_t32_msca_io.h"

/* 0xf8004 value / register pair of the direct mode, or tisp_reg_write(ch, idx, val) */
static inline void t32_msca_osd_put(struct t32_ms_io *io, u32 ch, u16 *cnt, u32 idx, u32 reg, u32 val)
{
	if (io->shadow) {
		t32_ms_tw(io, ch, idx, 0, val);
	} else {
		t32_ms_sw(io, 0xf8004, val);
		t32_ms_sw(io, 0xf8004, reg);
		*cnt = (u16)(*cnt + 1);
	}
}

/* sb = block of channel ch; cnt: direct mode only; e28bc = &t32_data_e28bc */
static inline void t32_msca_api_set_osd(struct t32_ms_io *io, u8 *sb, u32 ch, u16 *cnt, u32 *e28bc)
{
	u32 i, r, mask = 0;

	ch &= 0xff;
	for (i = 0; i < 8; i++) {
		u8 *e = sb + 20 * i;
		u32 f = t32_msa_r32(sb + 880);

		if (!(f & 1u << i))
			continue;
		t32_msa_w32(sb + 880, f & ~(1u << i));
		if (!io->shadow) {
			u32 x = t32_msa_r16(e + 682), y = t32_msa_r16(e + 684);
			u32 w, h;
			int four = (sb[844] == 2 && i >= 4) || (sb[845] == 2 && i < 4);

			if ((x & 1) || (y & 1)) {
				t32_msa_w16(e + 682, x & ~1u);
				t32_msa_w16(e + 684, y & ~1u);
				(*e28bc)++;
			}
			w = t32_msa_r16(e + 686);
			h = t32_msa_r16(e + 688);
			if (four ? ((w & 3) || (h & 1)) : ((w & 1) || (h & 1))) {
				t32_msa_w16(e + 686, w & (four ? 0xfffcu : ~1u));
				t32_msa_w16(e + 688, h & ~1u);
				(*e28bc)++;
			}
		}
		t32_msca_osd_put(io, ch, cnt, 280 + 4 * i, 0xf0780 + 16 * i,
				 (u32)t32_msa_r16(e + 682) << 16 | t32_msa_r16(e + 684));
		t32_msca_osd_put(io, ch, cnt, 281 + 4 * i, 0xf0784 + 16 * i,
				 (u32)t32_msa_r16(e + 686) << 16 | t32_msa_r16(e + 688));
		t32_msca_osd_put(io, ch, cnt, 282 + 4 * i, 0xf0788 + 16 * i, t32_msa_r32(e + 692));
		t32_msca_osd_put(io, ch, cnt, 283 + 4 * i, 0xf078c + 16 * i, t32_msa_r16(e + 696));
		mask |= (u32)e[680] << i;
	}
	t32_msca_osd_put(io, ch, cnt, 19, 0xf0048, mask);
	t32_msca_osd_put(io, ch, cnt, 269, 0xf0750, (u32)sb[841] << 4 | sb[840]);
	if (io->shadow) {
		t32_ms_tw(io, ch, 265, 0, 0x381da506);
		t32_ms_tw(io, ch, 266, 0, 0xd13b5e02);
		t32_ms_tw(io, ch, 267, 0, 0xf6e70064);
		t32_ms_tw(io, ch, 268, 0, 0x8010);
	}
	t32_msca_osd_put(io, ch, cnt, 270, 0xf0754,
			 (u32)sb[846] << 4 | (u32)sb[842] << 8 | sb[844] | (u32)sb[845] << 16 |
			 (u32)sb[847] << 20 | (u32)sb[843] << 24);
	for (r = 0; r < 8; r++) {
		const u8 *g = sb + 848 + (r & 3) + 16 * (r >> 2);

		t32_msca_osd_put(io, ch, cnt, 272 + r, 0xf0760 + 4 * r,
				 g[0] | (u32)g[4] << 8 | (u32)g[8] << 16 | (u32)g[12] << 24);
	}
}

#endif /* TX_ISP_T32_MSCA_OSD_H */
