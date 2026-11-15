/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "i9L": dmsc / dpc / ysp deinit with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9L.h"

extern void private_kfree(void *p);
extern void private_vfree(void *p);
extern int tisp_rcfg_module_release(u32 ch, u32 id);
extern int printk(const char *fmt, const char *func, int line, int ret);

u32 dmsc_info_o[4];
u32 dpc_info_o[4];
u32 ysp_info_o[4];
u32 ysp_params_o[4];

static void h_kfree(u32 p) { private_kfree((void *)(uintptr_t)p); }
static void h_vfree(u32 p) { private_vfree((void *)(uintptr_t)p); }
static int h_release(u32 ch, u32 id) { return tisp_rcfg_module_release(ch, id); }
static void h_err(u32 line, int ret) { printk("", "", (int)line, ret); }

static const struct t33_i9l_ops ops = { h_kfree, h_vfree, h_release, h_err };

void bo_tisp_dmsc_deinit(u32 ch) { t33_dmsc_deinit_ch(&ops, dmsc_info_o, ch); }
void bo_tisp_dpc_deinit(u32 ch) { t33_dpc_deinit_ch(&ops, dpc_info_o, ch); }
void bo_tisp_ysp_deinit(u32 ch) { t33_ysp_deinit_ch(&ops, ysp_info_o, ysp_params_o, ch); }
