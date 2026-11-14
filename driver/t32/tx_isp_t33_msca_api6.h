/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA address FIFO register access and channel attribute set
 * (vendor tisp_msca_t33_addr_fifo_read / _write / tisp_msca_t33_channel_attr_set).
 * Module state (stIspInitPar mode word, mscaCfgMode, stMscaAttr, fcrop_attr) is passed in.
 */
#ifndef TX_ISP_T33_MSCA_API6_H
#define TX_ISP_T33_MSCA_API6_H

#include "tx_isp_t33_blk.h"

extern u32 system_reg_read(u32 addr);
extern void system_reg_write(u32 addr, u32 val);
extern u32 tisp_reg_read_t33(u32 id, u32 blk, u32 reg);
extern void tisp_reg_write_t33(u32 id, u32 blk, u32 reg, u32 val);

/* register of the first / second FIFO address word of FIFO f */
static inline u32 t33_msca_fifo_reg_a(u32 cfg, u32 f)
{
	if (cfg)
		return 30 * f + 37;
	return (f << 8) + 0xf0130;
}

static inline u32 t33_msca_fifo_reg_b(u32 cfg, u32 f)
{
	if (cfg)
		return 30 * f + 45;
	return (f << 8) + 0xf0150;
}

/* tisp_msca_t33_addr_fifo_write */
static inline void t33_msca_addr_fifo_write(u32 init_mode, u32 cfg_mode, u32 id, u32 fifo, u32 a, u32 b)
{
	u32 f = fifo & 0xff;

	if (init_mode == 1) {
		system_reg_write(0xf0130 + (f << 8), a);
		system_reg_write(0xf0150 + (f << 8), b);
		return;
	}
	tisp_reg_write_t33(id & 0xff, 23, t33_msca_fifo_reg_a(cfg_mode & 0xff, f), a);
	tisp_reg_write_t33(id & 0xff, 23, t33_msca_fifo_reg_b(cfg_mode & 0xff, f), b);
}

/* tisp_msca_t33_addr_fifo_read */
static inline void t33_msca_addr_fifo_read(u32 init_mode, u32 cfg_mode, u32 id, u32 fifo, u32 *a, u32 *b)
{
	u32 f = fifo & 0xff;

	if (init_mode == 1) {
		*a = system_reg_read(0xf0138 + (f << 8)) & ~7u;
		*b = system_reg_read(0xf0158 + (f << 8)) & ~7u;
		return;
	}
	*a = tisp_reg_read_t33(id & 0xff, 23, t33_msca_fifo_reg_a(cfg_mode & 0xff, f));
	*b = tisp_reg_read_t33(id & 0xff, 23, t33_msca_fifo_reg_b(cfg_mode & 0xff, f));
}

static inline u16 t33_msca_h_get(const u8 *p, u32 off)
{
	return *(const u16 *)(p + off);
}

static inline void t33_msca_h_set(u8 *p, u32 off, u32 v)
{
	*(u16 *)(p + off) = (u16)v;
}

/*
 * tisp_msca_t33_channel_attr_set: channel ch (group ch / 3, slot ch % 3) of the
 * attribute table (22 bytes per slot, base[grp]) and the crop table (words).
 */
static inline void t33_msca_channel_attr_set(u32 ch, const u32 *a, u8 **tab, u32 *fcrop)
{
	u32 grp = (ch & 0xff) / 3, idx = (ch & 0xff) % 3;
	u8 *base = tab[grp];
	u8 *e = base + 22 * idx;
	u32 v = 15 * grp + idx;

	if (a[8] == 1) {
		t33_msca_h_set(e, 6, a[9]);
		t33_msca_h_set(e, 8, a[10]);
		t33_msca_h_set(e, 2, a[11]);
		t33_msca_h_set(e, 4, a[12]);
		fcrop[v] = a[8];
		fcrop[v + 3] = a[9];
		fcrop[v + 6] = a[10];
		fcrop[v + 9] = a[11];
		fcrop[v + 12] = a[12];
	} else {
		t33_msca_h_set(e, 6, 0);
		t33_msca_h_set(e, 8, 0);
		t33_msca_h_set(e, 2, t33_msca_h_get(base, 66));
		t33_msca_h_set(e, 4, t33_msca_h_get(base, 68));
		fcrop[v] = 0;
		fcrop[v + 3] = 0;
		fcrop[v + 6] = 0;
		fcrop[v + 9] = t33_msca_h_get(base, 66);
		fcrop[v + 12] = t33_msca_h_get(base, 68);
	}
	if (a[0]) {
		t33_msca_h_set(e, 10, a[1]);
		t33_msca_h_set(e, 12, a[2]);
	} else {
		t33_msca_h_set(e, 10, t33_msca_h_get(e, 2));
		t33_msca_h_set(e, 12, t33_msca_h_get(e, 4));
	}
	if (a[3]) {
		t33_msca_h_set(e, 18, a[4]);
		t33_msca_h_set(e, 20, a[5]);
		t33_msca_h_set(e, 14, a[6]);
		t33_msca_h_set(e, 16, a[7]);
	} else {
		t33_msca_h_set(e, 18, 0);
		t33_msca_h_set(e, 20, 0);
		t33_msca_h_set(e, 14, t33_msca_h_get(e, 10));
		t33_msca_h_set(e, 16, t33_msca_h_get(e, 12));
	}
}

#endif /* TX_ISP_T33_MSCA_API6_H */
