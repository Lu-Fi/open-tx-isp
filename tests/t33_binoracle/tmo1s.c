/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "tmo1s": TMO one-shot / AOV / multi-sensor V-table glue (driver/t32/tx_isp_t33_tmo1s.h)
 * with the vendor calling convention and the vendor's globals. */
#include <stdint.h>
#include "tx_isp_t33_tmo1s.h"

u8 tmoCfgMode, aovTikFlag, mulsensornum;
u16 tisp_t33_tmo_rcfg_reg_num;
u8 stIspInitPar[4096];
u8 *tmoInfo[4];
u8 stModuleAttr[0xe00];

extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);
extern void tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);
extern void tisp_tmo_ramV_vi0_reg_refresh(u32 ch);

static u8 *bo_info(u32 ch) { return tmoInfo[ch]; }
static u32 *bo_list(u32 ch) { return *(u32 **)(stModuleAttr + ch * 0x700 + 0xe8); }

static const struct t33_tmo1s bo_t = {
	&tmoCfgMode, &aovTikFlag, &mulsensornum, &tisp_t33_tmo_rcfg_reg_num, stIspInitPar, bo_info, bo_list,
	tisp_reg_write_t33, tisp_rcfg_module_cfg_trig, tisp_tmo_ramV_vi0_reg_refresh,
};

void bo_lib_tisp_tmo_oneshot_disable(void) { t33_tmo1s_disable(&bo_t); }
void bo_lib_tisp_tmo_oneshot_enable(void) { t33_tmo1s_enable(&bo_t); }
void bo_lib_tisp_tmo_oneshot_tick(void) { t33_tmo1s_tick(&bo_t); }
void bo_tisp_tmo_sleep(void) { t33_tmo1s_sleep(&bo_t); }
int bo_tisp_tmo_dma_interrupt(void) { return t33_tmo1s_dma_irq(); }
int bo_tisp_tmo_rcfg_reg_num_enV(u32 ch) { return t33_tmo1s_reg_num(&bo_t, ch, T33_TMO1S_LIST_LONG); }
void bo_tisp_tmo_ramV_vi23_reg_ctrl(u32 ch) { t33_tmo1s_vi23_ctrl(&bo_t, ch); }
void bo_tisp_tmo_ramV_vi23_reg_refresh(u32 ch) { t33_tmo1s_vi23_refresh(&bo_t, ch); }
void bo_tisp_tmo_wake_up(u32 ch) { t33_tmo1s_wake_up(&bo_t, ch); }
