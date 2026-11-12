/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) defog, module list 12, IQ bin section 14 (0x294 bytes):
 * tisp_defog_init (block geometry from the sensor size: 10 x 18 pixel
 * half-blocks, the four 2 KiB statistics DMA areas at 0xa210..0xa21c),
 * tisp_defog_write_reg_para1 (block weights of the section),
 * tisp_defog_update_reg_para2 + tisp_defog_write_reg_para2 (curves from
 * the section, the strength-scaled tables) and tisp_defog_write_reg_para3
 * (the 180 initial block entries 0x80 0x80 0x80 0 through the port
 * 0x320a4).  The weights are the section's own when it was made for the
 * sensor size (sec[0x1b1] = 0x80 and 0xbe / 0xc0 = width / height);
 * otherwise they are recomputed for the size and zoom sec[0x1b1]
 * (defog_count_weight35abc, t33_defog_count_weight on a copy of the
 * section; same algorithm and base tables as the T40 port).  tisp_defog_interp_trans_k only feeds the
 * per-frame soft path, not the init list.  QEMU trace of the vendor module
 * (openimp #388): list 12 word-equal but for the DMA addresses.
 */
#ifndef TX_ISP_T33_DEFOG_H
#define TX_ISP_T33_DEFOG_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_DEFOG		12
#define T33_SEC_DEFOG		14
#define T33_DEFOG_SEC_BYTES	0x294U
#define T33_DEFOG_DMA_BYTES	0x2000U

static const struct t33_tcmd t33_defog_tmpl[] = {
	{ 0x0a010, 1, 0 }, { 0x0a040, 4, 0 }, { 0x0a204, 10, 0 }, { 0x0a25c, 1, 0 }, { 0x0a050, 12, 1 },
	{ 0x0a080, 16, 1 }, { 0x0a0c0, 8, 1 }, { 0x0a0e0, 8, 1 }, { 0x0a100, 8, 1 }, { 0x0a120, 8, 1 },
	{ 0x0a140, 8, 1 }, { 0x0a160, 8, 1 }, { 0x0a180, 8, 1 }, { 0x0a1a0, 9, 1 }, { 0x0a1c4, 8, 1 },
	{ 0x0a1e4, 8, 1 }, { 0x0a228, 1, 0 }, { 0x320a0, 1, 0 }, { 0x320a4, 180, 1 }, { 0x320a0, 1, 0 },
	{ 0x0a228, 1, 0 },
};

#define S8(o)	((u32)sec[o])
#define S16(o)	t32_tp_le16(sec + (o))

/* tisp_defog_write_reg_para1: block weights (slots 21..94) */
static inline void t33_defog_para1(const u8 *sec, u32 *l)
{
	T33_SW(l, 21, ((S16(0x52) & 0x1fff) << 0x10) | (S16(0x50) & 0x1fff));
	T33_SW(l, 22, ((S16(0x56) & 0x1fff) << 0x10) | (S16(0x54) & 0x1fff));
	T33_SW(l, 23, ((S16(0x5a) & 0x1fff) << 0x10) | (S16(0x58) & 0x1fff));
	T33_SW(l, 24, ((S16(0x5e) & 0x1fff) << 0x10) | (S16(0x5c) & 0x1fff));
	T33_SW(l, 25, ((S16(0x62) & 0x1fff) << 0x10) | (S16(0x60) & 0x1fff));
	T33_SW(l, 26, ((S16(0x66) & 0x1fff) << 0x10) | (S16(0x64) & 0x1fff));
	T33_SW(l, 27, ((S16(0x6a) & 0x1fff) << 0x10) | (S16(0x68) & 0x1fff));
	T33_SW(l, 28, ((S16(0x6e) & 0x1fff) << 0x10) | (S16(0x6c) & 0x1fff));
	T33_SW(l, 29, ((S16(0x72) & 0x1fff) << 0x10) | (S16(0x70) & 0x1fff));
	T33_SW(l, 30, ((S16(0x76) & 0x1fff) << 0x10) | (S16(0x74) & 0x1fff));
	T33_SW(l, 31, ((S16(0x7a) & 0x1fff) << 0x10) | (S16(0x78) & 0x1fff));
	T33_SW(l, 32, ((S16(0x7e) & 0x1fff) << 0x10) | (S16(0x7c) & 0x1fff));
	T33_SW(l, 34, ((S16(0x82) & 0x3fff) << 0x10) | (S16(0x80) & 0x3fff));
	T33_SW(l, 35, ((S16(0x86) & 0x3fff) << 0x10) | (S16(0x84) & 0x3fff));
	T33_SW(l, 36, ((S16(0x8a) & 0x3fff) << 0x10) | (S16(0x88) & 0x3fff));
	T33_SW(l, 37, ((S16(0x8e) & 0x3fff) << 0x10) | (S16(0x8c) & 0x3fff));
	T33_SW(l, 38, ((S16(0x92) & 0x3fff) << 0x10) | (S16(0x90) & 0x3fff));
	T33_SW(l, 39, ((S16(0x96) & 0x3fff) << 0x10) | (S16(0x94) & 0x3fff));
	T33_SW(l, 40, ((S16(0x9a) & 0x3fff) << 0x10) | (S16(0x98) & 0x3fff));
	T33_SW(l, 41, ((S16(0x9e) & 0x3fff) << 0x10) | (S16(0x9c) & 0x3fff));
	T33_SW(l, 42, ((S16(0xa2) & 0x3fff) << 0x10) | (S16(0xa0) & 0x3fff));
	T33_SW(l, 43, ((S16(0xa6) & 0x3fff) << 0x10) | (S16(0xa4) & 0x3fff));
	T33_SW(l, 44, ((S16(0xaa) & 0x3fff) << 0x10) | (S16(0xa8) & 0x3fff));
	T33_SW(l, 45, ((S16(0xae) & 0x3fff) << 0x10) | (S16(0xac) & 0x3fff));
	T33_SW(l, 46, ((S16(0xb2) & 0x3fff) << 0x10) | (S16(0xb0) & 0x3fff));
	T33_SW(l, 47, ((S16(0xb6) & 0x3fff) << 0x10) | (S16(0xb4) & 0x3fff));
	T33_SW(l, 48, ((S16(0xba) & 0x3fff) << 0x10) | (S16(0xb8) & 0x3fff));
	T33_SW(l, 49, S16(0xbc) & 0x3fff);
	T33_SW(l, 51, ((S8(0x1b3) & 0x1f) << 8) | ((S8(0x1b4) & 0x1f) << 0x10) | (S8(0x1b2) & 0x1f) | ((S8(0x1b5) & 0x1f) << 0x18));
	T33_SW(l, 52, ((S8(0x1b7) & 0x1f) << 8) | ((S8(0x1b8) & 0x1f) << 0x10) | (S8(0x1b6) & 0x1f) | ((S8(0x1b9) & 0x1f) << 0x18));
	T33_SW(l, 53, ((S8(0x1bb) & 0x1f) << 8) | ((S8(0x1bc) & 0x1f) << 0x10) | (S8(0x1ba) & 0x1f) | ((S8(0x1bd) & 0x1f) << 0x18));
	T33_SW(l, 54, ((S8(0x1bf) & 0x1f) << 8) | ((S8(0x1c0) & 0x1f) << 0x10) | (S8(0x1be) & 0x1f) | ((S8(0x1c1) & 0x1f) << 0x18));
	T33_SW(l, 55, ((S8(0x1c3) & 0x1f) << 8) | ((S8(0x1c4) & 0x1f) << 0x10) | (S8(0x1c2) & 0x1f) | ((S8(0x1c5) & 0x1f) << 0x18));
	T33_SW(l, 56, ((S8(0x1c7) & 0x1f) << 8) | ((S8(0x1c8) & 0x1f) << 0x10) | (S8(0x1c6) & 0x1f) | ((S8(0x1c9) & 0x1f) << 0x18));
	T33_SW(l, 57, ((S8(0x1cb) & 0x1f) << 8) | ((S8(0x1cc) & 0x1f) << 0x10) | (S8(0x1ca) & 0x1f) | ((S8(0x1cd) & 0x1f) << 0x18));
	T33_SW(l, 58, ((S8(0x1cf) & 0x1f) << 8) | ((S8(0x1d0) & 0x1f) << 0x10) | (S8(0x1ce) & 0x1f) | ((S8(0x1d1) & 0x1f) << 0x18));
	T33_SW(l, 60, ((S8(0x1d3) & 0x1f) << 8) | ((S8(0x1d4) & 0x1f) << 0x10) | (S8(0x1d2) & 0x1f) | ((S8(0x1d5) & 0x1f) << 0x18));
	T33_SW(l, 61, ((S8(0x1d7) & 0x1f) << 8) | ((S8(0x1d8) & 0x1f) << 0x10) | (S8(0x1d6) & 0x1f) | ((S8(0x1d9) & 0x1f) << 0x18));
	T33_SW(l, 62, ((S8(0x1db) & 0x1f) << 8) | ((S8(0x1dc) & 0x1f) << 0x10) | (S8(0x1da) & 0x1f) | ((S8(0x1dd) & 0x1f) << 0x18));
	T33_SW(l, 63, ((S8(0x1df) & 0x1f) << 8) | ((S8(0x1e0) & 0x1f) << 0x10) | (S8(0x1de) & 0x1f) | ((S8(0x1e1) & 0x1f) << 0x18));
	T33_SW(l, 64, ((S8(0x1e3) & 0x1f) << 8) | ((S8(0x1e4) & 0x1f) << 0x10) | (S8(0x1e2) & 0x1f) | ((S8(0x1e5) & 0x1f) << 0x18));
	T33_SW(l, 65, ((S8(0x1e7) & 0x1f) << 8) | ((S8(0x1e8) & 0x1f) << 0x10) | (S8(0x1e6) & 0x1f) | ((S8(0x1e9) & 0x1f) << 0x18));
	T33_SW(l, 66, ((S8(0x1eb) & 0x1f) << 8) | ((S8(0x1ec) & 0x1f) << 0x10) | (S8(0x1ea) & 0x1f) | ((S8(0x1ed) & 0x1f) << 0x18));
	T33_SW(l, 67, ((S8(0x1ef) & 0x1f) << 8) | ((S8(0x1f0) & 0x1f) << 0x10) | (S8(0x1ee) & 0x1f) | ((S8(0x1f1) & 0x1f) << 0x18));
	T33_SW(l, 69, ((S8(0x1f3) & 0x1f) << 8) | ((S8(0x1f4) & 0x1f) << 0x10) | (S8(0x1f2) & 0x1f) | ((S8(0x1f5) & 0x1f) << 0x18));
	T33_SW(l, 70, ((S8(0x1f7) & 0x1f) << 8) | ((S8(0x1f8) & 0x1f) << 0x10) | (S8(0x1f6) & 0x1f) | ((S8(0x1f9) & 0x1f) << 0x18));
	T33_SW(l, 71, ((S8(0x1fb) & 0x1f) << 8) | ((S8(0x1fc) & 0x1f) << 0x10) | (S8(0x1fa) & 0x1f) | ((S8(0x1fd) & 0x1f) << 0x18));
	T33_SW(l, 72, ((S8(0x1ff) & 0x1f) << 8) | ((S8(0x200) & 0x1f) << 0x10) | (S8(0x1fe) & 0x1f) | ((S8(0x201) & 0x1f) << 0x18));
	T33_SW(l, 73, ((S8(0x203) & 0x1f) << 8) | ((S8(0x204) & 0x1f) << 0x10) | (S8(0x202) & 0x1f) | ((S8(0x205) & 0x1f) << 0x18));
	T33_SW(l, 74, ((S8(0x207) & 0x1f) << 8) | ((S8(0x208) & 0x1f) << 0x10) | (S8(0x206) & 0x1f) | ((S8(0x209) & 0x1f) << 0x18));
	T33_SW(l, 75, ((S8(0x20b) & 0x1f) << 8) | ((S8(0x20c) & 0x1f) << 0x10) | (S8(0x20a) & 0x1f) | ((S8(0x20d) & 0x1f) << 0x18));
	T33_SW(l, 76, ((S8(0x20f) & 0x1f) << 8) | ((S8(0x210) & 0x1f) << 0x10) | (S8(0x20e) & 0x1f) | ((S8(0x211) & 0x1f) << 0x18));
	T33_SW(l, 78, ((S8(0x213) & 0x1f) << 8) | ((S8(0x214) & 0x1f) << 0x10) | (S8(0x212) & 0x1f) | ((S8(0x215) & 0x1f) << 0x18));
	T33_SW(l, 79, ((S8(0x217) & 0x1f) << 8) | ((S8(0x218) & 0x1f) << 0x10) | (S8(0x216) & 0x1f) | ((S8(0x219) & 0x1f) << 0x18));
	T33_SW(l, 80, ((S8(0x21b) & 0x1f) << 8) | ((S8(0x21c) & 0x1f) << 0x10) | (S8(0x21a) & 0x1f) | ((S8(0x21d) & 0x1f) << 0x18));
	T33_SW(l, 81, ((S8(0x21f) & 0x1f) << 8) | ((S8(0x220) & 0x1f) << 0x10) | (S8(0x21e) & 0x1f) | ((S8(0x221) & 0x1f) << 0x18));
	T33_SW(l, 82, ((S8(0x223) & 0x1f) << 8) | ((S8(0x224) & 0x1f) << 0x10) | (S8(0x222) & 0x1f) | ((S8(0x225) & 0x1f) << 0x18));
	T33_SW(l, 83, ((S8(0x227) & 0x1f) << 8) | ((S8(0x228) & 0x1f) << 0x10) | (S8(0x226) & 0x1f) | ((S8(0x229) & 0x1f) << 0x18));
	T33_SW(l, 84, ((S8(0x22b) & 0x1f) << 8) | ((S8(0x22c) & 0x1f) << 0x10) | (S8(0x22a) & 0x1f) | ((S8(0x22d) & 0x1f) << 0x18));
	T33_SW(l, 85, ((S8(0x22f) & 0x1f) << 8) | ((S8(0x230) & 0x1f) << 0x10) | (S8(0x22e) & 0x1f) | ((S8(0x231) & 0x1f) << 0x18));
	T33_SW(l, 87, ((S8(0x233) & 0x1f) << 8) | ((S8(0x234) & 0x1f) << 0x10) | (S8(0x232) & 0x1f) | ((S8(0x235) & 0x1f) << 0x18));
	T33_SW(l, 88, ((S8(0x237) & 0x1f) << 8) | ((S8(0x238) & 0x1f) << 0x10) | (S8(0x236) & 0x1f) | ((S8(0x239) & 0x1f) << 0x18));
	T33_SW(l, 89, ((S8(0x23b) & 0x1f) << 8) | ((S8(0x23c) & 0x1f) << 0x10) | (S8(0x23a) & 0x1f) | ((S8(0x23d) & 0x1f) << 0x18));
	T33_SW(l, 90, ((S8(0x23f) & 0x1f) << 8) | ((S8(0x240) & 0x1f) << 0x10) | (S8(0x23e) & 0x1f) | ((S8(0x241) & 0x1f) << 0x18));
	T33_SW(l, 91, ((S8(0x243) & 0x1f) << 8) | ((S8(0x244) & 0x1f) << 0x10) | (S8(0x242) & 0x1f) | ((S8(0x245) & 0x1f) << 0x18));
	T33_SW(l, 92, ((S8(0x247) & 0x1f) << 8) | ((S8(0x248) & 0x1f) << 0x10) | (S8(0x246) & 0x1f) | ((S8(0x249) & 0x1f) << 0x18));
	T33_SW(l, 93, ((S8(0x24b) & 0x1f) << 8) | ((S8(0x24c) & 0x1f) << 0x10) | (S8(0x24a) & 0x1f) | ((S8(0x24d) & 0x1f) << 0x18));
	T33_SW(l, 94, ((S8(0x24f) & 0x1f) << 8) | ((S8(0x250) & 0x1f) << 0x10) | (S8(0x24e) & 0x1f) | ((S8(0x251) & 0x1f) << 0x18));
}

/* four section bytes from p as one word, 7 bits each */
static inline u32 t33_defog_b4(const u8 *p)
{
	return (p[1] & 0x7fU) << 8 | (p[2] & 0x7fU) << 16 | (p[0] & 0x7fU) | (p[3] & 0x7fU) << 24;
}

/* tisp_defog_update_reg_para2 + tisp_defog_write_reg_para2 */
static inline void t33_defog_para2(const u8 *sec, u32 *l)
{
	u32 w[17], a = sec[0x18d], b = sec[0x19f], x, y, k;
	u8 s[32];

	for (k = 0; k < 17; k++) {
		x = a < 0x41 ? (sec[0x18e + k] * a) >> 6 : 0x7f - (((0x7f - sec[0x18e + k]) * (0x80 - a)) >> 6);
		y = b < 0x41 ? (sec[0x1a0 + k] * b) >> 6 : 0x7f - (((0x7f - sec[0x1a0 + k]) * (0x80 - b)) >> 6);
		w[k] = (y & 0x7f) << 7 | (x & 0x7f);
	}
	for (k = 0; k < 32; k++)
		s[k] = (u8)(((sec[0x16d + k] * sec[0x16c]) & 0x3fff) >> 7);
	T33_SW(l, 5, (S8(0x10a) & 0x3f) << 8 | (S8(0x109) & 0x3f) << 16 | (S8(0x14b) & 0x3f) | (S8(0xc2) & 1) << 24);
	for (k = 0; k < 8; k++) {
		T33_SW(l, 0x60 + k, t33_defog_b4(sec + 0x10b + 4 * k));
		T33_SW(l, 0x69 + k, t33_defog_b4(sec + 0x12b + 4 * k));
		T33_SW(l, 0x7c + k, t33_defog_b4(sec + 0x14c + 4 * k));
		T33_SW(l, 0x85 + k, t33_defog_b4(s + 4 * k));
	}
	for (k = 0; k < 8; k++)
		T33_SW(l, 0x72 + k, (w[2 * k + 1] & 0x3fff) << 16 | (w[2 * k] & 0x3fff));
	T33_SW(l, 0x7a, w[16] & 0x3fff);
	T33_SW(l, 0xf, S8(0x253) & 0x1f);
	T33_SW(l, 1, 0xf);
}

/* tisp_defog_write_reg_para3 at init: 180 entries 0x80 0x80 0x80 0 */
static inline void t33_defog_para3(u32 *l)
{
	unsigned int k;

	T33_SW(l, 0x8e, 0);
	T33_SW(l, 0x90, 0x101);
	for (k = 0; k < 180; k++)
		T33_SW(l, 0x92 + k, 0x808080);
	T33_SW(l, 0x147, 0xb30102);
	T33_SW(l, 0x149, 1);
}

#undef S8
#undef S16

/* defog_count_weight35abc base tables (equal to the T40 module's) */
static const u16 t33_defog_w24_base[24] = {
	255, 521, 799, 1090, 1395, 1717, 2057, 2417, 2799, 3207, 3643, 4114,
	4624, 5180, 5792, 6471, 7237, 8111, 9133, 10359, 11896, 13953, 17075, 23792
};
static const u16 t33_defog_dist_base[63] = {
	133, 269, 407, 547, 689, 834, 982, 1132, 1285, 1440, 1599, 1761, 1926, 2094, 2266, 2442,
	2621, 2804, 2991, 3183, 3379, 3580, 3785, 3996, 4213, 4435, 4663, 4898, 5140, 5389, 5645, 5910,
	6183, 6465, 6758, 7061, 7375, 7702, 8042, 8397, 8767, 9155, 9562, 9989, 10440, 10916, 11421, 11959,
	12534, 13152, 13819, 14544, 15338, 16216, 17197, 18130, 19595, 21114, 22974, 25371, 28750, 34526, 40368
};

/* the section's weights are not for this size / zoom: defog_count_weight35abc needed */
static inline int t33_defog_weights_needed(const u8 *sec, u32 w, u32 h)
{
	return sec[0x1b1] != 0x80 || t32_tp_le16(sec + 0xbe) != w || t32_tp_le16(sec + 0xc0) != h;
}

/* scratch bytes t33_defog_count_weight needs (13 maps of the cells) */
static inline u32 t33_defog_scratch_bytes(u32 w, u32 h)
{
	return 13U * ((h + 5) / 10) * ((w + 9) / 18);
}

static inline void t33_defog_put16(u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/* defog_wei_interpcot: mean value per index, clipped to 31, non-decreasing */
static inline void t33_defog_wtab(u32 n, const u8 *idx, const u8 *val, u8 *out)
{
	u32 sum[32], cnt[32], i, v;

	memset(sum, 0, sizeof(sum));
	memset(cnt, 0, sizeof(cnt));
	for (i = 0; i < n; i++) {
		sum[idx[i] & 31] += val[i];
		cnt[idx[i] & 31]++;
	}
	for (i = 0; i < 32; i++) {
		v = cnt[i] ? (((cnt[i] >> 1) + sum[i]) / cnt[i]) & 0xff : 0;
		out[i] = v < 32 ? v : 31;
		if (i && out[i] < out[i - 1])
			out[i] = out[i - 1];
	}
}

/*
 * defog_count_weight35abc on a writable copy of section 14: the position
 * weights 0x50 (u16[24]), 0x80 (u16[31]) and 0x1b2 (5 x 32) for the size
 * w x h and the zoom sec[0x1b1]; m = zeroed scratch of
 * t33_defog_scratch_bytes(w, h).  0xbe / 0xc0 then hold w / h.
 */
static inline void t33_defog_count_weight(u8 *sec, u32 w, u32 h, u8 *m)
{
	u32 rows = (h + 5) / 10, cols = (w + 9) / 18, n = rows * cols, scale, i, j;
	static const u8 ref[8][2] = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 0 }, { 1, 1 }, { 1, 2 }, { 2, 0 }, { 2, 1 } };
	u16 dist[63], v;
	u8 *mm[8], *ix[5];
	int row, col, yy, xx;

	scale = ((h * w + 0x3f4) / 0x7e9 * sec[0x1b1] + 0x40) >> 7;
	for (i = 0; i < 24; i++)
		t33_defog_put16(sec + 0x50 + 2 * i, (u16)((t33_defog_w24_base[i] * scale + 0x2000) >> 14));
	for (i = 0; i < 63; i++)
		dist[i] = (u16)((t33_defog_dist_base[i] * scale + 0x1000) >> 13);
	for (i = 0; i < 31; i++)
		t33_defog_put16(sec + 0x80 + 2 * i, dist[2 * i + 1]);
	/* keep the tables inside their fields, strictly increasing at the top */
	if (t32_tp_le16(sec + 0x50 + 46) >> 13) {
		t33_defog_put16(sec + 0x50 + 46, 0x1fff);
		for (i = 22; i > 0; i--) {
			v = t32_tp_le16(sec + 0x50 + 2 * i + 2);
			if (t32_tp_le16(sec + 0x50 + 2 * i) < v)
				break;
			t33_defog_put16(sec + 0x50 + 2 * i, v - 1);
		}
	}
	if (t32_tp_le16(sec + 0x80 + 60) >> 14) {
		t33_defog_put16(sec + 0x80 + 60, 0x3fff);
		for (i = 29; i > 0; i--) {
			v = t32_tp_le16(sec + 0x80 + 2 * i + 2);
			if (t32_tp_le16(sec + 0x80 + 2 * i) < v)
				break;
			t33_defog_put16(sec + 0x80 + 2 * i, v - 1);
		}
	}
	for (i = 0; i < 8; i++)
		mm[i] = m + i * n;
	for (i = 0; i < 5; i++)
		ix[i] = m + (8 + i) * n;
	/* distance class of each cell to eight reference points (half cells) */
	for (row = 0, yy = 5 * rows - 1; row < (int)rows; row++, yy -= 2) {
		int y2[3] = { yy * yy, (yy - 2 * (int)rows) * (yy - 2 * (int)rows),
			      (yy - 4 * (int)rows) * (yy - 4 * (int)rows) };

		for (col = 0, xx = 5 * cols - 1; col < (int)cols; col++, xx -= 2) {
			int x2[3] = { xx * xx, (xx - 2 * (int)cols) * (xx - 2 * (int)cols),
				      (xx - 4 * (int)cols) * (xx - 4 * (int)cols) };

			for (i = 0; i < 8; i++) {
				u32 d = (u32)(y2[ref[i][0]] + x2[ref[i][1]] + 32) >> 6;

				for (j = 0; j < 63; j++)
					if (d < dist[j]) {
						mm[i][row * cols + col] = (u8)(63 - j);
						break;
					}
			}
		}
	}
	for (i = 0; i < n; i++) {
		ix[0][i] = mm[4][i] >> 1;
		ix[1][i] = (mm[4][i] + mm[5][i]) >> 2;
		ix[2][i] = (mm[4][i] + mm[7][i]) >> 2;
		ix[3][i] = mm[5][i] >> 1;
		ix[4][i] = mm[7][i] >> 1;
	}
	t33_defog_wtab(n, ix[0], mm[0], sec + 0x1b2);
	t33_defog_wtab(n, ix[1], mm[1], sec + 0x1d2);
	t33_defog_wtab(n, ix[2], mm[3], sec + 0x1f2);
	t33_defog_wtab(n, ix[3], mm[2], sec + 0x212);
	t33_defog_wtab(n, ix[4], mm[6], sec + 0x232);
	t33_defog_put16(sec + 0xbe, (u16)w);
	t33_defog_put16(sec + 0xc0, (u16)h);
}

