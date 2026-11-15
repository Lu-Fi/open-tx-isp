/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 TMO block P: tisp_tmo_tmo_size_update, tisp_tmo_tmoadr_get_statistics,
 * tisp_tmo_interrupt_static and tisp_tmo_modechange_stat_refresh.
 * The block state is the vendor's tmoInfo word array (v[byte offset / 4]).
 */
#ifndef TX_ISP_T33_TMOP_H
#define TX_ISP_T33_TMOP_H

#include "tx_isp_t33_blk.h"

/* hardware / other-block callees (system_reg_*, soft process, DMA sync, memcpy) */
struct t33_tmop_ops {
	u32 (*reg_read)(u32 addr);
	void (*reg_write)(u32 addr, u32 val);
	void (*soft_process)(u32 ch);
	void (*cache_sync)(u32 a0, void *buf, u32 len, u32 dir);
	void (*copy)(void *dst, const void *src, u32 len);
};

static inline void t33_tmop_st16(u8 *p, u32 x)
{
	p[0] = (u8)x;
	p[1] = (u8)(x >> 8);
}

/*
 * tisp_tmo_tmo_size_update: v the tmoInfo block, sec the section its word 683 (byte 2732) points at,
 * par the sensor size block (width u16 at 12, height u16 at 14).
 */
static inline void t33_tmop_size_update(u32 *v, u8 *sec, const u8 *par)
{
	u32 rows, cols, n, c, a;

	rows = (t32_tp_le16(par + 14) + 7) / 15;
	cols = (t32_tp_le16(par + 12) + 12) / 25;
	n = rows * cols;
	v[0] = rows;
	v[1] = cols;
	for (c = 13, a = 0x2000; c <= 25; c++, a <<= 1)
		if (n < (8U << (c - 13)))
			break;
	if (c <= 25) {
		v[2] = ((n >> 1) + a) / n;
		v[3] = c;
	}
	t33_tmop_st16(sec + 4, v[0]);
	t33_tmop_st16(sec + 6, v[1]);
	t33_tmop_st16(sec + 8, v[2]);
	t33_tmop_st16(sec + 10, v[3]);
}

/* tisp_tmo_tmoadr_get_statistics */
static inline void t33_tmop_get_stat(u32 *v, const struct t33_tmop_ops *o)
{
	unsigned int i;

	v[10828 / 4] = o->reg_read(28888);
	for (i = 0; i < 8; i++)
		v[3240 / 4 + i] = o->reg_read(28824 + 4 * i) & 0x1fffff;
	for (i = 0; i < 8; i++)
		v[3272 / 4 + i] = o->reg_read(28856 + 4 * i) & 0x1fffffff;
	o->reg_write(28888, 0);
}

/* tisp_tmo_interrupt_static: statistics, frame counter wrap at 5000001, soft process */
static inline int t33_tmop_irq_static(u32 *v, const struct t33_tmop_ops *o, u32 *counter, u32 ch)
{
	u32 c;

	t33_tmop_get_stat(v, o);
	c = *counter + 1;
	*counter = (int)c < 5000001 ? c : 0;
	o->soft_process(ch);
	return 1;
}

/* tisp_tmo_modechange_stat_refresh: curve_x = the 402 bytes of static_tmo_curve_x */
static inline void t33_tmop_mode_refresh(u32 *v, const u8 *curve_x, const struct t33_tmop_ops *o)
{
	static const u16 pat[10] = { 0, 410, 819, 1229, 1638, 2048, 2457, 2867, 3276, 3686 };
	u8 *grid = (u8 *)v + 3304;
	unsigned int r, j;

	for (r = 0; r < 375; r++)
		for (j = 0; j < 10; j++)
			t33_tmop_st16(grid + 20 * r + 2 * j, pat[j]);
	o->cache_sync(0, grid, 7500, 1);
	o->copy((u8 *)v + 2736, curve_x, 402);
}

#endif /* TX_ISP_T33_TMOP_H */
