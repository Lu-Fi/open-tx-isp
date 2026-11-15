/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) CCM / CSC small helpers, from the vendor tx-isp-t33.ko:
 *  - tisp_ccm_no_interp: copy the manual matrix words of the CCM state
 *    (ccm_para[ch]->...->) into the block state;
 *  - func_ccm_calc_single_ks.constprop.1: the largest shift 10..0 with
 *    ((a << s) + b / 2) / b < 0x100 (64 bit);
 *  - tisp_csc_get: fill the CSC output block from csc_para[ch].
 */
#ifndef TX_ISP_T33_CCMSM_H
#define TX_ISP_T33_CCMSM_H

#include "tx_isp_t33_blk.h"

static inline void t33_ccm_no_interp(u32 *v, const u32 *par)
{
	v[0x5d] = par[1];
	v[0x5e] = par[2];
	v[0x71] = par[0x1a];
	v[0x7e] = par[0xab];
	v[0x87] = par[0x10c];
	v[0x90] = par[0x16d];
}

static inline unsigned long long t33_ccm_single_ks(u32 a0, u32 a1, u32 a2, u32 a3)
{
	unsigned long long divisor = ((unsigned long long)a3 << 32) | a2;
	unsigned long long shifted;
	unsigned long long half = divisor >> 1;
	unsigned long long num, q;
	int s;

	for (s = 10; s >= 0; s--) {
		shifted = ((unsigned long long)a1 << 32 | a0) << s;
		num = shifted + half;
		q = divisor ? t32_tp_div64(num, divisor) : 0;	/* div64_u64 in the kernel */
		if ((q >> 32) == 0 && (u32)q < 0x100) {
			return ((unsigned long long)(s & 0xff) << 8) | (u32)q;
		}
	}
	return 0xff;
}

static inline int t33_csc_get(const u8 *par, const u8 *fb, u8 *out)
{
	unsigned int k;

	if (!par)
		return -1;
	if (par[98] == 1 || (par[98] == 0 && par[99] < 2)) {
		out[0] = par[91];
		for (k = 0; k < 36; k++)
			out[4 + k] = par[4 + k];
		for (k = 0; k < 6; k++)
			out[40 + k] = par[82 + k];
		for (k = 0; k < 6; k++)
			out[84 + k] = par[92 + k];
		for (k = 0; k < 36; k++)
			out[48 + k] = par[40 + k];
		return 0;
	}
	if (!fb)
		return -1;
	for (k = 0; k < 92; k++)
		out[k] = fb[k];
	return 0;
}

#endif /* TX_ISP_T33_CCMSM_H */
