/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 9: ispcore_sync_sensor_attr (0xb6ac4).
 * attr NULL clears the 4 per-channel sensor attribute copies (104 B each at core private +324).
 * Otherwise the 104 B attribute is copied to the slot of its channel (u16 at +92 of the channel
 * info that attr word +56 points to) and the tisp sensor block (124 B) is built from the sensor
 * info that attr word +52 points to (and attr words +68..+76), then handed to
 * lib_tisp_sync_sensor_attr(channel byte, block).
 *
 * Vendor behaviour kept: the channel index is not range checked; the bytes of the block that the
 * vendor does not set are uninitialised there (0 here, as in the oracle's reference run).
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispsync_oracle).
 */
#ifndef TX_ISP_T32_ISPSYNC_H
#define TX_ISP_T32_ISPSYNC_H

#include "tx_isp_t32_isppm.h"

#define T32_ATTR_SIZE		104
#define T32_PRIV_ATTRS		324	/* 4 x 104 B */
#define T32_SYNC_BLK		124

struct t32_sync_ops {
	void (*log)(u32 level, u32 line);		/* isp_printf */
	void (*sync)(u32 ch, const u8 *blk);		/* lib_tisp_sync_sensor_attr */
};

static inline u16 t32_ic_r16(u32 a, u32 off)
{
	return *(volatile u16 *)T32_IC_P(a + off);
}

static inline void t32_blk_w32(u8 *b, u32 off, u32 v)
{
	memcpy(b + off, &v, 4);
}

static inline int t32_ispcore_sync_sensor_attr(const struct t32_sync_ops *ops, u32 sd, u32 attr)
{
	u32 priv = t32_pm_priv(sd), si, ci, k;
	u8 a[T32_ATTR_SIZE], blk[T32_SYNC_BLK] = { 0 };
	u16 h;

	if (!priv) {
		ops->log(2, 3757);
		return T32_EINVAL;
	}
	if (!attr) {
		for (k = 0; k < 4; k++)
			memset(T32_IC_P(priv + T32_PRIV_ATTRS + k * T32_ATTR_SIZE), 0, T32_ATTR_SIZE);
		return 0;
	}
	memcpy(a, T32_IC_P(attr), T32_ATTR_SIZE);
	memcpy(&ci, a + 56, 4);		/* channel info */
	memcpy(T32_IC_P(priv + T32_PRIV_ATTRS + t32_ic_r16(ci, 92) * T32_ATTR_SIZE), T32_IC_P(attr), T32_ATTR_SIZE);
	memcpy(&si, a + 52, 4);		/* sensor info */
	h = t32_ic_r16(si, 276);
	memcpy(blk + 0, &h, 2);
	h = t32_ic_r16(si, 278);
	memcpy(blk + 2, &h, 2);
	t32_blk_w32(blk, 36, t32_ic_r32(si, 160));
	t32_blk_w32(blk, 40, t32_ic_r32(si, 192));
	memcpy(blk + 56, a + 68, 12);	/* attr words +68, +72, +76 */
	t32_blk_w32(blk, 68, t32_ic_r32(si, 220));
	t32_blk_w32(blk, 72, t32_ic_r32(si, 224));
	t32_blk_w32(blk, 80, t32_ic_r32(si, 224));
	t32_blk_w32(blk, 84, t32_ic_r32(si, 280));
	t32_blk_w32(blk, 88, t32_ic_r32(si, 236));
	t32_blk_w32(blk, 92, t32_ic_r32(si, 240));
	t32_blk_w32(blk, 96, t32_ic_r32(si, 176));
	t32_blk_w32(blk, 108, t32_ic_r32(si, 152));
	t32_blk_w32(blk, 112, t32_ic_r32(si, 216));
	blk[116] = (u8)t32_ic_r16(si, 228);
	blk[117] = (u8)t32_ic_r16(si, 164);
	blk[123] = *(volatile u8 *)T32_IC_P(si + 196) ? *(volatile u8 *)T32_IC_P(si + 196) : 1;
	ops->sync(*(volatile u8 *)T32_IC_P(ci + 92), blk);
	return 0;
}

#endif /* TX_ISP_T32_ISPSYNC_H */
