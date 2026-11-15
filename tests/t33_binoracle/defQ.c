/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "defQ": defog soft path and LCE Gaussian weights
 * (driver/t32/tx_isp_t33_defQ.h) with the vendor calling convention, run
 * against the vendor tx-isp-t33.ko instructions (spec: defQ.conf, FUNCS).
 */
#include <stdint.h>

extern void *memcpy(void *d, const void *s, unsigned int n);

#include "tx_isp_t33_defQ.h"

u32 *t33_defog_info[T33_DEFOG_INFO_WORDS];
u8 t33_defog_refresh;

extern void tisp_defog_interp_trans_k(u32 ch, u32 a1, const void *a2, const void *a3,
				      const void *a4, const void *a5, const void *a6,
				      const void *a7, const void *a8);
extern int dfg_array_cmp(const void *a, const void *b, u32 n);
extern int calculate_exponential(int x, int a, int b, int c);

__attribute__((noinline))
void *memcpy(void *d, const void *s, unsigned int n)
{
	unsigned char *cd = d;
	const unsigned char *cs = s;
	unsigned int i;

	for (i = 0; i < n; i++)
		cd[i] = cs[i];
	return d;
}

void bo_tisp_defog_update_trans_k(u32 ch) { t33_defog_update_trans_k(ch); }

int bo_tisp_defog_ev_update(u32 ch, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	return t33_defog_ev_update(ch, arg1, arg2, arg3, arg4, arg5);
}

void bo_tisp_lce_gauss_wgt(const u8 *p, u8 *w) { t33_lce_gauss_wgt(p, w); }
