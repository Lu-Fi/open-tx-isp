/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "r12D": AE antiflick / faceae, core register window, awb trigger, irq cb table. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r12D.h"

u8 *t33_g_stAeInfo[4];
u8 *t33_g_ae_cache[4];
u8 *t33_g_tparamsP[4];
u8 *t33_g_ispcore_sd;
u32 t33_g_irq_func_cb[128];

extern void tisp_ae_clac_deflicker_cfg(u32 ch);
extern u32 tisp_reg_write_t33(u32 a, u32 b, u32 c, u32 d);

static u32 r12d_mode2_unused(u32 v)
{
	return v;	/* vendor: tail jump to an unnamed function, not exercised */
}

void bo_tisp_ae_api_get_antiflick(u32 ch, u8 *out)
{
	t33_ae_get_antiflick(t33_g_stAeInfo, t33_g_tparamsP, ch, out);
}

u32 bo_tisp_ae_api_get_faceae(u32 ch, u8 *out)
{
	return t33_ae_get_faceae(t33_g_ae_cache, ch, out);
}

void bo_tisp_ae_api_set_antiflick(u32 ch, u8 *in)
{
	t33_ae_set_antiflick(t33_g_stAeInfo, t33_g_ae_cache, ch, in, tisp_ae_clac_deflicker_cfg);
}

u32 bo_system_reg_read(u32 off)
{
	return t33_sys_reg_read(t33_g_ispcore_sd, off);
}

u32 bo_system_reg_write(u32 off, u32 val)
{
	return t33_sys_reg_write(t33_g_ispcore_sd, off, val);
}

u32 bo_system_reg_set_awb_trig(u32 mode, u32 val)
{
	return t33_sys_reg_set_awb_trig(mode, val, tisp_reg_write_t33, r12d_mode2_unused);
}

void bo_system_irq_func_set(u32 a0, u32 idx, u32 fn)
{
	(void)a0;
	t33_irq_func_set(t33_g_irq_func_cb, idx, fn);
}
