/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r13C": fix_point_div / mult2 / mult3 families with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>

extern int isp_printf(int level, const char *fmt, const char *func, int line);
#define T33_FP_DIV_ZERO(line)	isp_printf(2, "", "", (line))

#include "tx_isp_t33_r13C.h"

unsigned long long bo_fix_point_div(u32 s, unsigned long long a, unsigned long long b)
{
	return t33_fix_point_div(s, a, b);
}
u32 bo_fix_point_div_32(u32 s, u32 a, u32 b) { return t33_fix_point_div_32(s, a, b); }
unsigned long long bo_fix_point_div_64(u32 s, unsigned long long a, unsigned long long b)
{
	return t33_fix_point_div_64(s, a, b);
}
unsigned long long bo_fix_point_mult2(u32 s, unsigned long long a, unsigned long long b)
{
	return t33_fp_mult2(s, a, b);
}
u32 bo_fix_point_mult2_32(u32 s, u32 a, u32 b) { return t33_fix_point_mult2_32(s, a, b); }
unsigned long long bo_fix_point_mult2_64(u32 s, unsigned long long a, unsigned long long b)
{
	return t33_fix_point_mult2_64(s, a, b);
}
unsigned long long bo_fix_point_mult3(u32 s, unsigned long long a, unsigned long long b, unsigned long long c)
{
	return t33_fix_point_mult3(s, a, b, c);
}
u32 bo_fix_point_mult3_32(u32 s, u32 a, u32 b, u32 c) { return t33_fix_point_mult3_32(s, a, b, c); }
unsigned long long bo_fix_point_mult3_64(u32 s, unsigned long long a, unsigned long long b, unsigned long long c)
{
	return t33_fix_point_mult3_64(s, a, b, c);
}
