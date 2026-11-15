/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "clmP": CSC refresh / attribute glue with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_clmP.h"

extern int tisp_csc_get(u32 ch, u8 *dst);
extern int tisp_clm_calc_para_refresh(u32 ch);
extern int tisp_clm_t33_write_reg(u32 ch);
extern int tisp_bcsh_interp_by_ev(u32 ch, u32 a1, u32 lo, u32 hi, u32 force);
extern int tisp_bcsh_interp_by_ct(u32 ch, u32 a1, u32 ct, u32 z, u32 force);
extern int tisp_bcsh_calc_para_refresh(u32 ch, u32 mode);
extern int tisp_csc_api_get(u32 ch, u32 a1);
extern int tisp_csc_api_set(u32 ch, u32 a1);

u8 *bo_clm_info[2];
u8 *bo_bcsh_para[2];

static const struct t33_clmp_ops bo_ops = {
	tisp_csc_get, tisp_clm_calc_para_refresh, tisp_clm_t33_write_reg, tisp_bcsh_interp_by_ev,
	tisp_bcsh_interp_by_ct, tisp_bcsh_calc_para_refresh, tisp_csc_api_get, tisp_csc_api_set,
};

int bo_tisp_clm_refresh_by_csc(u32 ch) { return t33_clm_refresh_by_csc(&bo_ops, bo_clm_info, ch); }
int bo_tisp_bcsh_refresh_by_csc(u32 ch) { return t33_bcsh_refresh_by_csc(&bo_ops, bo_bcsh_para, ch); }
int bo_tisp_get_csc_attr(u32 ch, u32 a1) { return t33_get_csc_attr(&bo_ops, ch, a1); }
int bo_tisp_set_csc_attr(u32 ch, u32 a1) { return t33_set_csc_attr(&bo_ops, bo_bcsh_para, bo_clm_info, ch, a1); }
