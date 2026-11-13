/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AF statistics, module list 31, IQ bin section 24 (0x320
 * bytes): tisp_af_init -> tisp_af_malloc_cfg (statistics buffer 0x6000
 * bytes in four parts at 0x18108..0x18114, 0x18118 = 0x100df),
 * tisp_af_par_init (window sizes from the sensor size, the IIR / FIR filter
 * sets picked by the section's indices, clamped as the vendor does, into
 * the vendor's parameter block) and tisp_af_t33_all_reg_write.  The block
 * is kept in the vendor's byte layout (a[], offsets of the vendor's
 * af_info + 0x404) so each store can be compared with the decompile.
 * Only the path with sec[0x300] = 0 (indices from the section); the
 * gain-indexed path is refused (list not built).  QEMU trace of the vendor
 * module (openimp #388): list 31 word-equal but for the DMA addresses.
 */
#ifndef TX_ISP_T33_AF_H
#define TX_ISP_T33_AF_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_AF		31
#define T33_SEC_AF		24
#define T33_AF_SEC_BYTES	0x320U
#define T33_AF_DMA_BYTES	0x6000U

static const struct t33_tcmd t33_af_tmpl[] = {
	{ 0x18010, 1, 0 }, { 0x18040, 2, 0 }, { 0x18074, 42, 0 }, { 0x18124, 3, 0 },
};

/* tisp_af_par_init filter tables (vendor iir: 21 x 12, fir: 45 x 5; as u16) */
static const u16 t33_af_iir[21][12] = {
	{ 33, 256, 0, 65280, 501, 65291, 256, 0, 65280, 417, 65357, 33 },
	{ 36, 256, 0, 65280, 444, 65275, 256, 0, 65280, 378, 65341, 36 },
	{ 36, 256, 0, 65280, 367, 65326, 256, 0, 65280, 286, 65336, 36 },
	{ 36, 256, 0, 65280, 257, 65328, 256, 0, 65280, 162, 65334, 36 },
	{ 36, 256, 0, 65280, 21, 65332, 256, 0, 65280, 123, 65330, 36 },
	{ 36, 256, 0, 65280, 65515, 65332, 256, 0, 65280, 65413, 65430, 36 },
	{ 66, 256, 0, 65280, 511, 65281, 256, 0, 65280, 294, 65429, 66 },
	{ 66, 256, 0, 65280, 422, 65342, 256, 0, 65280, 262, 65397, 66 },
	{ 66, 236, 0, 65280, 335, 65357, 256, 0, 65280, 162, 65384, 66 },
	{ 66, 256, 0, 65280, 244, 65365, 256, 0, 65280, 37, 65377, 66 },
	{ 66, 256, 0, 65280, 65440, 65375, 256, 0, 65280, 96, 65372, 66 },
	{ 92, 256, 0, 65280, 511, 65281, 256, 0, 65280, 192, 65466, 92 },
	{ 93, 256, 0, 65280, 410, 65355, 256, 0, 65280, 148, 65438, 93 },
	{ 93, 256, 0, 65280, 313, 65381, 256, 0, 65280, 46, 65421, 93 },
	{ 93, 256, 0, 65280, 200, 65396, 256, 0, 65280, 65459, 65408, 93 },
	{ 116, 256, 0, 65280, 511, 65281, 256, 0, 65280, 95, 65486, 116 },
	{ 116, 256, 0, 65280, 404, 65362, 256, 0, 65280, 37, 65462, 116 },
	{ 116, 256, 0, 65280, 298, 65400, 256, 0, 65280, 65470, 65422, 116 },
	{ 138, 256, 0, 65280, 511, 65281, 256, 0, 65280, 0, 65492, 138 },
	{ 139, 256, 0, 65280, 400, 65367, 256, 0, 65280, 65464, 65470, 139 },
	{ 160, 256, 0, 65280, 511, 65281, 256, 0, 65280, 65422, 65486, 160 },
};
static const u16 t33_af_fir[45][5] = {
	{ 56, 126, 158, 126, 56 },
	{ 44, 145, 201, 145, 44 },
	{ 0, 162, 283, 162, 0 },
	{ 65465, 118, 322, 118, 65465 },
	{ 65430, 38, 298, 38, 65430 },
	{ 65430, 65498, 298, 65498, 65430 },
	{ 65465, 65418, 322, 65418, 65465 },
	{ 0, 65374, 283, 65374, 0 },
	{ 44, 65391, 201, 65391, 44 },
	{ 51, 134, 176, 134, 51 },
	{ 27, 157, 242, 157, 27 },
	{ 65501, 148, 316, 148, 65501 },
	{ 65445, 78, 316, 78, 65445 },
	{ 65430, 0, 299, 0, 65430 },
	{ 65445, 65458, 316, 65458, 65445 },
	{ 65501, 65388, 316, 65388, 65501 },
	{ 27, 65379, 242, 65379, 27 },
	{ 40, 145, 208, 145, 40 },
	{ 0, 159, 287, 159, 0 },
	{ 65473, 117, 331, 117, 65473 },
	{ 65438, 38, 314, 38, 65438 },
	{ 65438, 65498, 314, 65498, 65438 },
	{ 65473, 65419, 331, 65419, 65473 },
	{ 0, 65377, 287, 65377, 0 },
	{ 22, 154, 250, 154, 22 },
	{ 65507, 145, 324, 145, 65507 },
	{ 65458, 79, 336, 79, 65458 },
	{ 65442, 0, 325, 0, 65442 },
	{ 65458, 65457, 336, 65457, 65458 },
	{ 65507, 65391, 324, 65391, 65507 },
	{ 0, 153, 296, 153, 0 },
	{ 65486, 116, 349, 116, 65486 },
	{ 65456, 40, 347, 40, 65456 },
	{ 65456, 65496, 347, 65496, 65456 },
	{ 65486, 65420, 349, 65420, 65486 },
	{ 65516, 138, 337, 138, 65516 },
	{ 65479, 80, 370, 80, 65479 },
	{ 65465, 0, 370, 0, 65465 },
	{ 65479, 65456, 370, 65456, 65479 },
	{ 65505, 112, 374, 112, 65505 },
	{ 65483, 41, 398, 41, 65483 },
	{ 65483, 65495, 398, 65495, 65483 },
	{ 65506, 78, 415, 78, 65506 },
	{ 65497, 0, 435, 0, 65497 },
	{ 65517, 41, 464, 41, 65517 },
};

/* list slot of an AF register (layout above) */
static inline unsigned int t33_af_slot(u32 reg)
{
	if (reg == 0x18010)
		return 1;
	if (reg < 0x18074)
		return 3 + (reg - 0x18040) / 4;
	if (reg < 0x18124)
		return 6 + (reg - 0x18074) / 4;
	return 49 + (reg - 0x18124) / 4;
}

#define T33_AF_W(l, reg, x)	T33_SW(l, t33_af_slot(reg), x)

static inline void t33_af_p16(u8 *a, u32 o, u32 v)
{
	a[o] = (u8)v;
	a[o + 1] = (u8)(v >> 8);
}

static inline void t33_af_sp16(u8 *s, u32 o, u32 v)
{
	s[o] = (u8)v;
	s[o + 1] = (u8)(v >> 8);
}

/* sum of (n - i) for i < k - 1 (16 bit) */
static inline u32 t33_af_tri(int k, u32 n)
{
	u32 v = 0;
	int i;

	for (i = 0; i < k - 1; i++)
		v = (v + n - (u32)i) & 0xffff;
	return v;
}

/*
 * Block init into list l: s a writable copy of section 24 (the window sizes
 * and clamped indices are written into it), w / h the sensor size, dma the
 * physical address of the statistics buffer (T33_AF_DMA_BYTES, 8-byte
 * aligned).  Returns the list length, -1 when not built.
 */
static inline int t33_af_init(u8 *s, u32 w, u32 h, u32 dma, u32 *l, u32 cap)
{
	static u8 a[0xe0];
#define SS(o)	((u32)t32_tp_le16(s + (o)))
#define A16(o)	((u32)t32_tp_le16(a + (o)))
#define A32(o)	((u32)a[o] | (u32)a[(o) + 1] << 8 | (u32)a[(o) + 2] << 16 | (u32)a[(o) + 3] << 24)
	const u16 *fa, *fb, *ic, *id;
	u32 nx = SS(0x56), ny = SS(0x52), v, k, c0, c1, c2, c3, u3, u4, u5, u15, u22, u23, u24;
	int len;

	if (!nx || !ny || s[0x300] || w < SS(0x54) + 0xe || h < SS(0x50) + 2)
		return -1;
	len = t33_rcfg_tmpl_init(l, cap, t33_af_tmpl, ARRAY_SIZE(t33_af_tmpl));
	if (len <= 0)
		return len;
	/* tisp_af_malloc_cfg */
	T33_AF_W(l, 0x18108, dma);
	T33_AF_W(l, 0x1810c, dma + 0x1800);
	T33_AF_W(l, 0x18110, dma + 0x3000);
	T33_AF_W(l, 0x18114, dma + 0x4800);
	T33_AF_W(l, 0x18118, 0x100df);
	/* tisp_af_par_init: window sizes (even) */
	v = (w - SS(0x54) - 0xe) / nx;
	v &= ~1U;
	for (k = 0; k < nx && k < 15; k++)
		t33_af_sp16(s, 0x58 + 2 * k, v);
	v = (h - SS(0x50) - 2) / ny;
	v &= ~1U;
	for (k = 0; k < ny && k < 15; k++)
		t33_af_sp16(s, 0x76 + 2 * k, v);
	/* filter indices, clamped as the vendor does (twice into 0x170 / 0x172) */
	if (SS(0x170) >= 6)
		t33_af_sp16(s, 0x170, 5);
	u3 = SS(0x1a8);
	u23 = u3;
	if (u23 >= 6)
		t33_af_sp16(s, 0x170, 5);
	if (SS(0x11c) >= 9)
		t33_af_sp16(s, 0x11c, 8);
	if (SS(0x146) >= 9)
		t33_af_sp16(s, 0x146, 8);
	if (SS(0x172) >= 7)
		t33_af_sp16(s, 0x172, 6);
	u4 = SS(0x1aa);
	if (u4 >= 7)
		t33_af_sp16(s, 0x172, 6);
	u5 = SS(0x11e);
	if (u5 >= 10)
		t33_af_sp16(s, 0x11e, 9);
	if (SS(0x148) >= 10)
		t33_af_sp16(s, 0x148, 9);
	u15 = SS(0x170);
	if (SS(0x172) <= u15)
		t33_af_sp16(s, 0x172, u15 + 1);
	if (u23 >= u4)
		t33_af_sp16(s, 0x1aa, u3 + 1);
	u24 = SS(0x11c);
	if (u24 >= SS(0x11e))
		t33_af_sp16(s, 0x11e, u24 + 1);
	u22 = SS(0x146);
	if (u22 >= SS(0x148))
		t33_af_sp16(s, 0x148, u22 + 1);
	c0 = (t33_af_tri((int)((SS(0x172) - u15) & 0xffff), 6) + u15) & 0xffff;
	c1 = (t33_af_tri((int)((SS(0x1aa) - u23) & 0xffff), 6) + u23) & 0xffff;
	c2 = (t33_af_tri((int)((SS(0x11e) - u24) & 0xffff), 9) + u24) & 0xffff;
	c3 = (t33_af_tri((int)((SS(0x148) - u22) & 0xffff), 9) + u22) & 0xffff;
	if (c0 >= ARRAY_SIZE(t33_af_iir) || c1 >= ARRAY_SIZE(t33_af_iir) || c2 >= ARRAY_SIZE(t33_af_fir) ||
	    c3 >= ARRAY_SIZE(t33_af_fir))
		return -1;
	ic = t33_af_iir[c0];
	id = t33_af_iir[c1];
	fa = t33_af_fir[c2];
	fb = t33_af_fir[c3];
	for (k = 2; k < 5; k++) {
		t33_af_sp16(s, 0x11c + 2 * k, fa[k]);
		t33_af_sp16(s, 0x146 + 2 * k, fb[k]);
	}
	for (k = 2; k < 12; k++) {
		t33_af_sp16(s, 0x170 + 2 * k, ic[k]);
		t33_af_sp16(s, 0x1a8 + 2 * k, id[k]);
	}
	/* the vendor's parameter block (af_info + 0x404) */
	memset(a, 0, sizeof(a));
	a[0] = (u8)SS(0x56);
	a[1] = (u8)SS(0x52);
	t33_af_p16(a, 2, SS(0x54));
	for (k = 0; k < 7; k++) {
		t33_af_p16(a, 4 + 2 * k, SS(0x58 + 2 * k));
		t33_af_p16(a, 0x14 + 2 * k, SS(0x68 + 2 * k));
	}
	t33_af_p16(a, 0x12, SS(0x64));	/* the vendor copies 0x64 twice (0x414 / 0x416), not 0x66 */
	t33_af_p16(a, 0x22, SS(0x50));
	for (k = 0; k < 15; k++)
		a[0x24 + k] = (u8)SS(0x76 + 2 * k);
	a[0x2b] = (u8)SS(0x82);		/* 0x42f: 0x82 again, not 0x84 */
	for (k = 0; k < 5; k++) {
		t33_af_p16(a, 0x34 + 2 * k, fa[k]);
		t33_af_p16(a, 0x3e + 2 * k, fb[k]);
	}
	a[0x48] = 0xf;
	{
		static const u8 ix[10] = { 0, 2, 3, 4, 5, 7, 8, 9, 10, 11 };

		for (k = 0; k < 10; k++) {
			t33_af_p16(a, 0x4a + 2 * k, ic[ix[k]]);
			t33_af_p16(a, 0x5e + 2 * k, id[ix[k]]);
		}
	}
	{
		/* four blocks of byte / u16 fields: { a, sec } */
		static const u16 m8[][2] = {
			{ 0x72, 0x20b }, { 0x73, 0x126 }, { 0x74, 0x128 }, { 0x78, 0x12c }, { 0x79, 0x12e }, { 0x7a, 0x130 },
			{ 0x7e, 0x134 }, { 0x7f, 0x20c }, { 0x80, 0x150 }, { 0x81, 0x152 }, { 0x84, 0x156 }, { 0x85, 0x158 },
			{ 0x86, 0x15a }, { 0x8a, 0x15e }, { 0x8b, 0x20d }, { 0x8c, 0x188 }, { 0x8d, 0x18a }, { 0x90, 0x18e },
			{ 0x91, 0x190 }, { 0x92, 0x192 }, { 0x96, 0x196 }, { 0x97, 0x20e }, { 0x98, 0x1c0 }, { 0x99, 0x1c2 },
			{ 0x9c, 0x1c6 }, { 0x9d, 0x1c8 }, { 0x9e, 0x1ca }, { 0xa2, 0x1ce }, { 0xa3, 0x20f }, { 0xa4, 0x136 },
			{ 0xa5, 0x138 }, { 0xa6, 0x13a }, { 0xa7, 0x13c }, { 0xa8, 0x210 }, { 0xa9, 0x160 }, { 0xaa, 0x162 },
			{ 0xab, 0x164 }, { 0xac, 0x166 }, { 0xad, 0x211 }, { 0xae, 0x198 }, { 0xaf, 0x19a }, { 0xb0, 0x19c },
			{ 0xb1, 0x19e }, { 0xb2, 0x212 }, { 0xb3, 0x1d0 }, { 0xb4, 0x1d2 }, { 0xb5, 0x1d4 }, { 0xb6, 0x1d6 },
			{ 0xb7, 0x213 }, { 0xb8, 0x214 }, { 0xb9, 0x215 }, { 0xba, 0x216 }, { 0xbb, 0x217 }, { 0xbc, 0x218 },
			{ 0xbd, 0x13e }, { 0xbe, 0x140 }, { 0xc4, 0x219 }, { 0xc5, 0x168 }, { 0xc6, 0x16a }, { 0xcc, 0x21a },
			{ 0xcd, 0x1a0 }, { 0xce, 0x1a2 }, { 0xd4, 0x21b }, { 0xd5, 0x1d8 }, { 0xd6, 0x1da },
		};
		static const u16 m16[][2] = {
			{ 0x76, 0x12a }, { 0x7c, 0x132 }, { 0x82, 0x154 }, { 0x88, 0x15c }, { 0x8e, 0x18c }, { 0x94, 0x194 },
			{ 0x9a, 0x1c4 }, { 0xa0, 0x1cc }, { 0xc0, 0x142 }, { 0xc2, 0x144 }, { 0xc8, 0x16c }, { 0xca, 0x16e },
			{ 0xd0, 0x1a4 }, { 0xd2, 0x1a6 }, { 0xd8, 0x1dc }, { 0xda, 0x1de },
		};

		/* sec bytes 0x20b.. are bytes, the others the low byte of a u16 */
		for (k = 0; k < ARRAY_SIZE(m8); k++)
			a[m8[k][0]] = s[m8[k][1]];
		for (k = 0; k < ARRAY_SIZE(m16); k++)
			t33_af_p16(a, m16[k][0], SS(m16[k][1]));
	}
	a[0xdc] = 1;
	a[0xdd] = 0;
	/* tisp_af_t33_all_reg_write */
	T33_AF_W(l, 0x18040, (A16(2) & 0xfff) << 16 | (A16(0x22) & 0xfff));
	T33_AF_W(l, 0x18044, (A16(4) & 0x1ff) | (u32)a[0x24] << 16);
	T33_AF_W(l, 0x18074, (a[0xdc] & 0xfU) | (u32)a[0xb7] << 8 | (a[0x48] & 0xfU) << 4 | (a[0xdd] & 1U) << 16);
	T33_AF_W(l, 0x18078, (a[0x7f] & 1U) << 4 | (a[0x8b] & 1U) << 8 | (a[0x72] & 1U) | (a[0x97] & 1U) << 12 |
			     (a[0xa3] & 1U) << 16 | (a[0xa8] & 1U) << 20 | (a[0xad] & 1U) << 24 | (a[0xb2] & 1U) << 28);
	T33_AF_W(l, 0x1807c, (A16(0x36) & 0x7ff) << 16 | (A16(0x34) & 0x7ff));
	T33_AF_W(l, 0x18080, (A16(0x3a) & 0x7ff) << 16 | (A16(0x38) & 0x7ff));
	T33_AF_W(l, 0x18084, A16(0x3c) & 0x7ff);
	T33_AF_W(l, 0x18088, (A16(0x40) & 0x7ff) << 16 | (A16(0x3e) & 0x7ff));
	T33_AF_W(l, 0x1808c, (A16(0x44) & 0x7ff) << 16 | (A16(0x42) & 0x7ff));
	T33_AF_W(l, 0x18090, A16(0x46) & 0x7ff);
	T33_AF_W(l, 0x18094, (A16(0x4c) & 0x7ff) << 16 | (A16(0x4a) & 0x1ff));
	T33_AF_W(l, 0x18098, (A16(0x50) & 0x7ff) << 16 | (A16(0x4e) & 0x7ff));
	T33_AF_W(l, 0x1809c, (A16(0x54) & 0x7ff) << 16 | (A16(0x52) & 0x7ff));
	T33_AF_W(l, 0x180a0, (A16(0x58) & 0x7ff) << 16 | (A16(0x56) & 0x7ff));
	T33_AF_W(l, 0x180a4, (A16(0x60) & 0x7ff) << 16 | (A16(0x5e) & 0x1ff));
	T33_AF_W(l, 0x180a8, (A16(0x64) & 0x7ff) << 16 | (A16(0x62) & 0x7ff));
	T33_AF_W(l, 0x180ac, (A16(0x68) & 0x7ff) << 16 | (A16(0x66) & 0x7ff));
	T33_AF_W(l, 0x180b0, (A16(0x6c) & 0x7ff) << 16 | (A16(0x6a) & 0x7ff));
	T33_AF_W(l, 0x180b4, (A16(0x76) & 0xfff) << 16 | A16(0x73));
	T33_AF_W(l, 0x180b8, (A16(0x7c) & 0xfff) << 16 | A16(0x79));
	T33_AF_W(l, 0x180bc, (A16(0x82) & 0xfff) << 16 | A16(0x80));
	T33_AF_W(l, 0x180c0, (A16(0x88) & 0xfff) << 16 | A16(0x85));
	T33_AF_W(l, 0x180c4, (A16(0x8e) & 0xfff) << 16 | A16(0x8c));
	T33_AF_W(l, 0x180c8, (A16(0x94) & 0xfff) << 16 | A16(0x91));
	T33_AF_W(l, 0x180cc, (A16(0x9a) & 0xfff) << 16 | A16(0x98));
	T33_AF_W(l, 0x180d0, (A16(0xa0) & 0xfff) << 16 | A16(0x9d));
	T33_AF_W(l, 0x180d4, (u32)a[0x7e] << 8 | (u32)a[0x84] << 16 | a[0x78] | (u32)a[0x8a] << 24);
	T33_AF_W(l, 0x180d8, (u32)a[0x96] << 8 | (u32)a[0x9c] << 16 | a[0x90] | (u32)a[0xa2] << 24);
	T33_AF_W(l, 0x180dc, A32(0xa4));
	T33_AF_W(l, 0x180e0, A32(0xa9));
	T33_AF_W(l, 0x180e4, A32(0xae));
	T33_AF_W(l, 0x180e8, A32(0xb3));
	T33_AF_W(l, 0x180ec, (a[0xb9] & 0xfU) << 4 | (a[0xba] & 0xfU) << 8 | (a[0xb8] & 0xfU) | (a[0xbb] & 0xfU) << 12 |
			     (a[0xbc] & 1U) << 16 | (a[0xc4] & 1U) << 20 | (a[0xcc] & 1U) << 24 | (a[0xd4] & 1U) << 28);
	T33_AF_W(l, 0x180f0, (u32)a[0xbe] << 8 | (u32)a[0xc5] << 16 | a[0xbd] | (u32)a[0xc6] << 24);
	T33_AF_W(l, 0x180f4, (u32)a[0xce] << 8 | (u32)a[0xd5] << 16 | a[0xcd] | (u32)a[0xd6] << 24);
	T33_AF_W(l, 0x180f8, (A16(0xc2) & 0x1fff) << 16 | (A16(0xc0) & 0x1fff));
	T33_AF_W(l, 0x180fc, (A16(0xca) & 0x1fff) << 16 | (A16(0xc8) & 0x1fff));
	T33_AF_W(l, 0x18100, (A16(0xd2) & 0x1fff) << 16 | (A16(0xd0) & 0x1fff));
	T33_AF_W(l, 0x18104, (A16(0xda) & 0x1fff) << 16 | (A16(0xd8) & 0x1fff));
	T33_AF_W(l, 0x18124, (A16(0x5c) & 0x1ff) << 16 | (A16(0x5a) & 0x7ff));
	T33_AF_W(l, 0x18128, (A16(0x70) & 0x1ff) << 16 | (A16(0x6e) & 0x7ff));
	T33_AF_W(l, 0x18010, 1);
#undef SS
#undef A16
#undef A32
	return len;
}

/*
 * tisp_af_api_set_attr / _get_attr (vendor 0x463a8 / 0x462d4) on the section copy s (the vendor's
 * af_info + 0x4e8 block): the 24 B attribute a { u32 .., u32 .., u8 @8, u8 shift @9, u16 @10, u16 @12,
 * u16 @14 (low byte stored), u16 @16, u8 @18 .. @21, u16 @22 }.  set patches the section words, the
 * caller then runs t33_af_init (tisp_af_par_init + tisp_af_t33_all_reg_write) again; get answers the
 * section words (0x44 / 0x40 shifted by the attribute's @9) and the attribute's own bytes 8, 9.
 */
static inline void t33_af_attr_patch(u8 *s, const u8 *a)
{
	static const u8 off[4] = { 0x54, 0x50, 0x56, 0x52 };
	int i;

	s[0x1e4] = a[10]; s[0x1e5] = a[11];
	s[0x1e6] = a[12]; s[0x1e7] = a[13];
	s[0x213] = a[14];
	s[0x1e0] = a[16]; s[0x1e1] = a[17];
	for (i = 0; i < 4; i++) {
		s[off[i]] = a[0x12 + i];
		s[off[i] + 1] = 0;
	}
	s[0x1e2] = a[0x16]; s[0x1e3] = a[0x17];
}

static inline u32 t33_af_le32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline void t33_af_attr_read(const u8 *s, const u8 *par, u8 *o)
{
	u32 sh = par[9] & 0x1f, v;
	int i;

	v = t33_af_le32(s + 0x44) >> sh;
	for (i = 0; i < 4; i++)
		o[i] = (u8)(v >> (8 * i));
	v = t33_af_le32(s + 0x40) >> sh;
	for (i = 0; i < 4; i++)
		o[4 + i] = (u8)(v >> (8 * i));
	o[8] = par[8];
	o[9] = par[9];
	o[10] = s[0x1e4]; o[11] = s[0x1e5];
	o[12] = s[0x1e6]; o[13] = s[0x1e7];
	o[14] = s[0x213]; o[15] = 0;
	o[16] = s[0x1e0]; o[17] = s[0x1e1];
	o[0x12] = s[0x54]; o[0x13] = s[0x50]; o[0x14] = s[0x56]; o[0x15] = s[0x52];
	o[0x16] = s[0x1e2]; o[0x17] = s[0x1e3];
}

#endif /* TX_ISP_T33_AF_H */
