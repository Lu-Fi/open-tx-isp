/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) YDNS / SDNS parameter refresh (vendor tisp_ydns_par_refresh,
 * tisp_sdns_par_refresh).
 *
 * Both functions keep a per-channel state block (ydnsInfo[ch] / sdnsInfo[ch])
 * whose byte 104 / 148 holds the last gain and byte 100 / 144 the gain
 * tolerance.  A refresh with the same gain (within the tolerance) is skipped.
 * Otherwise the bin section is re-interpolated into the state, the vendor's
 * pre/post para updates run, and (when the module's DMA is idle) the register
 * list is written through tisp_reg_write_t33.  The refresh state byte is set
 * to 1 when the list was written, 2 when the DMA was busy.
 *
 * The callees are passed in, they are provided elsewhere (runtime
 * tx_isp_t32_hw.c).  Return value follows the vendor binary: 1 on gain-skip,
 * 0x10000 after the parameter update path (success or DMA busy).
 */
#ifndef TX_ISP_T33_PARA_H
#define TX_ISP_T33_PARA_H

#include "tx_isp_t33_blk.h"

/* tisp_simple_intp_uint8_t: 11-point u8 table at s, 16.16 gain */
static inline u32 t33_simple_intp_u8(u32 i, u32 f, const u8 *s)
{
	return t32_tp_intp_u8(i, f, s);
}

struct t33_par_ops {
	void (*reg_write)(u32 ch, u32 mod, u32 reg, u32 val);
	int (*dma_state)(u32 mod);
	void (*pre_update)(u32 ch);
	void (*para_update)(u32 ch);
};

/* ---- YDNS, module 17 ---------------------------------------------------- */

#define T33_YDNS_PAR_MOD	17
#define T33_YDNS_PAR_GAIN	104	/* state byte offset: last gain */
#define T33_YDNS_PAR_TOL	100	/* state byte offset: gain tolerance */
#define T33_YDNS_PAR_SEC	96	/* state byte offset: section pointer */

/* tisp_ydns_par_refresh */
static inline u32 t33_ydns_par_refresh(u32 ch, u32 gain, u32 *inf, u8 cfg_mode,
				       const struct t33_par_ops *ops, u8 *refresh)
{
	const u8 *sec;
	u32 last, i, f, v;
	unsigned int k;

	ch &= 0xff;
	last = inf[T33_YDNS_PAR_GAIN / 4];
	if (last != 0xffffffff) {
		u32 d = gain > last ? gain - last : last - gain;

		if (d < inf[T33_YDNS_PAR_TOL / 4])
			return 1;
	}
	inf[T33_YDNS_PAR_GAIN / 4] = gain;
	ops->pre_update(ch);
	sec = (const u8 *)(unsigned long)inf[T33_YDNS_PAR_SEC / 4];
	i = gain >> 16;
	f = gain & 0xffff;
	inf[1] = t33_simple_intp_u8(i, f, sec + 5);
	inf[2] = t33_simple_intp_u8(i, f, sec + 16);
	inf[3] = t33_simple_intp_u8(i, f, sec + 27);
	inf[4] = t33_simple_intp_u8(i, f, sec + 38);
	inf[5] = t33_simple_intp_u8(i, f, sec + 49);
	inf[6] = t33_simple_intp_u8(i, f, sec + 60);
	inf[7] = t33_simple_intp_u8(i, f, sec + 71);
	inf[8] = t33_simple_intp_u8(i, f, sec + 82);
	inf[9] = t33_simple_intp_u8(i, f, sec + 93);
	inf[10] = t33_simple_intp_u8(i, f, sec + 104);
	inf[11] = t33_simple_intp_u8(i, f, sec + 115);
	ops->para_update(ch);
	if (ops->dma_state(T33_YDNS_PAR_MOD)) {
		*refresh = 2;
		return 0x10000;
	}
	v = (inf[13] & 0xff) | ((inf[14] & 3) << 8) | ((inf[15] & 3) << 12);
	for (k = 0; k < 3; k++)
		ops->reg_write(ch, T33_YDNS_PAR_MOD, cfg_mode ? 4 : 0xe054, v);
	ops->reg_write(ch, T33_YDNS_PAR_MOD, cfg_mode ? 5 : 0xe058,
		       (inf[16] & 0xff) | (inf[19] << 24) |
		       ((inf[17] << 8) & 0xffff) | ((inf[18] & 0xff) << 16));
	ops->reg_write(ch, T33_YDNS_PAR_MOD, cfg_mode ? 6 : 0xe05c,
		       (inf[20] & 0xff) | (inf[23] << 24) |
		       ((inf[21] << 8) & 0xffff) | ((inf[22] & 0xff) << 16));
	ops->reg_write(ch, T33_YDNS_PAR_MOD, cfg_mode ? 3 : 0xe050, inf[12] & 1);
	ops->reg_write(ch, T33_YDNS_PAR_MOD, cfg_mode ? 1 : 0xe010, 1);
	*refresh = 1;
	return 0x10000;
}

/* ---- SDNS, module 20 ---------------------------------------------------- */

#define T33_SDNS_PAR_MOD	20
#define T33_SDNS_PAR_GAIN	148	/* state byte offset: last gain */
#define T33_SDNS_PAR_SEC	144	/* state byte offset: section pointer */

/* tisp_sdns_par_refresh */
static inline u32 t33_sdns_par_refresh(u32 ch, u32 gain, u32 tol, u32 *inf,
				       u8 cfg_mode, const struct t33_par_ops *ops,
				       u8 *refresh)
{
	const u8 *sec;
	u32 last, i, f;

	ch &= 0xff;
	last = inf[T33_SDNS_PAR_GAIN / 4];
	if (last != 0xffffffff) {
		u32 d = gain > last ? gain - last : last - gain;

		if (d < tol)
			return 1;
	}
	inf[T33_SDNS_PAR_GAIN / 4] = gain;
	ops->pre_update(ch);
	sec = (const u8 *)(unsigned long)inf[T33_SDNS_PAR_SEC / 4];
	i = gain >> 16;
	f = gain & 0xffff;
	inf[1] = t33_simple_intp_u8(i, f, sec + 5);
	inf[2] = t33_simple_intp_u8(i, f, sec + 16);
	inf[4] = t33_simple_intp_u8(i, f, sec + 28);
	inf[5] = t33_simple_intp_u8(i, f, sec + 39);
	inf[6] = t33_simple_intp_u8(i, f, sec + 50);
	inf[7] = t33_simple_intp_u8(i, f, sec + 61);
	inf[8] = t33_simple_intp_u8(i, f, sec + 72);
	inf[9] = t33_simple_intp_u8(i, f, sec + 83);
	inf[10] = t33_simple_intp_u8(i, f, sec + 94);
	inf[11] = t33_simple_intp_u8(i, f, sec + 105);
	inf[13] = t33_simple_intp_u8(i, f, sec + 117);
	inf[14] = t33_simple_intp_u8(i, f, sec + 128);
	inf[16] = t33_simple_intp_u8(i, f, sec + 140);
	ops->para_update(ch);
	if (ops->dma_state(T33_SDNS_PAR_MOD)) {
		*refresh = 2;
		return 0x10000;
	}
	ops->reg_write(ch, T33_SDNS_PAR_MOD, cfg_mode ? 5 : 0x13028,
		       (inf[19] & 0xff) | (inf[22] << 24) |
		       ((inf[20] << 8) & 0xffff) | ((inf[21] & 0xff) << 16));
	ops->reg_write(ch, T33_SDNS_PAR_MOD, cfg_mode ? 6 : 0x1302c,
		       (inf[23] & 0xff) | (inf[26] << 24) |
		       ((inf[24] << 8) & 0xffff) | ((inf[25] & 0xff) << 16));
	ops->reg_write(ch, T33_SDNS_PAR_MOD, cfg_mode ? 7 : 0x13030,
		       ((inf[28] << 8) & 0xffff) | ((inf[29] & 0xff) << 16) |
		       (inf[27] & 0x3f) | ((inf[30] & 0x3f) << 24));
	ops->reg_write(ch, T33_SDNS_PAR_MOD, cfg_mode ? 8 : 0x13034, inf[31] & 0x1f);
	ops->reg_write(ch, T33_SDNS_PAR_MOD, cfg_mode ? 9 : 0x13038,
		       (inf[32] & 0xff) | (inf[35] << 24) |
		       ((inf[33] << 8) & 0xffff) | ((inf[34] & 0xff) << 16));
	ops->reg_write(ch, T33_SDNS_PAR_MOD, cfg_mode ? 3 : 0x13020, inf[17] & 3);
	ops->reg_write(ch, T33_SDNS_PAR_MOD, cfg_mode ? 4 : 0x13024, inf[18] & 3);
	ops->reg_write(ch, T33_SDNS_PAR_MOD, cfg_mode ? 7 : 0x13030,
		       ((inf[28] << 8) & 0xffff) | ((inf[29] & 0xff) << 16) |
		       (inf[27] & 0x3f) | ((inf[30] & 0x3f) << 24));
	ops->reg_write(ch, T33_SDNS_PAR_MOD, cfg_mode ? 1 : 0x13010, 1);
	*refresh = 1;
	return 0x10000;
}

#endif /* TX_ISP_T33_PARA_H */