/*
 * Block init into list l (capacity cap words): sec section 14, w / h the
 * sensor size, dma the physical address of the statistics area
 * (T33_DEFOG_DMA_BYTES).  Returns the list length, -1 when the section's
 * weights are not for this size: when t33_defog_weights_needed, run
 * t33_defog_count_weight on a copy of the section first.
 */
static inline int t33_defog_init(const u8 *sec, u32 w, u32 h, u32 dma, u32 *l, u32 cap)
{
	u32 rows = ((h >> 1) / 10) * 2, cols = ((w >> 1) / 18) * 2, n = (rows + 2) * (cols + 2), sz;
	int len;

	/* the zoom (sec[0x1b1]) is the caller's: t33_defog_weights_needed */
	if (t32_tp_le16(sec + 0xbe) != w || t32_tp_le16(sec + 0xc0) != h)
		return -1;
	len = t33_rcfg_tmpl_init(l, cap, t33_defog_tmpl, ARRAY_SIZE(t33_defog_tmpl));
	if (len <= 0)
		return len;
	sz = n > 0x2000 ? (n > 0x4000 ? (n > 0x8000 ? 3 : 2) : 1) : 0;
	T33_SW(l, 0xb, dma);
	T33_SW(l, 0xc, dma + 0x800);
	T33_SW(l, 0xd, dma + 0x1000);
	T33_SW(l, 0xe, dma + 0x1800);
	T33_SW(l, 10, 3);
	T33_SW(l, 3, (rows & 0xff) << 16 | (cols & 0xff));
	T33_SW(l, 4, ((h >> 1) % 10) << 16 | ((w >> 1) % 18));
	T33_SW(l, 6, 0);
	T33_SW(l, 8, 0);
	T33_SW(l, 9, sz);
	T33_SW(l, 10, 2);
	T33_SW(l, 1, 3);
	t33_defog_para1(sec, l);
	t33_defog_para2(sec, l);
	t33_defog_para3(l);
	return len;
}

#endif /* TX_ISP_T33_DEFOG_H */
