/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "gammav" (openimp #99 group 13d-2, #608): the gamma functions on the vendor
 * gamma_para / stLutInfo state (driver/t32/tx_isp_t33_gammav.h) with the vendor calling convention (spec:
 * gammav.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_gammav.h"

unsigned long gamma_para[4];
unsigned long stLutInfo[4];
unsigned long tparamsP[4];
u8 gammaCfgMode;
u8 tispGammaRefresh;
u32 gamma_count[4];

extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);

#define G(ch)	((unsigned long *)gamma_para[ch])
#define TAB(ch)	((u8 *)((unsigned long *)stLutInfo[ch])[8])

int bo_tisp_gamma_api_set(u32 ch, const u8 *attr)
{
	return t33_gv_api_set(ch, G(ch), TAB(ch), attr, gammaCfgMode, tisp_reg_write_t33, &tispGammaRefresh);
}

int bo_tisp_s_Gamma(u32 ch, const u8 *attr)
{
	t33_gv_api_set(ch, G(ch), TAB(ch), attr, gammaCfgMode, tisp_reg_write_t33, &tispGammaRefresh);
	return 0;
}

int bo_tisp_gamma_api_get(u32 ch, u8 *out)
{
	return t33_gv_api_get(G(ch), out);
}

int bo_tisp_g_Gamma(u32 ch, u8 *out)
{
	t33_gv_api_get(G(ch), out);
	return 0;
}

int bo_tisp_gamma_irq_frame_done(u32 ch)
{
	return t33_gv_frame_done(ch, G(ch), gamma_count, &tispGammaRefresh, gammaCfgMode, tisp_reg_write_t33,
				 tisp_rcfg_get_dma_state, tisp_rcfg_module_cfg_trig);
}

int bo_tisp_gamma_strength_transform(u32 ch)
{
	t33_gv_transform(G(ch), TAB(ch));
	return 0;
}

int bo_tisp_gamma_write_lut_rgb(u32 ch)
{
	t33_gv_write_lut(ch, G(ch), gammaCfgMode, tisp_reg_write_t33);
	return 0;
}

int bo_tisp_gamma_ev_update(u32 ch, u32 x, u32 lo, u32 hi)
{
	(void)x;
	return t33_gv_ev_update(ch, G(ch), TAB(ch), lo, hi, gammaCfgMode, tisp_reg_write_t33, tisp_rcfg_get_dma_state,
				&tispGammaRefresh);
}

int bo_tisp_gamma_switch_para(u32 ch, u32 x)
{
	(void)x;
	ch &= 0xff;
	t33_gv_switch(ch, G(ch), (u8 *)tparamsP[ch] + 0x5b80, TAB(ch), gammaCfgMode, tisp_reg_write_t33);
	return 0;
}
