/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) TMO (tone mapping), module list 8, IQ bin section 4
 * (0x1690 bytes): the list part of tisp_tmo_init.
 *
 *  - tisp_tmo_tmo_size_update: statistics grid ((h + 7) / 15 rows,
 *    (w + 12) / 25 columns), cell reciprocal and shift;
 *  - func_bin_para_update, the EV (or BV, sec[0x1660]) interpolations of
 *    the 11-point tables, tisp_tmo_tmo_lumcurve_ev_interp (201-point
 *    luminance curve between eleven curve sets) and
 *    tisp_tmo_tmo_satcurve_ev_interp (25 points), func_tmo_ic_para_update
 *    (the register copy with the vendor's clamps);
 *  - the slot stores of tisp_tmo_init, tisp_tmo_ramY_vi0_reg_refresh (Y
 *    curve through the 0x32274 port) and tisp_tmo_ramV_vi0_reg_refresh
 *    for the single-sensor path (0x70e0 = the V table DMA address).
 * The list is built with the vendor's short length (tisp_tmo_rcfg_reg_num_disV,
 * 0xc4 words: no V ram in the list).  The V table (tisp_tmo_modechange_stat_refresh
 * pattern packed by tisp_tmo_s2s3_ddrV_init, 1500 words) is written by the
 * caller into the DMA buffer and directly through 0x32260 / 0x32264.
 * The block state is the vendor's tmoInfo word array up to 0xab0.
 * Not here: the curve mode sec[0x1634] = 1 (static curve blend; list not
 * built), the DRC strength (0x2a58, off at init), the per-frame soft process.
 * QEMU trace of the vendor module (openimp #388): list 8 word-equal but for
 * the DMA address.
 */
#ifndef TX_ISP_T33_TMO_H
#define TX_ISP_T33_TMO_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_TMO		8
#define T33_SEC_TMO		4
#define T33_TMO_SEC_BYTES	0x1690U
#define T33_TMO_WORDS		(0xab0U / 4)
#define T33_TMO_V_WORDS		1500U
#define T33_TMO_V_BYTES		(T33_TMO_V_WORDS * 4)

static const struct t33_tcmd t33_tmo_tmpl[] = {
	{ 0x07010, 1, 0 }, { 0x07020, 18, 0 }, { 0x07078, 8, 0 }, { 0x070ec, 4, 0 }, { 0x07068, 13, 1 },
	{ 0x0706c, 4, 1 }, { 0x07070, 4, 1 }, { 0x07074, 2, 1 }, { 0x0707c, 13, 1 }, { 0x0708c, 10, 1 },
	{ 0x070dc, 2, 0 }, { 0x32270, 1, 0 }, { 0x32274, 101, 1 }, { 0x32270, 1, 0 },
};

#define T33_TV(o)	v[(o) / 4]

/* tisp_{ev,bv}_intp_{uint8,int8,uint16}_t: 11 points t (8 / 16 bit) over 11 LE u32 knots e */
static inline int t33_tmo_intp(u32 x, const u8 *e, const u8 *t, int bits, int sgn, int bv)
{
	u32 k, lo, hi, d, a, b;
	unsigned long long f;
	int r;

	for (k = 0; k < 11; k++) {
		u32 kv = t33_le32(e + 4 * k);

		if (bv ? kv < x : x < kv)
			break;
	}
#define T33_TMO_T(i)	(bits == 16 ? (u32)t32_tp_le16(t + 2 * (i)) : sgn ? (u32)(int)(s8)t[i] : (u32)t[i])
	if (k == 11) {
		a = T33_TMO_T(10);
	} else if (!k) {
		a = T33_TMO_T(0);
	} else {
		hi = t33_le32(e + 4 * k);
		lo = t33_le32(e + 4 * k - 4);
		d = bv ? lo - hi : hi - lo;
		if (!d)
			return 2;
		f = (unsigned long long)(bv ? lo - x : x - lo) * 0x1000 + (d >> 1);
		a = T33_TMO_T(k - 1);
		b = T33_TMO_T(k);
		a = (u32)t33_sub_interp((u32)t32_tp_div64(f, (unsigned long long)d), (int)a, (int)b);
	}
#undef T33_TMO_T
	if (bits == 16)
		return (u16)a;
	r = sgn ? (s8)a : (u8)a;
	return r;
}

/* tisp_tmo_log2 */
static inline u32 t33_tmo_log2(u16 a, u16 b)
{
	u32 c = 0;
	u16 s = 2;

	if (a >= b)
		return 0;
	do {
		c++;
		if (b <= (u16)(a + s))
			return c;
		s <<= 1;
	} while (c != 17);
	return c;
}

/*
 * the curve blend of tisp_tmo_tmo_{lum,sat}curve_ev_interp: f and the set
 * index k from the EV knots (rising, 0x1664) or the BV knots (falling, 0x1608)
 */
static inline void t33_tmo_curve_pick(const u8 *s, u32 ev, u32 evhi, u32 bv, u32 *f, u32 *k)
{
	u32 i, a, b;

	*f = 0;
	*k = 10;
	if (!s[0x1660]) {
		for (i = 0; i < 10; i++) {
			a = t33_le32(s + 0x1664 + 4 * i);
			b = t33_le32(s + 0x1668 + 4 * i);
			if (!evhi && a <= ev && ev < b) {
				*f = ((b - ev) * 0x100) / (b - a) & 0xffff;
				*k = i;
				break;
			}
		}
		if (evhi || t33_le32(s + 0x168c) <= ev) {
			*f = 0;
			*k = 10;
		}
		if (!evhi && ev < t33_le32(s + 0x1664)) {
			*f = 0x100;
			*k = 0;
		}
	} else {
		for (i = 0; i < 10; i++) {
			a = t33_le32(s + 0x1608 + 4 * i);
			b = t33_le32(s + 0x160c + 4 * i);
			if (bv <= a && b < bv) {
				*f = ((a - bv) * 0x100) / (a - b) & 0xffff;
				*k = i;
				break;
			}
		}
		if (bv <= t33_le32(s + 0x1630)) {
			*f = 0;
			*k = 10;
		}
		if (t33_le32(s + 0x1608) < bv) {
			*f = 0x100;
			*k = 0;
		}
	}
	if (*k > 9)
		*k = 9;
}

/* the V table (tisp_tmo_modechange_stat_refresh + tisp_tmo_s2s3_ddrV_init): 375 rows of 4 words */
static inline void t33_tmo_vtable(u32 *t)
{
	static const u16 p[10] = { 0, 0x19a, 0x333, 0x4cd, 0x666, 0x800, 0x999, 0xb33, 0xccc, 0xe66 };
	u32 r, h, w[4];

	for (h = 0; h < 2; h++) {
		const u16 *a = p + 5 * h;

		w[2 * h] = (a[1] & 0xfffU) << 12 | (u32)a[2] << 24 | (a[0] & 0xfffU);
		w[2 * h + 1] = (a[2] & 0xfffU) >> 8 | (a[3] & 0xfffU) << 4 | (a[4] & 0xfffU) << 16;
	}
	for (r = 0; r < T33_TMO_V_WORDS; r++)
		t[r] = w[r & 3];
}

/*
 * Block init into list l (capacity cap words): s section 4, w / h the
 * sensor size, ev the tisp init EV, vdma the physical address of the V
 * table, v T33_TMO_WORDS words of block state.  Returns the list length,
 * -1 for the static curve mode.
 */
#ifdef __KERNEL__
#define T33_TMO_FN	static noinline_for_stack __maybe_unused int
#else
#define T33_TMO_FN	static inline int
#endif
T33_TMO_FN t33_tmo_init(const u8 *s, u32 w, u32 h, unsigned long long ev, u32 vdma, u32 *v, u32 *l, u32 cap)
{
	static const struct { u16 dst, src; u8 bits, sgn; } ip[] = {
		{ 0x10, 0x0c, 8, 0 }, { 0x14, 0x17, 8, 0 }, { 0x18, 0x22, 8, 0 }, { 0x1c, 0x2d, 8, 0 },
		{ 0x20, 0x38, 8, 0 }, { 0x24, 0x43, 8, 0 }, { 0x28, 0x4e, 8, 1 }, { 0x2c, 0x59, 8, 1 },
		{ 0x30, 0x64, 16, 0 }, { 0x34, 0x7a, 16, 0 }, { 0x38, 0x90, 8, 0 }, { 0x3c, 0x9b, 8, 0 },
		{ 0x40, 0xa6, 8, 0 }, { 0x44, 0xb1, 8, 0 }, { 0x48, 0xbc, 8, 0 }, { 0x4c, 0xc7, 8, 0 },
		{ 0x50, 0xd2, 16, 0 }, { 0x54, 0xe8, 16, 0 }, { 0x58, 0xfe, 16, 0 }, { 0x5c, 0x114, 16, 0 },
		{ 0x60, 0x12a, 16, 0 }, { 0x64, 0x140, 16, 0 }, { 0x7c, 0x160, 16, 0 }, { 0x80, 0x176, 16, 0 },
		{ 0x84, 0x18c, 16, 0 }, { 0x4e8, 0x1570, 16, 0 }, { 0x4ec, 0x1586, 16, 0 },
		{ 0x4f4, 0x159d, 8, 0 }, { 0x548, 0x15d0, 8, 0 }, { 0x54c, 0x15db, 8, 0 },
		{ 0x550, 0x15e6, 8, 0 }, { 0x554, 0x15f2, 16, 0 },
	};
	static const u16 cp1[][2] = {
		{ 0x578, 0x20 }, { 0x57c, 0x24 }, { 0x580, 0x28 }, { 0x584, 0x2c }, { 0x588, 0x30 }, { 0x58c, 0x34 },
		{ 0x590, 0x38 }, { 0x594, 0x3c }, { 0x598, 0x40 }, { 0x59c, 0x44 }, { 0x5a0, 0x48 }, { 0x5a4, 0x4c },
		{ 0x5c0, 0x68 }, { 0x5d0, 0x78 }, { 0x5dc, 0x84 }, { 0x5e0, 0x88 }, { 0x6a4, 0x14c }, { 0x6a8, 0x150 },
		{ 0x6ac, 0x154 }, { 0x6b0, 0x158 }, { 0xa3c, 0x4e4 }, { 0xa40, 0x4e8 }, { 0xa44, 0x4ec },
		{ 0xa48, 0x4f0 }, { 0xa4c, 0x4f4 }, { 0xaa0, 0x548 }, { 0xaa4, 0x54c }, { 0xaa8, 0x550 },
	};
	static const u16 cpn[][3] = {
		{ 0x568, 0x10, 2 }, { 0x570, 0x18, 2 }, { 0x5a8, 0x50, 2 }, { 0x5b0, 0x58, 2 }, { 0x5b8, 0x60, 2 },
		{ 0x5c4, 0x6c, 3 }, { 0x5d4, 0x7c, 2 }, { 0x5e4, 0x8c, 25 }, { 0x648, 0xf0, 8 }, { 0x668, 0x110, 8 },
		{ 0x688, 0x130, 7 }, { 0x6b4, 0x15c, 25 }, { 0xa50, 0x4f8, 10 }, { 0xa78, 0x520, 10 },
		{ 0x718, 0x1c0, 201 },
	};
	u32 x, bv = 0, evhi, f, k, n, i, c, a, b;
	int isbv, len;

	if (!w || !h || t32_tp_le16(s + 0x1634) != 0)
		return -1;
	len = t33_rcfg_tmpl_init(l, cap, t33_tmo_tmpl, ARRAY_SIZE(t33_tmo_tmpl));
	if (len <= 0)
		return len;
	for (i = 0; i < T33_TMO_WORDS; i++)
		v[i] = 0;
	x = (u32)(ev >> 10);
	evhi = (u32)(ev >> 42);
	/* tisp_tmo_tmo_size_update */
	v[0] = (h + 7) / 15;
	v[1] = (w + 12) / 25;
	n = v[0] * v[1];
	for (c = 0xd, a = 0x2000; c <= 0x19; c++, a <<= 1)
		if (n < (0x8U << (c - 0xd)))
			break;
	if (c <= 0x19) {
		v[2] = ((n >> 1) + a) / n;
		v[3] = c;
	}
	T33_SW(l, 3, (v[1] & 0xff) << 8 | (v[2] & 0xfff) << 16 | (v[0] & 0xff));
	T33_SW(l, 4, v[3] & 0x1f);
	T33_SW(l, 1, 0xffffffffU);
	/* func_bin_para_update */
	T33_TV(0x68) = s[0x156];
	T33_TV(0x6c) = t32_tp_le16(s + 0x158);
	T33_TV(0x70) = t32_tp_le16(s + 0x15a);
	T33_TV(0x74) = t32_tp_le16(s + 0x15c);
	T33_TV(0x78) = s[0x15e];
	T33_TV(0x88) = s[0x1a2];
	for (i = 0; i < 16; i++)
		T33_TV(0xf0 + 4 * i) = t32_tp_le16(s + 0x3ca + 2 * i);
	for (i = 0; i < 7; i++)
		T33_TV(0x130 + 4 * i) = t33_tmo_log2(t32_tp_le16(s + 0x3ca + 2 * i), t32_tp_le16(s + 0x3cc + 2 * i));
	T33_TV(0x14c) = s[0x3f1];
	T33_TV(0x150) = s[0x3f2];
	T33_TV(0x154) = s[0x3f3];
	T33_TV(0x158) = t32_tp_le16(s + 0x3f4);
	T33_TV(0x4e4) = s[0x156e];
	T33_TV(0x4f0) = s[0x159c];
	for (i = 0; i < 20; i++)
		T33_TV(0x4f8 + 4 * i) = t32_tp_le16(s + 0x15a8 + 2 * i);
	for (i = 0; i < 25; i++)
		T33_TV(0x15c + 4 * i) = t32_tp_le16(s + 0x3f6 + 2 * i);
	/* EV / BV interpolations */
	isbv = s[0x1660] != 0;
	for (i = 0; i < ARRAY_SIZE(ip); i++)
		T33_TV(ip[i].dst) = (u32)t33_tmo_intp(isbv ? bv : x, s + (isbv ? 0x1608 : 0x1664), s + ip[i].src,
						      ip[i].bits, ip[i].sgn, isbv);
	/* luminance curve (0x1c0 words, the Y ram) and saturation curve (0x8c) */
	{
		static const u16 lt[11] = { 0x124a, 0x13dc, 0x428, 0x5ba, 0x74c, 0x8de, 0xa70, 0xc02, 0xd94, 0xf26,
					    0x10b8 };

		t33_tmo_curve_pick(s, x, evhi, bv, &f, &k);
		for (i = 0; i < 201; i++) {
			a = t32_tp_le16(s + lt[k] + 2 * i);
			b = t32_tp_le16(s + lt[k + 1] + 2 * i);
			T33_TV(0x1c0 + 4 * i) = (u32)((int)(a * f + (0x100 - f) * b) / 0x100) & 0xffff;
		}
		for (i = 0; i < 25; i++) {
			a = t32_tp_le16(s + 0x1a4 + 0x32 * k + 2 * i);
			b = t32_tp_le16(s + 0x1d6 + 0x32 * k + 2 * i);
			T33_TV(0x8c + 4 * i) = (u32)((int)(a * f + (0x100 - f) * b) / 0x100) & 0xffff;
		}
	}
	/* func_tmo_ic_para_update */
	for (i = 0; i < ARRAY_SIZE(cp1); i++)
		T33_TV(cp1[i][0]) = T33_TV(cp1[i][1]);
	for (i = 0; i < ARRAY_SIZE(cpn); i++)
		for (k = 0; k < cpn[i][2]; k++)
			T33_TV(cpn[i][0] + 4 * k) = T33_TV(cpn[i][1] + 4 * k);
	if ((int)T33_TV(0x5a8) < 0x80)
		T33_TV(0x5a8) = 0x80;
	a = 1U << (T33_TV(0x64) & 0x1f);
	b = 1U << (T33_TV(0x54) & 0x1f);
	T33_TV(0x5bc) = a + T33_TV(0x5b8);
	T33_TV(0x5ac) = b + T33_TV(0x5a8);
	if ((int)T33_TV(0x5bc) > 0xfff) {
		T33_TV(0x5bc) = 0xfff;
		T33_TV(0x5b8) = 0xfff - a;
	}
	if ((int)T33_TV(0x5ac) >= 0x10000) {
		T33_TV(0x5ac) = 0xffff;
		T33_TV(0x5a8) = 0xffff - b;
	}
	if ((int)T33_TV(0x5ac) - 0x80 < (int)T33_TV(0x5b4))
		T33_TV(0x5b4) = T33_TV(0x5ac) - 0x80;
	T33_TV(0x5b0) = 0;
	/* the slot stores of tisp_tmo_init */
#define TT(o, m)	(T33_TV(o) & (m))
	T33_SW(l, 5, TT(0x56c, 0x7f) << 16 | TT(0x568, 0x7f));
	T33_SW(l, 6, TT(0x574, 0x7f) << 16 | TT(0x570, 0x7f));
	T33_SW(l, 7, TT(0x57c, 0xff) << 8 | TT(0x580, 0x7f) << 16 | TT(0x578, 0xff) | TT(0x584, 0x7f) << 24);
	T33_SW(l, 8, TT(0x588, 0x1fff) << 16 | TT(0x58c, 0x1fff));
	T33_SW(l, 9, TT(0x590, 0x7f) | T33_TV(0x59c) << 24 | TT(0x594, 0x7f) << 8 | TT(0x598, 0xff) << 16);
	T33_SW(l, 10, TT(0x5a4, 0xff) << 8 | TT(0x5a0, 0xff));
	T33_SW(l, 0xb, TT(0x5a8, 0xffff) | T33_TV(0x5ac) << 16);
	T33_SW(l, 0xc, TT(0x5b0, 0xffff) | T33_TV(0x5b4) << 16);
	T33_SW(l, 0xd, TT(0x5bc, 0xfff) << 16 | TT(0x5b8, 0xfff));
	T33_SW(l, 0x12, TT(0x5d8, 0xfff) << 12 | TT(0x5d4, 0xfff));
	T33_SW(l, 0x13, TT(0x5dc, 0x3ff));
	for (i = 0; i < 12; i++)
		T33_SW(l, 0x24 + i, TT(0x5e8 + 8 * i, 0x7ff) << 16 | TT(0x5e4 + 8 * i, 0x7ff));
	T33_SW(l, 0x30, TT(0x644, 0x7ff));
	T33_SW(l, 0x19, TT(0xa40, 0xfff) | T33_TV(0xa4c) << 28 | TT(0xa44, 0xfff) << 16);
	T33_SW(l, 0x1c, TT(0xaa4, 0xf) << 4 | TT(0xaa0, 0xf));
	T33_SW(l, 0x1d, TT(0xaa8, 1));
	/* 0x2a5c = 0 at init: the eight 0x7070 gains 0x100 */
	for (i = 0; i < 8; i++)
		T33_TV(0x668 + 4 * i) = 0x100;
	T33_SW(l, 0xe, TT(0x5c8, 0xfff) << 16 | TT(0x5c4, 0xfff));
	T33_SW(l, 0xf, TT(0x5cc, 0xfff));
	T33_SW(l, 0x10, TT(0x5c0, 3));
	T33_SW(l, 0x11, TT(0x5d0, 3) << 4);
	T33_SW(l, 0x14, TT(0x5e0, 7));
	for (i = 0; i < 4; i++) {
		T33_SW(l, 0x32 + i, TT(0x64c + 8 * i, 0x1fff) << 16 | TT(0x648 + 8 * i, 0x1fff));
		T33_SW(l, 0x37 + i, TT(0x66c + 8 * i, 0x1ff) << 16 | TT(0x668 + 8 * i, 0x1ff));
	}
	T33_SW(l, 0x3c, TT(0x68c, 0xf) << 8 | TT(0x690, 0xf) << 16 | TT(0x688, 0xf) | TT(0x694, 0xf) << 23);
	T33_SW(l, 0x3d, TT(0x69c, 0xf) << 8 | TT(0x6a0, 0xf) << 16 | TT(0x698, 0xf));
	T33_SW(l, 0x16, TT(0x6a8, 1) << 4 | TT(0x6ac, 1) << 8 | TT(0x6a4, 1) | TT(0x6b0, 0xfff) << 12);
	T33_SW(l, 0x18, TT(0xa3c, 1));
	T33_SW(l, 0x1a, TT(0xa48, 1));
	for (i = 0; i < 10; i++)
		T33_SW(l, 0x4d + i, TT(0xa54 + 8 * i, 0xfff) << 16 | TT(0xa50 + 8 * i, 0xfff));
	for (i = 0; i < 12; i++)
		T33_SW(l, 0x3f + i, TT(0x6b8 + 8 * i, 0xfff) << 16 | TT(0x6b4 + 8 * i, 0xfff));
	T33_SW(l, 0x4b, TT(0x714, 0xfff));
	/* tisp_tmo_ramY_vi0_reg_refresh: the Y curve (201 points) */
	T33_SW(l, 0x5b, 0x101);
	for (i = 0; i < 100; i++)
		T33_SW(l, 0x5d + i, TT(0x1c0 + 8 * i + 4, 0x1fff) << 16 | TT(0x1c0 + 8 * i, 0x1fff));
	T33_SW(l, 0x5d + 100, TT(0x1c0 + 800, 0x1fff) << 16 | TT(0x1c0 + 800, 0x1fff));
	T33_SW(l, 0xc3, 0x10102);
	/* tisp_tmo_ramV_vi0_reg_refresh, single sensor: V table by DMA */
	T33_SW(l, 0x22, 0);
	T33_SW(l, 0x1f, 0);
	T33_SW(l, 0x59, vdma);
	T33_SW(l, 0x20, 1);
#undef TT
	return len;
}

#undef T33_TV

#endif /* TX_ISP_T33_TMO_H */
