/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "r13E": vendor calling conventions of the ported functions. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r13E.h"

u32 gamma_para[4];
u32 defog_info[4];

int bo_tisp_gamma_interp_by_ev(u32 idx, u32 a1, u32 lo, u32 hi, u32 force)
{
	const u32 *blk = (const u32 *)(uintptr_t)gamma_para[idx & 3];

	(void)a1;
	return t33_gamma_interp_by_ev((const u8 *)(uintptr_t)blk[0], (u8 *)(uintptr_t)blk[1],
				      (u32 *)(uintptr_t)blk[2], lo, hi, force);
}

void bo_tisp_defog_strength_itp(u32 s, const u8 *src, u8 *dst) { t33_defog_strength_itp(s, src, dst); }

void bo_tisp_defog_interp_trans_k(u32 ch, u32 ev, const u8 *kn, const u8 *t0, const u8 *t1, const u8 *t2,
				  const u8 *t3, const u8 *t4, u8 *out)
{
	const u8 *info = (const u8 *)(uintptr_t)defog_info[ch & 3];
	const u8 *tab[5];

	tab[0] = t0; tab[1] = t1; tab[2] = t2; tab[3] = t3; tab[4] = t4;
	t33_defog_trans_k((const u8 *)(uintptr_t)t33_le32(info + 12), t33_le32(info + 28), ev, kn, tab, out);
}

u32 bo_tisp_bcsh_itp(u32 x, u32 lo, u32 hi, u32 a, u32 b) { return t33_bcsh_itp(x, lo, hi, a, b); }
int bo_tisp_clm_itp(int a, int b, u32 w) { return t33_clm_itp(a, b, w); }
int bo_tisp_ccm_sub_interp(u32 w, int a, int b) { return t33_ccm_sub_interp(w, a, b); }

u32 bo_ISPAWBInterpolation1(u32 a0, u32 sh, u32 x, u32 lo, u32 hi, u32 p, u32 q)
{
	(void)a0;
	return t33_awb_interp1(sh, x, lo, hi, p, q);
}

u32 bo_ISPAWBInterpolation2(u32 a0, u32 sh, u32 x, u32 lo, u32 hi, u32 p, u32 q)
{
	(void)a0;
	return t33_awb_interp2(sh, x, lo, hi, p, q);
}

int bo_func_zone_ct_weight(u32 v, const u8 *z, const u8 *t, u32 bits, u32 arg, u8 *out)
{
	return t33_zone_ct_weight(v, z, t, bits, arg, out);
}
