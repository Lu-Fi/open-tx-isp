/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle block "r11A": tisp_awb_long_par_update / tisp_awb_spec_calculate with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r11A.h"

extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 slot, u32 val);
extern u32 system_reg_read(u32 reg);
extern u32 fix_point_mult2_32(u32 bits, u32 a, u32 b);

/* the vendor's awbInfo: per channel a pointer to { par, st, .., .., aux } */
u32 *awbInfo[4] = { (u32 *)1, (u32 *)1, (u32 *)1, (u32 *)1 };

void bo_tisp_awb_long_par_update(u32 ch, u8 *tbl)
{
	u32 *info = awbInfo[ch];

	t33_awb_long_par_update((u8 *)(unsigned long)info[0], (u8 *)(unsigned long)info[1], info[4], tbl);
}

void bo_tisp_awb_spec_calculate(u32 ch)
{
	struct t33_r11A_ops ops;
	u32 *info = awbInfo[ch];

	ops.reg_write = tisp_reg_write_t33;
	ops.reg_read = system_reg_read;
	ops.mult = fix_point_mult2_32;
	t33_awb_spec_calculate(&ops, ch, (u8 *)(unsigned long)info[0], (u8 *)(unsigned long)info[1]);
}
