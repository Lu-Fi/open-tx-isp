/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "r13D": AE helpers with the vendor calling convention (spec: r13D.conf). */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r13D.h"

u8 *bo_stAeInfo[4];
u8 *bo_ae_cache[4];

u32 bo_tisp_ae_lib_bilinear_intp(unsigned long long x, unsigned long long p, unsigned long long q, u32 d, u32 e)
{
	return t33_ae_bilinear_intp(x, p, q, d, e);
}

u32 bo_tisp_ae_calc_convergence_speed(u8 *p, u16 *up, u16 *dn, u32 a3, u32 shift, u32 v)
{
	return t33_ae_conv_speed(p, up, dn, a3, shift, v);
}

u32 bo_tisp_ae_fps_calc_max_exp(u32 ch, u32 fps)
{
	return t33_ae_fps_calc_max_exp(ch, fps, bo_stAeInfo, bo_ae_cache);
}

u32 bo_tisp_ae_max_exp_calc_fps(u32 ch, u32 exp)
{
	return t33_ae_max_exp_calc_fps(ch, exp, bo_stAeInfo, bo_ae_cache);
}

void bo_tisp_ae_sort_H2L(int *v, int *w)
{
	t33_ae_sort_h2l(v, w);
}

u32 bo_tisp_ae_long_target(unsigned long long v, u8 *tab, u8 *y, u32 shift)
{
	return t33_ae_long_target(v, tab, y, shift);
}

void bo_tisp_ae_init_par_calc(u32 ch)
{
	t33_ae_init_par_calc(ch, bo_stAeInfo, bo_ae_cache);
}

void bo_tisp_ae_clac_deflicker_cfg(u32 ch)
{
	t33_ae_clac_deflicker_cfg(ch, bo_stAeInfo, bo_ae_cache);
}
