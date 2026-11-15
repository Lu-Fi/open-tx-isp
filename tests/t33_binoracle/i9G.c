/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "i9G": tisp_dpc_init (driver/t32/tx_isp_t33_i9G.h) with the vendor calling
 * convention; kernel services are the vendor-named externs (recorded by the oracle, CALLS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9G.h"

extern int tisp_rcfg_module_register(u32 ch, u32 n, void *fn);
extern int printk(const char *fmt, ...);
extern void *private_kmalloc(u32 size, u32 flags);
extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 where, u32 val);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern void system_irq_func_set(u32 ch, u32 irq, void *fn);

u32 *dpcInfo_o[4];
u8 *tparamsP_o[4];
u8 dpcCfgMode_o;
u8 tispDpcRefresh_o;

static int c_register(u8 ch, u32 mod) { return tisp_rcfg_module_register(ch, mod, 0); }
static void c_warn(int ret) { printk("tisp_dpc_init: register failed %d\n", ret); }
static void *c_alloc(u32 size, u32 flags) { return private_kmalloc(size, flags); }
static void c_zero(void *p, u32 n) { memset(p, 0, n); }
static void c_write(u8 ch, u32 mod, u32 where, u32 val) { tisp_reg_write_t33(ch, mod, where, val); }
static int c_dma(u32 mod) { return tisp_rcfg_get_dma_state(mod); }
static void c_irq(u8 ch, u32 irq) { system_irq_func_set(ch, irq, 0); }

static const struct t33_dpc_init_ops ops = {
	c_register, c_warn, c_alloc, c_zero, c_write, c_dma, c_irq,
};

int bo_tisp_dpc_init(u32 a0, u8 *st)
{
	u32 gain = st[928] | st[929] << 8 | st[930] << 16 | (u32)st[931] << 24;

	return t33_dpc_init_vendor(&ops, (u8)a0, gain, dpcInfo_o, tparamsP_o, dpcCfgMode_o, &tispDpcRefresh_o);
}
