/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rcfgcore": the register-list core
 * (driver/t32/tx_isp_t33_rcfgcore.h) with the vendor calling convention and
 * callees, run against the vendor tx-isp-t33.ko instructions (spec:
 * rcfgcore.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rcfgcore.h"

u8 bo_attr[0x1c00];
u16 bo_regnum[4];
u16 bo_tmo_num;
u8 bo_initpar[0xfc0];
u32 **bo_buf_info;
u32 bo_module;
u32 bo_trig_cnt[4];
u8 bo_refresh;

extern void *private_kmalloc(u32 n, u32 flags);
extern void private_kfree(void *p);
extern void private_dma_cache_sync(void *dev, void *p, u32 n, int dir);
extern void system_reg_write(u32 reg, u32 val);
extern u32 system_reg_read(u32 reg);
extern int isp_printf(int level, const char *fmt, ...);
extern int printk(const char *fmt, ...);

static void *a_alloc(u32 n) { return private_kmalloc(n, 0xd0); }
static void a_zero(void *p, u32 n) { memset(p, 0, n); }
static void a_free(void *p) { private_kfree(p); }
static void a_sync(void *p, u32 n) { private_dma_cache_sync(0, p, n, 0); }
static void a_log(int level, u32 id)
{
	if (level < 0)
		printk("rcfg %u\n", id);
	else
		isp_printf(level, "rcfg %u\n", id);
}

static const struct t33_rcfgc bo_t = {
	.attr = bo_attr, .regnum = bo_regnum, .tmo_num = &bo_tmo_num, .initpar = bo_initpar, .buf_info = &bo_buf_info,
	.module = &bo_module, .trig_cnt = bo_trig_cnt, .refresh = &bo_refresh, .alloc = a_alloc, .zero = a_zero,
	.free = a_free, .sync = a_sync, .wr = system_reg_write, .rd = system_reg_read,
	.log = a_log,
};

u32 bo_register(u32 ch, u32 id, u32 fn) { return t33_rcfg_module_register(&bo_t, ch, id, fn); }
u32 bo_release(u32 ch, u32 id) { return t33_rcfg_module_release(&bo_t, ch, id); }
u32 bo_cfg_trig(u32 ch, u32 id) { return t33_rcfg_module_cfg_trig(&bo_t, ch, id); }
void bo_dual(u32 ch, u32 id) { t33_rcfg_dual_cfg_trig(&bo_t, ch, id); }
u32 bo_write_all(u32 ch) { return t33_rcfg_write_all_reg(&bo_t, ch); }
u32 bo_get_module(void) { return t33_get_rcfg_module(&bo_t); }
void bo_lib_trig(u32 ch) { t33_lib_rcfg_cfg_trig(&bo_t, ch); }
void bo_end_irq(u32 ch) { t33_rcfg_cfg_end_interrupt(&bo_t, ch); }
