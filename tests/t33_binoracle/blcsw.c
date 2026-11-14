/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "blcsw": black level gain update / switch / init with the vendor calling
 * convention (spec: blcsw.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_blcsw.h"

extern void tisp_blc_write_reg(u32 ch);
extern int tisp_rcfg_module_register(u32 ch, u32 mod, void *fn);
extern void *private_vmalloc(u32 size);
extern int printk(const char *fmt, ...);
extern void system_irq_func_set(u32 ch, u32 irq, void *fn);

struct t33_blc_info *blc_para[4];
u8 *tparamsP[4];

static const u8 dflt[64];

static void w_write(u32 ch) { tisp_blc_write_reg(ch); }
static int w_reg(u32 ch) { return tisp_rcfg_module_register(ch, 4, 0); }
static void *w_alloc(u32 sz) { return private_vmalloc(sz); }
static void w_log(u32 line, int val) { printk("%s %d %d", "tisp_blc_init", line, val); }
static void w_irq(u32 ch) { system_irq_func_set(ch, 40, 0); }

static const struct t33_blc_ops ops = {
	w_write, w_reg, w_alloc, w_log, w_irq, dflt, dflt, dflt, dflt,
};

int bo_tisp_blc_tgain_update(u32 ch, u32 tgain)
{
	return t33_blc_tgain_update(ch, blc_para[ch], tgain, w_write);
}

int bo_tisp_blc_switch_para(u32 ch)
{
	return t33_blc_switch_para(ch, blc_para[ch], tparamsP[ch], w_write);
}

int bo_tisp_blc_interp_by_tgain(u32 ch, u32 tgain, u32 flag)
{
	(void)flag;
	t33_blc_interp(blc_para[ch], tgain);
	return 0;
}

int bo_tisp_blc_init(u32 a0, const u8 *isp)
{
	u32 ch = a0 & 0xff;

	return t33_blc_init_ch(ch, isp, tparamsP[ch], &blc_para[ch], &ops);
}
