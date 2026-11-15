/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "parA": YDNS / SDNS parameter refresh
 * (driver/t32/tx_isp_t33_parA.h) with the vendor calling convention, run
 * against the vendor tx-isp-t33.ko instructions (spec: parA.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_parA.h"

/* callees: neither side runs them, calls are recorded and compared */
extern void func_ydns_bin_para_update(u32 ch);
extern void func_ydns_para_update(u32 ch);
extern void tisp_sdns_other_params_transform(u32 ch);
extern void func_sdns_para_update(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);

/* module globals (GLOBAL lines) */
u8 ydnsCfgMode;
u8 sdnsCfgMode;
u8 tispYdnsRefresh;
u8 tispSdnsRefresh;

static const struct t33_par_ops ydns_ops = {
	.reg_write = tisp_reg_write_t33,
	.dma_state = tisp_rcfg_get_dma_state,
	.pre_update = func_ydns_bin_para_update,
	.para_update = func_ydns_para_update,
};

static const struct t33_par_ops sdns_ops = {
	.reg_write = tisp_reg_write_t33,
	.dma_state = tisp_rcfg_get_dma_state,
	.pre_update = tisp_sdns_other_params_transform,
	.para_update = func_sdns_para_update,
};

int bo_tisp_ydns_par_refresh(u32 ch, u32 gain, u32 *inf)
{
	return (int)t33_ydns_par_refresh(ch, gain, inf, ydnsCfgMode, &ydns_ops,
					  &tispYdnsRefresh);
}

int bo_tisp_sdns_par_refresh(u32 ch, u32 gain, u32 tol, u32 *inf)
{
	return (int)t33_sdns_par_refresh(ch, gain, tol, inf, sdnsCfgMode,
					  &sdns_ops, &tispSdnsRefresh);
}
