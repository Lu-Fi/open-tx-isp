/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA mask output: readable C of tisp_api_msca_set_mask(ch, cnt).  It pushes the pending
 * mask block configurations of the three sets of the channel's parameter block (sb, 872 bytes
 * per set) to the hardware.  Per set the dword at +884 holds one "changed" bit for each of the
 * four mask blocks (entries at +32 + 16 * k); a changed block is cleared and written as
 *   value 1 = (u16 +22 << 16 | u16 +24)    value 2 = u32 at +32 of the entry
 *   value 3 = (u16 +26 << 16 | u16 +28), or 0 when the entry's byte +20 is 0.
 * After the four blocks the enable bytes (+52 +68 +84 +100 of the set, ORed) are merged into a
 * mask kept over the sets (bit n = set n) and written.
 *   direct mode (t32_data_d03a0 == 0): every value goes through the RAM port 0xf8004 (value, then
 *       the target register address), *cnt counts 3 per block and 1 per set;
 *   shadow mode: tisp_reg_write(ch, index, value), index 136 + 16 * set + 4 * k (value 1),
 *       138 + ... (value 2), 137 + ... (value 3) and 18 for the mask.
 * The vendor's repeated re-reads of the d03a0 flag inside each mode's loop (a second, dead mode
 * test) are dropped; the vendor returns a leftover register, not a status, and the port nothing.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscam_oracle).
 */
#ifndef TX_ISP_T32_MSCA_MASK_H
#define TX_ISP_T32_MSCA_MASK_H

#include "tx_isp_t32_msca_api.h"
#include "tx_isp_t32_msca_io.h"

/* sb = block of channel ch; cnt is only used (and only valid) in direct mode */
static inline void t32_msca_api_set_mask(struct t32_ms_io *io, u8 *sb, u32 ch, u16 *cnt)
{
	u32 set, k, mask = 0;

	ch &= 0xff;
	for (set = 0; set < 3; set++) {
		u8 *b = sb + 872 * set;

		for (k = 0; k < 4; k++) {
			u32 f = t32_msa_r32(b + 884);
			u8 *p = b + (k + 2) * 16;
			u32 v3;

			if (!(f & 1u << k))
				continue;
			t32_msa_w32(b + 884, f & ~(1u << k));
			v3 = p[20] ? (u32)t32_msa_r16(p + 26) << 16 | t32_msa_r16(p + 28) : 0;
			if (io->shadow) {
				t32_ms_tw(io, ch, 136 + 16 * set + 4 * k, 0,
					  (u32)t32_msa_r16(p + 22) << 16 | t32_msa_r16(p + 24));
				t32_ms_tw(io, ch, 138 + 16 * set + 4 * k, 0, t32_msa_r32(p + 32));
				t32_ms_tw(io, ch, 137 + 16 * set + 4 * k, 0, v3);
			} else {
				t32_ms_sw(io, 0xf8004, (u32)t32_msa_r16(p + 22) << 16 | t32_msa_r16(p + 24));
				t32_ms_sw(io, 0xf8004, ((set + 15376) * 4 + k) * 16);
				t32_ms_sw(io, 0xf8004, t32_msa_r32(p + 32));
				t32_ms_sw(io, 0xf8004, 0xf0408 + 64 * set + 16 * k);
				t32_ms_sw(io, 0xf8004, v3);
				t32_ms_sw(io, 0xf8004, 0xf0404 + 64 * set + 16 * k);
				*cnt = (u16)(*cnt + 3);
			}
		}
		mask |= (u32)(b[52] | b[68] | b[84] | b[100]) << set;
		if (io->shadow) {
			t32_ms_tw(io, ch, 18, 0xf0044, mask);
		} else {
			t32_ms_sw(io, 0xf8004, mask);
			t32_ms_sw(io, 0xf8004, 0xf0044);
			*cnt = (u16)(*cnt + 1);
		}
	}
}

#endif /* TX_ISP_T32_MSCA_MASK_H */
