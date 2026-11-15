/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "initC": tisp_csccr_init / tisp_csccr_deinit /
 * tisp_bcsh_deinit (driver/t32/tx_isp_t33_initC.h) with the vendor calling
 * convention, run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include "tx_isp_t33_initC.h"

u32 *csccrInfo[4];
u8 csccrCfgMode;
u8 tispCsccrRefresh;
u32 csccrOutput[4];
u8 start_night[4];
u32 frameCount[4];
void *bcsh_para[4];
u32 *bcshInfo[4];
u8 *tparamsP[4];

u8 t33_csccr_cp[12];
u8 t33_csccr_ap[12];

extern int tisp_rcfg_module_register(u32 ch, u32 mod, void *reg_init);
extern int tisp_rcfg_module_release(u32 ch, u32 mod);
extern int tisp_reg_write_t33(u32 ch, u32 list, u32 slot, u32 val);
extern int tisp_csccr_para_refresh_by_mode(u32 mode, u32 one, u8 *dest);
extern int tisp_csccr_bin_par_update(u32 ch);
extern int system_irq_func_set(u32 ch, u32 irq, void *fn);
extern int lib_tisp_set_csccr(u32 ch, u32 night);
extern void *private_kmalloc(u32 size, u32 flags);
extern void private_kfree(void *p);
extern void private_vfree(void *p);

int tisp_rcfg_csccr_reg_init(u32 ch, u32 list, u32 slot, u32 val) { return 0; }
int tisp_csccr_t33_frame_done_interrupt(u32 ch) { return 0; }

int bo_tisp_csccr_init(u32 ch)
{
	return t33_csccr_init(ch, csccrInfo, tparamsP, &csccrCfgMode, &tispCsccrRefresh,
			      csccrOutput, start_night);
}

int bo_tisp_csccr_deinit(u32 ch)
{
	return t33_csccr_deinit(ch, csccrInfo, start_night, frameCount);
}

int bo_tisp_bcsh_deinit(u32 ch)
{
	return t33_bcsh_deinit(ch, bcsh_para, bcshInfo);
}
