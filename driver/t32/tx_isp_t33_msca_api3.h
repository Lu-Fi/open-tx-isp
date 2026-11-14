/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA frame-drop / source-crop API.  The hardware accessors are passed in
 * (struct t33_msca_io) so the logic is independent of how they are provided.
 */
#ifndef TX_ISP_T33_MSCA_API3_H
#define TX_ISP_T33_MSCA_API3_H

#include "tx_isp_t33_blk.h"

struct t33_msca_io {
	u32 (*sys_rd)(u32 addr);
	void (*sys_wr)(u32 addr, u32 val);
	u32 (*reg_rd)(u32 id, u32 blk, u32 addr);
	u32 (*reg_wr)(u32 id, u32 blk, u32 addr, u32 val);
	int (*print)(int lvl, const char *fmt, ...);
};

/* tisp_msca_api_get_frame_drop: p = { 1, drop byte at +4, value at +8 }; init = stIspInitPar word 226 */
static inline int t33_msca_get_frame_drop(const struct t33_msca_io *io, u32 init, u32 id, u32 chan, u8 *out)
{
	u32 a = 0xf0120U + (chan << 8), b = 0xf0124U + (chan << 8), v;
	u32 *p = (u32 *)out;

	if (init == 1) {
		out[4] = (u8)io->sys_rd(a);
		v = io->sys_rd(b);
	} else {
		out[4] = (u8)io->reg_rd(id & 0xff, 23, a);
		v = io->reg_rd(id & 0xff, 23, b);
	}
	p[2] = v;
	p[0] = 1;
	return 0;
}

/* tisp_msca_api_set_frame_drop */
static inline int t33_msca_set_frame_drop(const struct t33_msca_io *io, u32 init, u32 chan, const u8 *in)
{
	const u32 *p = (const u32 *)in;
	u32 drop = 0, val = 0xffffffffU;

	if (p[0] == 1) {
		drop = in[4];
		if (drop >= 32U) {
			io->print(1, "FPS over 32 in the numerator or denominator!!! Setup failed!!!\n");
			return -1;
		}
		val = p[2];
	}
	if (init != 1) {
		io->print(1, "msca frame drop: isp not initialised\n");
		return 0;
	}
	io->sys_wr(0xf0120U + (chan << 8), drop);
	io->sys_wr(0xf0124U + (chan << 8), val);
	return 0;
}

/* tisp_msca_refresh_srcrop: cfg = mscaCfgMode */
static inline int t33_msca_refresh_srcrop(const struct t33_msca_io *io, u8 cfg, u32 id, u32 x, u32 y)
{
	io->reg_wr(id & 0xff, 23, cfg ? 23U : 0xf0090U, 0);
	return (int)io->reg_wr(id & 0xff, 23, cfg ? 24U : 0xf0094U, (x << 16) | y);
}

#endif /* TX_ISP_T33_MSCA_API3_H */
