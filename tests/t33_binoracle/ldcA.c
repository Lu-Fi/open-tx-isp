/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "ldcA": LDC init / DMA config / list load with the vendor calling convention.
 * Kernel / module callees are extern (CALLS in ldcA.conf); module statics are plain globals (GLOBAL lines).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_ldcA.h"

extern void *private_kmalloc(u32 size, u32 flags);
extern int isp_printf(int lvl, const char *fmt, ...);
extern int printk(const char *fmt, ...);
extern int tisp_rcfg_module_register(u32 ch, u32 mod, void *fn);
extern int tisp_rcfg_ldc_reg_init(void *p);
extern void system_reg_write(u32 reg, u32 val);
extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 slot, u32 val);
extern void tisp_top_ldc_bypass_refresh(u32 ch);

u32 *bo_stLDCAttr[4];
u32 bo_stMemInfo[76];
u32 bo_st_dma_cfg[92];
u8 bo_stLdcPar[320];
u8 bo_LdcInfo[2];
u8 bo_tispLdcRefresh[4];
u8 bo_ldcCfgMode;
u32 bo_stIspInitPar[227];
u16 bo_maxWidth, bo_maxHeight, bo_maxOutWidth, bo_maxOutHeight;

/* through pointers so the compiler keeps the calls */
static void *(*volatile ms)(void *, int, size_t) = memset;
static void *(*volatile mc)(void *, const void *, size_t) = (void *(*)(void *, const void *, size_t))memcpy;

static void *op_kmalloc(u32 s, u32 f) { return private_kmalloc(s, f); }
static void op_memclr(void *p, u32 n) { ms(p, 0, n); }
static int op_rcfg_register(u32 ch) { return tisp_rcfg_module_register(ch, 25, (void *)tisp_rcfg_ldc_reg_init); }
static void op_err_alloc(void) { isp_printf(2, (const char *)0, (const char *)0, 61); }
static void op_err_register(int r) { printk((const char *)0, r); }
static void op_par_reset(void *d) { mc(d, (const void *)0, 80); }
static void op_reg_write(u32 r, u32 v) { system_reg_write(r, v); }
static void op_reg_write33(u32 c, u32 m, u32 s, u32 v) { tisp_reg_write_t33(c, m, s, v); }
static void op_bypass(u32 c) { tisp_top_ldc_bypass_refresh(c); }
static void op_err_block(void) { printk((const char *)0); }

static const struct t33_ldca_ops ops = {
	op_kmalloc, op_memclr, op_rcfg_register, op_err_alloc, op_err_register, op_par_reset,
	op_reg_write, op_reg_write33, op_bypass, op_err_block,
};

static void env_fill(struct t33_ldca_env *e)
{
	e->ops = &ops;
	e->attr = bo_stLDCAttr;
	e->mem_info = bo_stMemInfo;
	e->dma_cfg = bo_st_dma_cfg;
	e->par = bo_stLdcPar;
	e->ldc_info = bo_LdcInfo;
	e->refresh = bo_tispLdcRefresh;
	e->cfg_mode = bo_ldcCfgMode;
	e->init_par_flag = bo_stIspInitPar[226];
	e->max_w = bo_maxWidth;
	e->max_h = bo_maxHeight;
	e->max_ow = bo_maxOutWidth;
	e->max_oh = bo_maxOutHeight;
}

int bo_tisp_ldc_t33_init(u32 ch, u8 *info)
{
	struct t33_ldca_env e;

	env_fill(&e);
	return t33_ldca_init(&e, ch, info);
}

int bo_tisp_ldc_cfg_dma(u32 ch)
{
	struct t33_ldca_env e;

	env_fill(&e);
	return t33_ldca_cfg_dma(&e, ch);
}

int bo_tisp_ldc_cfg_load(u32 ch)
{
	struct t33_ldca_env e;

	env_fill(&e);
	return t33_ldca_cfg_load(&e, ch);
}

void bo_tisp_func_ldc_t33_dma_cfg(u32 ch, u32 mode, u32 w, u32 h, u32 ow, u32 oh, u32 a, u32 b)
{
	struct t33_ldca_env e;

	env_fill(&e);
	t33_ldca_dma_cfg(&e, ch, mode, w, h, ow, oh, a, b);
}
