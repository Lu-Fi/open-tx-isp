/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "i9A": tisp_ydns_init / tisp_sdns_init / tisp_cdns_init */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9A.h"

extern int tisp_rcfg_module_register(u32 ch, u32 id, void *cb);
extern void *private_kmalloc(u32 size, u32 flags);
extern void tisp_ydns_refresh(u32 ch, u32 gain);
extern void tisp_sdns_refresh(u32 ch, u32 gain);
extern void tisp_cdns_refresh(u32 ch, u32 gain);
extern void system_irq_func_set(u32 ch, u32 idx, void *cb);
extern int printk(const char *fmt, ...);

u32 o_ydnsInfo[256];
u32 o_sdnsInfo[256];
u32 o_cdnsInfo[256];
u32 o_tparamsP[256];
u8 o_tispCdnsRefresh;

static void no_alloc_fail(u32 line)
{
	(void)line;
}

static void reg_fail(u32 line, int ret)
{
	printk("i9A %u %d", line, ret);
}

static void fill(struct t33_dnsi_ops *o, void (*refresh)(u32, u32))
{
	o->reg_register = tisp_rcfg_module_register;
	o->alloc = private_kmalloc;
	o->alloc_fail = no_alloc_fail;
	o->reg_fail = reg_fail;
	o->refresh = refresh;
	o->irq_set = system_irq_func_set;
	o->reg_cb = (void *)1;
	o->irq_cb = (void *)1;
}

int bo_tisp_ydns_init(u32 ch, u8 *par)
{
	struct t33_dnsi_ops o;

	fill(&o, tisp_ydns_refresh);
	return t33_ydns_init_ch(ch, par, o_ydnsInfo, o_tparamsP, &o);
}

int bo_tisp_sdns_init(u32 ch, u8 *par)
{
	struct t33_dnsi_ops o;

	fill(&o, tisp_sdns_refresh);
	return t33_sdns_init_ch(ch, par, o_sdnsInfo, o_tparamsP, &o);
}

int bo_tisp_cdns_init(u32 ch, u8 *par)
{
	struct t33_dnsi_ops o;

	fill(&o, tisp_cdns_refresh);
	return t33_cdns_init_ch(ch, par, o_cdnsInfo, o_tparamsP, &o_tispCdnsRefresh, &o);
}
