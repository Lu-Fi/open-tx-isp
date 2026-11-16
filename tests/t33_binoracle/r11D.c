/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r11D": AF accessors / malloc_cfg, LCE malloc_cfg and gauss_lpf of
 * driver/t32/tx_isp_t33_r11D.h with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r11D.h"

extern void *private_vmalloc(u32 n);
extern void *private_kmalloc(u32 n, u32 flags);
extern int isp_printf(int level, const char *fmt, ...);
extern int printk(const char *fmt, ...);
extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);

u8 *af_info[4];
u32 *af_buf_info[4];
u8 af_par_attr[96];
u8 afCfgMode;
u32 lceBuffInfo[48];
u8 lceCfgMode;
u8 lsfCfgMode;

static void *op_vmalloc(u32 n) { return private_vmalloc(n); }
static void *op_kmalloc(u32 n, u32 f) { return private_kmalloc(n, f); }
static void op_oom(void) { isp_printf(2, "oom"); }
static void op_unaligned(void) { printk("unaligned %s %d\n", "tisp_af_malloc_cfg", 149); }
static int op_reg(u32 ch, u32 blk, u32 reg, u32 val) { return tisp_reg_write_t33(ch, blk, reg, val); }

static const struct t33_mem_ops mem_ops = { op_vmalloc, op_kmalloc, op_oom, op_unaligned, op_reg };

int bo_tisp_af_api_get_metric(u32 ch, u32 *m1, u32 *m2) { return t33_af_get_metric(af_info, af_par_attr, ch, m1, m2); }
void bo_tisp_af_api_get_statis_info(u32 ch, u8 *out) { t33_af_get_statis(af_buf_info, ch, out); }
int bo_tisp_g_af_metric_attr(u32 ch, u8 *buf) { return t33_g_af_metric_attr(af_info, af_par_attr, ch, buf); }
int bo_tisp_g_af_statis_attr(u32 ch, u8 *buf) { return t33_g_af_statis_attr(af_buf_info, ch, buf); }
int bo_tisp_af_malloc_cfg(u32 ch) { return t33_af_malloc_cfg(af_buf_info, ch, afCfgMode, &mem_ops); }
int bo_tisp_lce_malloc_cfg(u32 ch) { return t33_lce_malloc_cfg(lceBuffInfo, ch, lceCfgMode, lsfCfgMode, &mem_ops); }
void bo_tisp_lce_gauss_lpf(u8 *sigma, u32 *out) { t33_lce_gauss_lpf(sigma, out); }
