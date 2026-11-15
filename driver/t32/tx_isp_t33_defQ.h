/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) defog soft path and LCE Gaussian weights (openimp #99).
 *
 *  - tisp_defog_update_trans_k: per-channel transmission curve refresh
 *    (defog_info[ch] block state, the 5-byte curve at +0x104, the
 *    0xf mask of the 180-byte statistics area at +0xc6a).
 *  - tisp_defog_ev_update: EV / strength change of a channel, calls
 *    tisp_defog_update_trans_k when the block is enabled.
 *  - tisp_lce_gauss_wgt: 32 Gaussian weights from the three section bytes
 *    (sigma, scale, centre) via calculate_exponential.
 */
#ifndef TX_ISP_T33_DEFQ_H
#define TX_ISP_T33_DEFQ_H

#include "tx_isp_t33_blk.h"

/* defog_info[]: the vendor's per-channel block state pointers */
#define T33_DEFOG_INFO_WORDS 0x104
extern u32 *t33_defog_info[T33_DEFOG_INFO_WORDS];
/* tispDefogRefresh: set by tisp_defog_ev_update */
extern u8 t33_defog_refresh;

extern void tisp_defog_interp_trans_k(u32 ch, u32 a1, const void *a2, const void *a3,
				      const void *a4, const void *a5, const void *a6,
				      const void *a7, const void *a8);
extern int dfg_array_cmp(const void *a, const void *b, u32 n);
extern int calculate_exponential(int x, int a, int b, int c);

/* tisp_defog_update_trans_k: refresh channel ch's transmission curve */
static inline void t33_defog_update_trans_k(u32 ch)
{
	u32 *v = t33_defog_info[ch];
	u8 *s0 = (u8 *)(unsigned long)v[3];
	u8 *s1 = (u8 *)(unsigned long)v[4];
	u8 cur[5];
	u8 *p;
	int i;

	for (i = 0; i < 5; i++)
		cur[i] = s0[0x104 + i];
	if (s0[0x252])
		return;
	if (*(u32 *)(s0 + 4) == v[6])
		return;
	*(u32 *)(s0 + 4) = v[6];
	tisp_defog_interp_trans_k(ch & 0xff, v[6], s0 + 8, s0 + 0x266, s0 + 0x26f,
				  s0 + 0x278, s0 + 0x281, s0 + 0x28a, s0 + 0x104);
	if (dfg_array_cmp(cur, s0 + 0x104, 5))
		return;
	for (p = s1 + 0xc6a; p < s1 + 0xd1e; p++)
		*p &= 0xf;
}

/* tisp_defog_ev_update: key = (a3 << 22) | (a2 >> 10); returns 0 */
static inline int t33_defog_ev_update(u32 ch, u32 ignored, u32 a2, u32 a3, u32 a4, u32 a5)
{
	u32 *v = t33_defog_info[ch];
	u32 key = (a3 << 22) | (a2 >> 10);
	u8 *s0 = (u8 *)(unsigned long)v[3];

	(void)ignored;
	if (key != v[6]) {
		if (s0[0xc3] == 0)
			t33_defog_update_trans_k(ch);
	}
	v[6] = key;
	if (v[7] != a4 || a5 != 0) {
		if (s0[0xc3] == 1) {
			v[7] = a4;
			t33_defog_update_trans_k(ch);
		}
	}
	t33_defog_refresh = 1;
	return 0;
}

/* signed divide as the MIPS div of the vendor code: a zero divisor leaves the dividend */
static inline int t33_sdiv(int a, int b)
{
	return b ? a / b : a;
}

/* tisp_lce_gauss_wgt: 32 weights into w[0..31] from sigma p[1], scale p[0], centre p[2] */
static inline void t33_lce_gauss_wgt(const u8 *p, u8 *w)
{
	int s1 = 2 * p[1] * p[1];
	int s2 = p[0] << 12;
	int s5 = p[2];
	int s6 = s1 >> 1;
	int i, x, e, v;

	for (i = 0; i < 32; i++) {
		x = (i << 3) - s5;
		x = x * x;
		x = (x << 12) + s6;
		x = t33_sdiv(x, s1);
		e = calculate_exponential(x, 10, 12, 12);
		v = (e >> 1) + s2;
		w[i] = (u8)(v / e);
	}
}

#endif /* TX_ISP_T33_DEFQ_H */
