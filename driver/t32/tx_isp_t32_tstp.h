/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 time stamp / marker block (TSTP, registers 0x33000 .. 0x330a8):
 * readable C of the vendor's tisp_tstp_init, _deinit, _reg_cfg,
 * _mark1_func, _mark2_func, _param_array_get / _set and _wake_up.
 *
 * The vendor keeps one 154 B parameter block (vmalloc, pointer t32_data_e28c0)
 * made of u16 fields with some u32 words at 2-byte aligned offsets (the
 * vendor reads those with lwl/lwr; little endian).  tisp_tstp_reg_cfg writes
 * the whole block to the hardware, register by register; the two marker
 * functions update a part of it and write only that part.
 *
 * Register writes go straight to system_reg_write (no shadow mode); the OS
 * calls are hooks (struct t32_tstp_ops).  Checked against Matt Davis'
 * recovered C (PR #15 @ 2e2e06bc) by tests/t32_oracle (t32_tstp_oracle).
 *
 * Vendor UB, not reproduced: tisp_tstp_mark1_func copies n1 / n2 u16 values
 * into two 16-entry tables and then zero-fills the rest with a loop that
 * ends only on equality; n > 16 overruns the block (and does not end).
 * Ours refuses n > 16 (-1, no effect).  tisp_tstp_init does not check the
 * vmalloc result (memset of NULL); ours returns -1 then.
 */
#ifndef TX_ISP_T32_TSTP_H
#define TX_ISP_T32_TSTP_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_TSTP_PAR_SZ		154
#define T32_TSTP_REG		0x33000U
#define T32_TSTP_TAB_N		16	/* entries of each marker-1 table */

/* parameter block offsets (bytes); W = u32 word, else u16 (B = only the low byte is used) */
enum {
	T32_TSTP_W_CTRL = 0,		/* -> 0x33000 */
	T32_TSTP_H_C0 = 4,		/* 0x33004 fields: 4, 6, 8, 10, 12, 14, 16 */
	T32_TSTP_B_P0 = 18,		/* 0x33008: bytes 18 / 22, u16 20 / 24 */
	T32_TSTP_B_P1 = 26,		/* 0x3300c: bytes 26 / 30, u16 28 / 32 */
	T32_TSTP_W_A = 34,		/* -> 0x33010 */
	T32_TSTP_H_A = 38,		/* 0x33014 fields: 38, 40, 42, 44, 46 */
	T32_TSTP_W_B = 48,		/* -> 0x33020 */
	T32_TSTP_H_B = 52,		/* 0x33024 fields: 52 .. 60 */
	T32_TSTP_H_M0 = 62,		/* 0x33028 fields: 62, 64, 66 */
	T32_TSTP_H_EN0 = 68,		/* 0x33030 fields: 68, 70 (mark 1), 72 (mark 2) */
	T32_TSTP_H_EN1 = 70,
	T32_TSTP_H_EN2 = 72,
	T32_TSTP_H_F0 = 74,		/* 0x33040 fields: 74, 76 */
	T32_TSTP_TAB1 = 78,		/* marker-1 table 1, 16 x u16 */
	T32_TSTP_TAB2 = 110,		/* marker-1 table 2, 16 x u16 */
	T32_TSTP_H_M1 = 142,		/* -> 0x33090 (2 bits) */
	T32_TSTP_M2 = 144,		/* marker-2 values, 4 x u16 -> 0x330a0 / 0x330a4 */
	T32_TSTP_H_M2 = 152,		/* -> 0x330a8 (2 bits) */
};

struct t32_tstp_ops {
	void *(*vmalloc)(u32 size);
	void (*vfree)(void *p);
	void (*reg_write)(u32 reg, u32 val);	/* system_reg_write */
};

struct t32_tstp {
	u8 *par;				/* t32_data_e28c0 */
	const struct t32_tstp_ops *ops;
};

static inline u32 t32_tstp_h(const u8 *p, u32 off)
{
	return (u32)p[off] | (u32)p[off + 1] << 8;
}

static inline u32 t32_tstp_w(const u8 *p, u32 off)
{
	return t32_tstp_h(p, off) | t32_tstp_h(p, off + 2) << 16;
}

static inline void t32_tstp_set_h(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
}

static inline void t32_tstp_wr(const struct t32_tstp *t, u32 reg, u32 val)
{
	t->ops->reg_write(T32_TSTP_REG + reg, val);
}

/* 0x33030: the three enables */
static inline u32 t32_tstp_en(const u8 *p)
{
	return (t32_tstp_h(p, T32_TSTP_H_EN0) & 1) | (t32_tstp_h(p, T32_TSTP_H_EN1) & 1) << 4 |
	       (t32_tstp_h(p, T32_TSTP_H_EN2) & 1) << 8;
}

/* 0x33014 / 0x33024: 4-bit, 4-bit, 1-bit, 3-bit, 3-bit fields */
static inline u32 t32_tstp_area(const u8 *p, u32 o)
{
	return (t32_tstp_h(p, o) & 0xf) | (t32_tstp_h(p, o + 2) & 0xf) << 8 |
	       (t32_tstp_h(p, o + 4) & 1) << 12 | (t32_tstp_h(p, o + 6) & 7) << 16 |
	       (t32_tstp_h(p, o + 8) & 7) << 20;
}

/* 0x33008 / 0x3300c: byte, u16 << 8 (16 bits), byte << 16, u16 << 24 */
static inline u32 t32_tstp_pos(const u8 *p, u32 o)
{
	return p[o] | ((t32_tstp_h(p, o + 2) << 8) & 0xffff) | (u32)p[o + 4] << 16 |
	       t32_tstp_h(p, o + 6) << 24;
}

/* 0x33050 .. 0x3308c: the two marker-1 tables as 16 words */
static inline void t32_tstp_wr_tabs(const struct t32_tstp *t)
{
	u32 k;

	for (k = 0; k < 16; k++)
		t32_tstp_wr(t, 0x50 + 4 * k, t32_tstp_w(t->par, T32_TSTP_TAB1 + 4 * k));
}

/* 0x330a0 .. 0x330a8: the marker-2 values */
static inline void t32_tstp_wr_m2(const struct t32_tstp *t)
{
	t32_tstp_wr(t, 0xa0, t32_tstp_w(t->par, T32_TSTP_M2));
	t32_tstp_wr(t, 0xa4, t32_tstp_w(t->par, T32_TSTP_M2 + 4));
	t32_tstp_wr(t, 0xa8, t32_tstp_h(t->par, T32_TSTP_H_M2) & 3);
}

/* tisp_tstp_reg_cfg (0x88800): the whole block, in the vendor's order */
static inline int t32_tstp_reg_cfg(const struct t32_tstp *t)
{
	const u8 *p = t->par;

	t32_tstp_wr(t, 0x00, t32_tstp_w(p, T32_TSTP_W_CTRL));
	t32_tstp_wr(t, 0x04, (t32_tstp_h(p, 4) & 0xf) | ((t32_tstp_h(p, 6) << 4) & 0xff) |
			     (t32_tstp_h(p, 8) & 0xf) << 8 | (t32_tstp_h(p, 10) & 1) << 12 |
			     (t32_tstp_h(p, 12) & 7) << 16 | (t32_tstp_h(p, 14) & 7) << 20 |
			     (t32_tstp_h(p, 16) & 1) << 24);
	t32_tstp_wr(t, 0x08, t32_tstp_pos(p, T32_TSTP_B_P0));
	t32_tstp_wr(t, 0x0c, t32_tstp_pos(p, T32_TSTP_B_P1));
	t32_tstp_wr(t, 0x10, t32_tstp_w(p, T32_TSTP_W_A));
	t32_tstp_wr(t, 0x14, t32_tstp_area(p, T32_TSTP_H_A));
	t32_tstp_wr(t, 0x20, t32_tstp_w(p, T32_TSTP_W_B));
	t32_tstp_wr(t, 0x24, t32_tstp_area(p, T32_TSTP_H_B));
	t32_tstp_wr(t, 0x28, (t32_tstp_h(p, 62) & 1) | (t32_tstp_h(p, 64) & 3) << 4 |
			     (t32_tstp_h(p, 66) & 3) << 8);
	t32_tstp_wr(t, 0x30, t32_tstp_en(p));
	t32_tstp_wr(t, 0x40, (t32_tstp_h(p, T32_TSTP_H_F0) & 7) | (t32_tstp_h(p, 76) & 3) << 4);
	t32_tstp_wr_tabs(t);
	t32_tstp_wr(t, 0x90, t32_tstp_h(p, T32_TSTP_H_M1) & 3);
	t32_tstp_wr_m2(t);
	return 0;
}

/* tisp_tstp_init (0x88f5c): zeroed block, written to the hardware */
static inline int t32_tstp_init(struct t32_tstp *t)
{
	t->par = t->ops->vmalloc(T32_TSTP_PAR_SZ);
	if (!t->par)
		return -1;	/* vendor: memset of NULL */
	memset(t->par, 0, T32_TSTP_PAR_SZ);
	t32_tstp_reg_cfg(t);
	return 0;
}

/* tisp_tstp_deinit (0x88fb4) */
static inline int t32_tstp_deinit(struct t32_tstp *t)
{
	if (t->par) {
		t->ops->vfree(t->par);
		t->par = NULL;
	}
	return 0;
}

/* tisp_tstp_wake_up (0x8907c): rewrite the block when it exists */
static inline int t32_tstp_wake_up(const struct t32_tstp *t)
{
	if (!t->par)
		return 0;
	return t32_tstp_reg_cfg(t);
}

/* tisp_tstp_param_array_get (0x88ff4): copy out the block, size 154 */
static inline int t32_tstp_param_array_get(const struct t32_tstp *t, void *buf, u32 *size)
{
	memcpy(buf, t->par, T32_TSTP_PAR_SZ);
	*size = T32_TSTP_PAR_SZ;
	return 0;
}

/* tisp_tstp_param_array_set (0x89038): copy in the block and write it */
static inline int t32_tstp_param_array_set(const struct t32_tstp *t, const void *buf)
{
	memcpy(t->par, buf, T32_TSTP_PAR_SZ);
	t32_tstp_reg_cfg(t);
	return 0;
}

/* one marker-1 table: n values, the rest of the 16 entries zero */
static inline void t32_tstp_fill_tab(u8 *p, u32 off, const u16 *src, u32 n)
{
	u32 k;

	for (k = 0; k < T32_TSTP_TAB_N; k++)
		t32_tstp_set_h(p, off + 2 * k, k < n ? src[k] : 0);
}

/*
 * tisp_tstp_mark1_func (0x88c20): enable 1, the 2-bit mode at 142 and the two
 * tables (n1 values of tab1, n2 of tab2); writes 0x33030, the tables, 0x33090.
 */
static inline int t32_tstp_mark1(const struct t32_tstp *t, u32 en, u32 mode, u32 n1, const u16 *tab1,
				 u32 n2, const u16 *tab2)
{
	u8 *p = t->par;

	if (!p)
		return -1;
	if (n1 > T32_TSTP_TAB_N || n2 > T32_TSTP_TAB_N)
		return -1;	/* vendor: overrun, see the header comment */
	t32_tstp_set_h(p, T32_TSTP_H_M1, mode);
	t32_tstp_set_h(p, T32_TSTP_H_EN1, en);
	t32_tstp_fill_tab(p, T32_TSTP_TAB1, tab1, n1);
	t32_tstp_fill_tab(p, T32_TSTP_TAB2, tab2, n2);
	t32_tstp_wr(t, 0x30, t32_tstp_en(p));
	t32_tstp_wr_tabs(t);
	t32_tstp_wr(t, 0x90, t32_tstp_h(p, T32_TSTP_H_M1) & 3);
	return 0;
}

/*
 * tisp_tstp_mark2_func (0x88e7c): enable 2, the 2-bit mode at 152 and the
 * four marker-2 values; writes 0x33030 and 0x330a0 .. 0x330a8.
 */
static inline int t32_tstp_mark2(const struct t32_tstp *t, u32 en, u32 mode, u32 v0, u32 v1, u32 v2, u32 v3)
{
	u8 *p = t->par;

	if (!p)
		return -1;
	t32_tstp_set_h(p, T32_TSTP_H_M2, mode);
	t32_tstp_set_h(p, T32_TSTP_H_EN2, en);
	t32_tstp_set_h(p, T32_TSTP_M2, v0);
	t32_tstp_set_h(p, T32_TSTP_M2 + 2, v1);
	t32_tstp_set_h(p, T32_TSTP_M2 + 4, v2);
	t32_tstp_set_h(p, T32_TSTP_M2 + 6, v3);
	t32_tstp_wr(t, 0x30, t32_tstp_en(p));
	t32_tstp_wr_m2(t);
	return 0;
}

#endif /* TX_ISP_T32_TSTP_H */
