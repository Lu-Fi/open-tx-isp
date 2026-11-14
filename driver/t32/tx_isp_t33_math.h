/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) math helpers: the vendor's fixpoint_sqrt and the two
 * lib_tisp_* thunks (lib_tisp_fix_point_mult2_32, lib_tisp_log2_fixed_to_fixed)
 * that tail-call the shared T32 runtime (fix_point_mult2_32 in
 * tx_isp_t33_awb.h, tisp_log2_fixed_to_fixed elsewhere).
 */
#ifndef TX_ISP_T33_MATH_H
#define TX_ISP_T33_MATH_H

#include "tx_isp_t33_blk.h"

/* shared T32 runtime the lib_tisp_* thunks tail-call (proven elsewhere) */
extern int fix_point_mult2_32(int a, int b, int shift);
extern int tisp_log2_fixed_to_fixed(int x, int y, u32 frac);

/*
 * fixpoint_sqrt: integer square root of x (x < 2 -> x), Newton from g for at
 * most iters steps, then rounded to nearest (the vendor's own loop, distinct
 * from t33_fix_sqrt / t33_isqrt_round: it keeps the last y in a1 and tests
 * (2y+1)^2/4 and (2y-1)^2/4 against x).
 */
static inline int t33_fixpoint_sqrt(int x, int g, int iters)
{
	int k, y, c;

	if (x < 2)
		return x;
	y = g;
	for (k = 0; k < iters; k++) {
		int n = (x / y + y + 1) / 2;

		if (n == y)
			break;
		y = n;
	}
	c = 2 * y + 1;
	if (x >= c * c / 4)
		return y + 1;
	c = 2 * y - 1;
	return y - (x < c * c / 4);
}

/* lib_tisp_fix_point_mult2_32: tail call of fix_point_mult2_32 (shared T32) */
static inline int t33_fix_point_mult2_32(int a, int b, int shift)
{
	return fix_point_mult2_32(a, b, shift);
}

/* lib_tisp_log2_fixed_to_fixed: tail call of tisp_log2_fixed_to_fixed, a2 masked to 8 bits */
static inline int t33_log2_fixed_to_fixed(int x, int y, u32 frac)
{
	return tisp_log2_fixed_to_fixed(x, y, frac & 0xff);
}

#endif /* TX_ISP_T33_MATH_H */
