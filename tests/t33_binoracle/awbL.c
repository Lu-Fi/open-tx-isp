/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "awbL": tisp_t33_awb_long_alogrithm / tisp_t33_awb_sat_weight (driver/t32/tx_isp_t33_awbL.h)
 * with the vendor calling convention: they take the module statics awbInfo, stAwbStatBuff and num_stat.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_awbL.h"

void *awbInfo[4];
void *stAwbStatBuff[4];
u32 num_stat[4];

extern u32 system_reg_read(u32 reg);
extern u32 fix_point_mult2_32(u32 bits, u32 a, u32 b);
extern unsigned long long fix_point_div(u32 bits, unsigned long long num, unsigned long long den);
extern void kernel_mxu_begin_partial(u32 mask);
extern void kernel_mxu_end(void);
extern void tisp_awb_ct_detect_simd_3(u8 *d, u32 *rb, u32 *flag);

static void bo_ops(struct t33_awbL_ops *o)
{
	o->reg_read = system_reg_read;
	o->mult = fix_point_mult2_32;
	o->div = fix_point_div;
	o->mxu_begin = kernel_mxu_begin_partial;
	o->ct_detect = tisp_awb_ct_detect_simd_3;
	o->mxu_end = kernel_mxu_end;
}

void bo_tisp_t33_awb_sat_weight(u32 ch, u8 *out, u32 *wt)
{
	struct t33_awbL_ops o;
	u32 *info = (u32 *)awbInfo[ch];
	u32 *stat = (u32 *)stAwbStatBuff[ch];

	bo_ops(&o);
	t33_awbL_sat_weight(&o, (u8 *)(uintptr_t)info[0], (const u8 *)(uintptr_t)stat[1], out, wt);
}

void bo_tisp_t33_awb_long_alogrithm(u8 *d, u32 ch)
{
	struct t33_awbL_ops o;
	u32 *info = (u32 *)awbInfo[ch];
	u32 *stat = (u32 *)stAwbStatBuff[ch];

	bo_ops(&o);
	t33_awbL_long(&o, d, (u8 *)(uintptr_t)info[0], (u8 *)(uintptr_t)info[1], (u8 *)(uintptr_t)info[4],
		      (u8 *)&info[5], (const u8 *)(uintptr_t)stat[1], num_stat[ch]);
}
