/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "ldc": LDC parameter state helpers with the vendor calling convention. */
#include <stdint.h>
#include "tx_isp_t33_ldcpar.h"

u8 ldcParExist[8];
u8 stLdcPar[640];

extern void tisp_top_ldc_bypass_refresh(u32 idx, u8 *blk);
extern int isp_printf(int level, const char *fmt, ...);

void bo_tisp_ldc_set_bypass_state(u32 idx, u32 state)
{
	u8 b[4];
	u32 i = t33_ldc_bypass_buf(b, idx, state);

	tisp_top_ldc_bypass_refresh(i, b);
}

int bo_tisp_ldc_get_extra_par(u32 idx, u8 *info)
{
	int r = t33_ldc_extra_par(ldcParExist, stLdcPar, idx, info);

	if (r)
		isp_printf(1, "ldc extra par size", "tisp_ldc_get_extra_par", 391, 0);
	return r;
}

void bo_tisp_ldc_reset_par_state(u32 idx)
{
	t33_ldc_reset_par(ldcParExist, idx);
}
