/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) MDNS parameter helpers: the compression parameter
 * calculation (tisp_mdns_cmp_par_calc), the buffer clear
 * (tisp_mdns_buf_clc) and the refresh entry (tisp_mdns_refresh).
 *
 * The block state is the vendor's mdnsInfo[chn] word array (see
 * tx_isp_t33_mdns.h); the compression words 2191..2200 are the register
 * values tisp_mdns_t33_cmp_reg_write writes.
 */
#ifndef TX_ISP_T33_MDNSP_H
#define TX_ISP_T33_MDNSP_H

#include "tx_isp_t33_blk.h"

/* tisp_mdns_buf_clc: clear the three MDNS buffers (list slots 20..22) */
static inline int t33_mdns_buf_clc(u32 id, int (*wr)(u32 id, u32 blk, u32 reg, u32 val))
{
	wr(id, 15, 20, 0);
	wr(id, 15, 21, 0);
	return wr(id, 15, 22, 0);
}

/*
 * tisp_mdns_cmp_par_calc: compression of the reference frame at ratio
 * v[2201] (1/100, 1100..2000), w x h, into mdnsInfo words 2191..2200
 * (word 2193 stays 0 as at init).
 */
static inline void t33_mdns_cmp_par(u32 *v, u32 h, u32 w)
{
	u32 r = v[2201] & 0xffff;
	int blk = (int)(((w + 0x1f) >> 5) * h), step, rest;
	u32 bytes = 0x5dc00 / r, lo, q, c, e;

	v[2191] = (u32)((int)(bytes * blk + 0x3f) / 0x40);
	v[2192] = (h - 1) << 16 | (w - 1);
	e = 0x2000 - (w * 16000) / r;
	lo = 0xc00;
	if ((int)e < 0xc00) {
		lo = r > 0x4b0 ? 0x400 : 0x9c4;
		if ((int)e >= (int)lo)
			lo = e;
	}
	step = (int)(0x2000 - lo) / blk;
	c = bytes - step;
	if ((int)c < 100)
		c = 100;
	if ((int)c > 0x180)
		c = 0x180;
	rest = (int)(0x2000 - lo) - blk * step;
	if (rest < 1)
		rest = 1;
	q = (u32)(blk / rest);
	q = (int)q >= 0xff ? 0xff : (int)q > 0 ? q : 1;
	v[2194] = (((int)(c * 3) >> 2) << 16) | c;
	v[2195] = (r > 0x4b0 ? 0x2c | 0x20 << 16 : 0x28 | 0x28 << 16) | 0x64007800;
	v[2196] = q | 0xf040200;
	v[2197] = r < 1200 ? 0xfcfc0002 : 0xfdfe0002;
	v[2198] = c << 16 | lo;
	v[2199] = (int)c < 0xaa ? 0x140008 : 0x140005;
	v[2200] = ((u32)blk - 0x100) | 0x76000000;
}

/* tisp_mdns_refresh: refresh the channel's parameters at the given gain */
static inline int t33_mdns_refresh(u32 chn, u32 gain, u32 *v)
{
	extern void tisp_mdns_par_refresh(u32 chn, u32 gain, u32 mode);

	tisp_mdns_par_refresh(chn, gain, v[2182]);
	return 0;
}

#endif /* TX_ISP_T33_MDNSP_H */
