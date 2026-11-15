/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) DPC / CTR / CAC block parameter updates on the vendor's dpcInfo layout (openimp #99 group 13c):
 * tisp_dpc_bin_para_update, func_dpc_para_update, func_ctr_para_update and func_cac_para_update, each on the
 * 0x2b0-byte word array v (dpcInfo[ch]).  Word 0xaa holds the IQ section (604 bytes; the vendor: tparamsP +
 * 0x3e2c), word 0xab the 16.16 total gain.  tisp_dpc_init, tisp_dpc_l_gain_update and tisp_dpc_switch_para
 * call them in this order: bin, (gain interpolations), dpc, ctr, cac.
 * Oracle block dpcpar (tests/t33_binoracle/dpcpar.conf): the whole block equal to the vendor's.
 */
#ifndef TX_ISP_T33_DPCPAR_H
#define TX_ISP_T33_DPCPAR_H

#include "tx_isp_t33_blk.h"

#define T33_DPC_SEC_WORD	0xaaU
#define T33_DPC_GAIN_WORD	0xabU

/* tisp_dpc_bin_para_update: the section's enable / mode bytes and the CTR / CAC words (bytes, s16, u16) */
static inline void t33_dpc_bin_para_v(u32 *v)
{
	const u8 *s = (const u8 *)(unsigned long)v[T33_DPC_SEC_WORD];
	unsigned int k;

	for (k = 0; k < 10; k++)
		v[k] = s[4 + k];
	for (k = 0; k < 7; k++)
		v[0xb + k] = s[0x19 + k];
	v[0x26] = s[0x1c2];
	v[0x28] = s[0x1da];
	v[0x2a] = s[0x1f2];
	v[0x2c] = s[0x20a];
	v[0x2d] = s[0x20b];
	v[0x2e] = s[0x20c];
	for (k = 0; k < 10; k++)
		v[0x31 + k] = s[0x23a + k];
	for (k = 0; k < 6; k++)
		v[0x3b + k] = (u32)(int)(s16)(s[0x244 + 2 * k] | s[0x245 + 2 * k] << 8);
	v[0x41] = s[0x250] | s[0x251] << 8;
	v[0x42] = s[0x252] | s[0x253] << 8;
	v[0x43] = s[0x254];
	v[0x44] = s[0x255];
	v[0x45] = s[0x256] | s[0x257] << 8;
	v[0x46] = s[0x258];
}

/* func_dpc_para_update: the DPC copies, range limits and the correction level enables */
static inline void t33_dpc_para_v(u32 *v)
{
	static const u8 cp[][2] = {
		{ 0x47, 0x00 }, { 0x48, 0x01 }, { 0x49, 0x02 }, { 0x4a, 0x03 }, { 0x4b, 0x04 }, { 0x4c, 0x05 },
		{ 0x4d, 0x06 }, { 0x4e, 0x07 }, { 0x4f, 0x08 }, { 0x50, 0x09 }, { 0x65, 0x0b }, { 0x66, 0x0c },
		{ 0x67, 0x0d }, { 0x68, 0x0e }, { 0x69, 0x0f }, { 0x6a, 0x10 }, { 0x79, 0x11 }, { 0x7a, 0x12 },
		{ 0x7b, 0x13 }, { 0x7e, 0x18 }, { 0x7f, 0x19 }, { 0x80, 0x1a }, { 0x81, 0x1b }, { 0x82, 0x1c },
		{ 0x83, 0x1d }, { 0x84, 0x1e }, { 0x85, 0x1f }, { 0x86, 0x22 }, { 0x87, 0x23 }, { 0x7c, 0x16 },
		{ 0x7d, 0x17 }, { 0x88, 0x26 }, { 0x89, 0x27 },
	};
	u32 lo, hi, lvl;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(cp); k++)
		v[cp[k][0]] = v[cp[k][1]];
	lo = v[0x20];
	hi = v[0x21];
	v[0x72] = v[0x1b] - lo;
	v[0x76] = v[0x1f] + hi;
	v[0x6e] = v[0x17] + hi;
	v[0x6b] = v[0x12] - v[0x14];
	v[0x6d] = v[0x16] - lo;
	lvl = v[10];
	v[0x6f] = v[0x18] - lo;
	v[0x70] = v[0x19] - lo;
	v[0x71] = v[0x1a] - lo;
	v[0x73] = v[0x1c] + hi;
	v[0x74] = v[0x1d] + hi;
	v[0x75] = v[0x1e] + hi;
	v[0x6c] = v[0x15] + v[0x13];
	v[0x77] = v[0x22] - v[0x24];
	v[0x78] = v[0x25] + v[0x23];
	/* level 0 none, 1 .. 3 more of the enables, every other value all of them */
	v[0x51] = lvl != 0;
	v[0x52] = lvl != 0 && lvl != 1;
	v[0x53] = lvl != 0 && lvl != 1 && lvl != 2;
	v[0x54] = lvl != 0 && lvl != 1 && lvl != 2 && lvl != 3;
	v[0x59] = lvl != 0 && lvl != 1;
	v[0x5a] = lvl != 0 && lvl != 1 && lvl != 2;
	v[0x5b] = v[0x54];
	v[0x5f] = v[0x5a];
	v[0x60] = v[0x54];
	v[99] = v[0x54];
	v[0x55] = v[0x51];
	v[0x56] = v[0x52];
	v[0x57] = v[0x53];
	v[0x58] = v[0x54];
	v[0x5c] = v[0x59];
	v[0x5d] = v[0x5a];
	v[0x5e] = v[0x5b];
	v[0x61] = v[0x5f];
	v[0x62] = v[0x60];
	v[100] = v[99];
	v[0x8a] = v[0x46];
	v[0x8b] = 0x200;
}

/* func_ctr_para_update: words 0x28..0x39 into 0x8c..0x9d */
static inline void t33_ctr_para_v(u32 *v)
{
	unsigned int k;

	for (k = 0; k < 18; k++)
		v[0x8c + k] = v[0x28 + k];
}

/* func_cac_para_update: words 0x3a..0x45 into 0x9e..0xa9 */
static inline void t33_cac_para_v(u32 *v)
{
	unsigned int k;

	for (k = 0; k < 12; k++)
		v[0x9e + k] = v[0x3a + k];
}

#endif /* TX_ISP_T33_DPCPAR_H */
