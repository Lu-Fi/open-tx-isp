/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "i9E": tisp_gamma_init / tisp_lsc_init / tisp_ae_init with the vendor
 * calling convention; the callees are the vendor names (extern, recorded by the oracle).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9E.h"

extern int tisp_rcfg_module_register(u32 ch, u32 id, const void *fn);
extern void *private_vmalloc(size_t n);
extern void *private_kmalloc(size_t n, u32 flags);
extern void private_kfree(const void *p);
extern void private_spin_lock_init(void *l);
extern int printk(const char *fmt, ...);
extern int isp_printf(u32 level, const char *fmt, ...);
extern int system_irq_func_set(u32 ch, u32 n, const void *fn);
extern int tisp_event_set_cb(u32 ch, u32 n, const void *fn);
extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);
extern int tisp_gamma_interp_by_ev(u32 ch, const void *ap, u32 lo, u32 hi, u32 force);
extern int tisp_gamma_update_curve(u32 ch, const void *ap, u32 lo, u32 hi);
extern int tisp_gamma_strength_transform(u32 ch);
extern int tisp_gamma_write_lut_rgb(u32 ch);
extern int tisp_lsc_lut_valid_check(u32 a, u32 b, u32 c, u32 d, u8 *p8, u8 *p9, u8 *p10, u8 *p11,
				    u8 *p0, u8 *p21, u8 *p4);
extern int tisp_lsc_malloc_info(u32 ch);
extern int tisp_lsc_interp_by_tgain(u32 ch, u32 gain, u32 force);
extern int tisp_lsc_interp_by_ct(u32 ch, u32 ct, u32 force);
extern int func_lsc_para_update(u32 ch);
extern int tisp_lsc_write_reg(u32 ch, u32 sel);
extern int tisp_ae_malloc_info(u32 ch, void *ctx);
extern u32 tisp_get_sensor_attr(u32 ch);
extern int tisp_normal_ae_init(u32 ch, void *hw, void *par);
extern void tisp_rcfg_gamma_reg_init(void);
extern void tisp_gamma_irq_frame_done(void);
extern void tisp_rcfg_lsc_reg_init(void);
extern void tisp_lsc_irq_frame_done(void);
extern void tisp_rcfg_ae_reg_init(void);
extern void tisp_rcfg_dg_reg_init(void);
extern void tisp_ae_interrupt_hist(void);
extern void tisp_ae_interrupt_static(void);
extern void tisp_ae_calc_process(void);

/* module statics (vendor names) */
u8 *tparamsP[4];
u8 *g_ae_contexts[4];
u8 *stLutInfo[4];
struct t33_gamma_blk *gamma_para[4];
struct t33_lsc_blk *lsc_para[4];
u8 lscCfgMode;
u8 tispLscRefresh;
u8 tispGammaRefresh;
u8 gamma_cp[260];
u8 gamma_ip[8];
u8 gamma_sp[260];
u8 gamma_ap[264];

/* through volatile pointers so that the compiler keeps real calls (recorded by the oracle) */
static void *w_copy(void *d, const void *s, size_t n)
{
	void *(*volatile f)(void *, const void *, size_t) = memcpy;

	return f(d, s, n);
}

static void *w_fill(void *d, int c, size_t n)
{
	void *(*volatile f)(void *, int, size_t) = memset;

	return f(d, c, n);
}

static void w_warn(u32 line, int r)
{
	printk("%s:%d %d", "fn", line, r);
}

static void w_msg(u32 level, u32 line)
{
	isp_printf(level, "", line);
}

static const struct t33_i9e_ops ops = {
	.module_register = tisp_rcfg_module_register,
	.alloc_v = private_vmalloc,
	.alloc_k = private_kmalloc,
	.release = private_kfree,
	.copy = w_copy,
	.fill = w_fill,
	.spin_init = private_spin_lock_init,
	.warn = w_warn,
	.msg = w_msg,
	.irq_set = system_irq_func_set,
	.event_cb = tisp_event_set_cb,
	.reg_write = tisp_reg_write_t33,
	.gamma_interp = tisp_gamma_interp_by_ev,
	.gamma_curve = tisp_gamma_update_curve,
	.gamma_strength = tisp_gamma_strength_transform,
	.gamma_lut = tisp_gamma_write_lut_rgb,
	.lsc_valid = tisp_lsc_lut_valid_check,
	.lsc_malloc = tisp_lsc_malloc_info,
	.lsc_tgain = tisp_lsc_interp_by_tgain,
	.lsc_ct = tisp_lsc_interp_by_ct,
	.lsc_para = func_lsc_para_update,
	.lsc_write = tisp_lsc_write_reg,
	.ae_malloc = tisp_ae_malloc_info,
	.sensor_attr = tisp_get_sensor_attr,
	.ae_normal = tisp_normal_ae_init,
	.gamma_reg_init = (const void *)tisp_rcfg_gamma_reg_init,
	.gamma_irq = (const void *)tisp_gamma_irq_frame_done,
	.lsc_reg_init = (const void *)tisp_rcfg_lsc_reg_init,
	.lsc_irq = (const void *)tisp_lsc_irq_frame_done,
	.ae_reg_init = (const void *)tisp_rcfg_ae_reg_init,
	.dg_reg_init = (const void *)tisp_rcfg_dg_reg_init,
	.ae_hist = (const void *)tisp_ae_interrupt_hist,
	.ae_static = (const void *)tisp_ae_interrupt_static,
	.ae_calc = (const void *)tisp_ae_calc_process,
};

int bo_tisp_gamma_init(u32 ch, u8 *info)
{
	return t33_gamma_module_init(&ops, ch, info, gamma_para, tparamsP, gamma_cp, gamma_ip, gamma_sp,
				     gamma_ap, &tispGammaRefresh);
}

int bo_tisp_lsc_init(u32 ch, u8 *info)
{
	return t33_lsc_module_init(&ops, ch, info, lsc_para, tparamsP, stLutInfo, &lscCfgMode,
				   &tispLscRefresh);
}

int bo_tisp_ae_init(u32 ch, u8 *info)
{
	return t33_ae_module_init(&ops, ch, info, g_ae_contexts, tparamsP);
}
