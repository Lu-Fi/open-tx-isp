/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "misc1" (spec: misc1.conf) */
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

extern u32 system_reg_read(u32 reg);
extern void system_reg_write(u32 reg, u32 val);
extern int isp_printf(int level, const char *fmt, ...);
extern void __udelay(u32 us);
extern void tisp_ae_clac_deflicker_cfg(u32 ch);
extern void tisp_rcfg_module_cfg_trig(u32 ch, u32 id);

#include "tx_isp_t33_misc1.h"

u8 *oe_ae_cache[4];

void bo_lib_tisp_func_reset(void) { t33_func_reset(); }
u32 bo_lib_tisp_get_ipc_all_cache_data(void) { return t33_ipc_all_cache(); }
u32 bo_lib_tisp_get_ipc_cache_data_byindex(u32 idx) { return t33_ipc_cache_byindex(idx); }
void bo_normal_ae_dn_par_refresh(u32 ch) { t33_ae_dn_par_refresh(oe_ae_cache, ch); }
