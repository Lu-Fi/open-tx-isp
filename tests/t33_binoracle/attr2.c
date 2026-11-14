/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "attr2": gamma / autozoom / module ratio ioctl attribute wrappers
 * (driver/t32/tx_isp_t33_attr2.h) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_attr2.h"

extern int tisp_g_Gamma(u32 a0, void *attr);
extern int tisp_s_Gamma(u32 a0, void *attr);
extern int tisp_g_autozoom_control(u32 a0, void *attr);
extern int tisp_g_module_ratio_attr(u32 a0, void *attr);
extern int tisp_s_module_ratio_attr(u32 a0, void *attr);
extern int private_copy_to_user(void *to, void *from, unsigned long n);
extern int private_copy_from_user(void *to, void *from, unsigned long n);
extern int isp_printf(int level, const char *fmt, const char *func, int line);

int bo_tx_isp_gamma_g_attr(u32 a0, const u8 *p)
{
	return t33_gamma_g_attr(a0, p, tisp_g_Gamma, private_copy_to_user, isp_printf);
}

int bo_tx_isp_gamma_s_attr(u32 a0, u32 user)
{
	return t33_gamma_s_attr(a0, user, tisp_s_Gamma, private_copy_from_user, isp_printf);
}

int bo_tx_isp_autozoom_g_attr(u32 a0, const u8 *p)
{
	return t33_autozoom_g_attr(a0, p, tisp_g_autozoom_control, private_copy_to_user, isp_printf);
}

int bo_tx_isp_module_ratio_g_attr(u32 a0, const u8 *p)
{
	return t33_ratio_g_attr(a0, p, tisp_g_module_ratio_attr, private_copy_to_user, isp_printf);
}

int bo_tx_isp_module_ratio_s_attr(u32 a0, u32 user)
{
	return t33_ratio_s_attr(a0, user, tisp_s_module_ratio_attr, private_copy_from_user, isp_printf);
}
