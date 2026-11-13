/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 scaler level setter: readable C of the vendor's
 * tisp_set_scaler_level_control_set (reached through tisp_set_scaler_level).
 *
 * buf = { u8 set (0..2), .., u32 mode at +4, u8 level at +8 }.  The MSCA
 * block B (*e28ac[ch]) keeps per output set s the bytes +181 + 2s / +182 + 2s
 * (scaler bypass bits); their bit 0 form the MSCA bypass word
 * (bus 0xf004c, shadow index 20):
 *   bit 0..2 = B[181], B[183], B[185]   bit 4..6 = B[182], B[184], B[186]
 *  - mode 1: bypass the set's scaler (both bytes 1), write the word;
 *  - mode 0: set the sharpness level (<= 128) into bits 0..7 and 8..15 of
 *    the set's curve control (bus 0xf0174 + s * 0x100, shadow 69 + s * 32),
 *    keeping bits 16..24 and 28..31 of the hardware value (the vendor reads
 *    it with system_reg_read, the shadow index as address when the shadow
 *    is on), clear both bytes and write the bypass word;
 *  - any other mode only triggers.
 * Every accepted call marks e2898[ch] and writes 1 to the MSCA trigger
 * (bus 0xf0010, shadow 13).  A set >= 3 or a level > 128 is refused (-1)
 * with an error print.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_apiscl_oracle).
 */
#ifndef TX_ISP_T32_API_SCL_H
#define TX_ISP_T32_API_SCL_H

#include "tx_isp_t32_tp.h"

struct t32_api_scl_ops {
	u32 (*sys_reg_read)(u32 addr);
	void (*reg_write)(u32 ch, u32 addr, u32 val);
	void (*warn)(void);
};

static inline u32 t32_scl_bypass_word(const u8 *b)
{
	return (b[181] & 1) | (b[183] & 1) << 1 | (b[185] & 1) << 2 |
	       (b[182] & 1) << 4 | (b[184] & 1) << 5 | (b[186] & 1) << 6;
}

/* tisp_set_scaler_level_control_set: blk = *e28ac[ch], mark = &e2898[ch] */
static inline int t32_set_scaler_level_control_set(const struct t32_api_scl_ops *op, u8 shadow, u8 *blk,
						   u8 *mark, u32 ch, const u8 *buf)
{
	u32 s = buf[0], mode = buf[4] | (u32)buf[5] << 8 | (u32)buf[6] << 16 | (u32)buf[7] << 24;

	if (s >= 3) {
		op->warn();
		return -1;
	}
	if (mode == 1) {
		blk[181 + 2 * s] = 1;
		blk[182 + 2 * s] = 1;
		op->reg_write(ch, shadow ? 20 : 0xf004c, t32_scl_bypass_word(blk));
	} else if (mode == 0) {
		u32 lvl = buf[8], addr, v;

		if (lvl > 128) {
			op->warn();
			return -1;
		}
		addr = shadow ? (s << 5) + 69 : (s << 8) + 0xf0174;
		v = op->sys_reg_read(addr);
		op->reg_write(ch, addr, (lvl << 8 | lvl) | (v >> 28) << 28 | ((v >> 16) & 0x1ff) << 16);
		blk[181 + 2 * s] = 0;
		blk[182 + 2 * s] = 0;
		op->reg_write(ch, shadow ? 20 : 0xf004c, t32_scl_bypass_word(blk));
	}
	*mark = 1;
	op->reg_write(ch, shadow ? 13 : 0xf0010, 1);
	return 0;
}

#endif
