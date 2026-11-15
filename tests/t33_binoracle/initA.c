/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "initA": tisp_csc_init / tisp_csc_deinit / tisp_clm_init
 * (driver/t32/tx_isp_t33_initA.h) with the vendor calling convention, built for mipsel
 * (tests/t33_binoracle/run.sh) and run against the vendor tx-isp-t33.ko instructions by
 * tools/ko_emu/t33_binoracle.py (spec: initA.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_initA.h"

/* module globals the vendor functions read/write (GLOBAL lines) */
struct t33_cscp *csc_para[4];
u8 *clm_info[4];
u8 tispCscRefresh;
const u8 *tparamsP[4];
u8 csc_cp[40];
u8 csc_ap[92];

/* kernel imports / module functions ported elsewhere: recorded as CALLS, never run */
extern void *private_vmalloc(u32 n);
extern void private_vfree(void *p);
extern int printk(const char *fmt, ...);
extern int isp_printf(int lvl, const char *fmt, ...);
extern int system_irq_func_set(u32 ch, u32 irq, void *fn);
extern int tisp_rcfg_module_register(u32 ch, u32 mod, const void *tmpl);
extern int tisp_rcfg_module_release(u32 ch, u32 mod);
extern int tisp_t33_csc_write_reg(u32 ch);
extern int tisp_csc_para_refresh_by_mode(u32 mode, u32 refresh, u8 *blk);
extern int tisp_csc_update_para(u32 ch);
extern int tisp_csc_get(u32 ch, u8 *out);
extern int tisp_clm_ct_interp(u32 ch, u32 ct, u32 force);
extern int tisp_clm_calc_para_refresh(u32 ch);
extern int tisp_clm_t33_write_reg(u32 ch);

static void *o_vmalloc(u32 n) { return private_vmalloc(n); }
static void o_vfree(void *p) { private_vfree(p); }
static void *o_memcpy(void *d, const void *s, u32 n) { return memcpy(d, s, n); }
static void *o_memset(void *d, int c, u32 n) { return memset(d, c, n); }
static int o_printk(const char *fmt) { return printk(fmt); }
static int o_isp_printf(int lvl, const char *fmt) { return isp_printf(lvl, fmt); }
static int o_irq_set(u32 ch, u32 irq, void *fn) { return system_irq_func_set(ch, irq, fn); }
static int o_rcfg_register(u32 ch, u32 mod, const void *tmpl) { return tisp_rcfg_module_register(ch, mod, tmpl); }
static int o_rcfg_release(u32 ch, u32 mod) { return tisp_rcfg_module_release(ch, mod); }
static int o_csc_write_reg(u32 ch) { return tisp_t33_csc_write_reg(ch); }
static int o_csc_refresh_by_mode(u32 mode, u32 refresh, u8 *blk) { return tisp_csc_para_refresh_by_mode(mode, refresh, blk); }
static int o_csc_update_para(u32 ch) { return tisp_csc_update_para(ch); }
static int o_csc_get(u32 ch, u8 *out) { return tisp_csc_get(ch, out); }
static int o_clm_ct_interp(u32 ch, u32 ct, u32 force) { return tisp_clm_ct_interp(ch, ct, force); }
static int o_clm_calc_refresh(u32 ch) { return tisp_clm_calc_para_refresh(ch); }
static int o_clm_write_reg(u32 ch) { return tisp_clm_t33_write_reg(ch); }

static const struct t33_initA_ops g_ops = {
	.vmalloc = o_vmalloc,
	.vfree = o_vfree,
	.memcpy = o_memcpy,
	.memset = o_memset,
	.printk = o_printk,
	.isp_printf = o_isp_printf,
	.irq_set = o_irq_set,
	.rcfg_register = o_rcfg_register,
	.rcfg_release = o_rcfg_release,
	.csc_write_reg = o_csc_write_reg,
	.csc_refresh_by_mode = o_csc_refresh_by_mode,
	.csc_update_para = o_csc_update_para,
	.csc_get = o_csc_get,
	.clm_ct_interp = o_clm_ct_interp,
	.clm_calc_refresh = o_clm_calc_refresh,
	.clm_write_reg = o_clm_write_reg,
};

int bo_tisp_csc_init(u32 a0) { return t33_csc_init(a0, csc_para, tparamsP, csc_cp, csc_ap, &tispCscRefresh, &g_ops); }
int bo_tisp_csc_deinit(u32 a0) { return t33_csc_deinit(a0, csc_para, &g_ops); }
int bo_tisp_clm_init(u32 a0, const u8 *par) { return t33_clm_initA(a0, par, clm_info, tparamsP, &g_ops); }
