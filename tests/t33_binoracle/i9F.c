/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "i9F": tisp_awb_init and tisp_defog_init with the vendor calling convention.
 * Callees are extern with the vendor names (CALLS lines in i9F.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9F.h"

extern void *private_kmalloc(u32 size, u32 flags);
extern void *private_vmalloc(u32 size);
extern int isp_printf(u32 level);
extern int tisp_rcfg_module_register(u32 ch, u32 mod, u32 fn);
extern int tisp_awb_malloc_info(u32 ch);
extern int tisp_awb_params_refresh(u32 ch, u32 par);
extern int tisp_awb_show_para_update(u32 ch);
extern int tisp_event_set_cb(u32 ch, u32 ev, u32 fn);
extern int tisp_awb_set_riscv_param(u32 ch);
extern int tisp_awb_set_hardware_param(u32 ch);
extern int tisp_awb_set_gain(u32 ch);
extern int system_irq_func_set(u32 ch, u32 irq, u32 fn);
extern int tisp_reg_write_t33(u32 ch, u32 n, u32 reg, u32 val);
extern int defog_count_weight35abc(u32 ch, u32 h, u32 w, u32 b);
extern int tisp_defog_write_reg_para1(u32 ch);
extern int tisp_defog_interp_trans_k(u32 ch, u32 a, u32 b, u32 c);
extern int tisp_defog_update_reg_para2(u32 ch);
extern int tisp_defog_write_reg_para2(u32 ch);
extern int tisp_defog_write_reg_para3(u32 ch);

u8 *o_awbInfo[4];
u8 *o_pst_awb_ct_detect[4];
u8 *o_tparamsP[4];
u8 o_riscvEnable[4];
u8 o_tispAwbgRefresh;
u8 o_tispAwbRefresh;
u8 o_awb_ls_wgt_lut_inter[1028];
u32 o_defog_weight_cal_max_size[4];
u8 *o_defog_weight[4];
u8 *o_defog_info[4];
u8 o_defogCfgMode;
u8 o_tispDefogRefresh;

static void *w_kmalloc(u32 size, u32 flags) { return private_kmalloc(size, flags); }
static void *w_vmalloc(u32 size) { return private_vmalloc(size); }
static __attribute__((noinline, noclone)) void w_fill(void *p, u32 c, u32 n) { memset(p, (int)c, n); }
static __attribute__((noinline, noclone)) void w_copy(void *d, const void *s, u32 n) { memcpy(d, s, n); }

static __attribute__((noinline)) void w_hook(u32 id, u32 a, u32 b, u32 c, u32 d)
{
	switch (id) {
	case T33_I9_RCFG: tisp_rcfg_module_register(a, b, 0); break;
	case T33_I9_ERR: isp_printf(2); break;
	case T33_I9_AWB_MALLOC_INFO: tisp_awb_malloc_info(a); break;
	case T33_I9_AWB_PARAMS: tisp_awb_params_refresh(a, b); break;
	case T33_I9_AWB_SHOW: tisp_awb_show_para_update(a); break;
	case T33_I9_EVENT: tisp_event_set_cb(a, b, 0); break;
	case T33_I9_RISCV: tisp_awb_set_riscv_param(a); break;
	case T33_I9_HW: tisp_awb_set_hardware_param(a); break;
	case T33_I9_GAIN: tisp_awb_set_gain(a); break;
	case T33_I9_IRQ: system_irq_func_set(a, b, 0); break;
	case T33_I9_REGW: tisp_reg_write_t33(a, b, c, d); break;
	case T33_I9_CNT_WEIGHT: defog_count_weight35abc(a, b, c, d); break;
	case T33_I9_PARA1: tisp_defog_write_reg_para1(a); break;
	case T33_I9_INTERP_K: tisp_defog_interp_trans_k(a, b, c, d); break;
	case T33_I9_PARA2_UPD: tisp_defog_update_reg_para2(a); break;
	case T33_I9_PARA2_WR: tisp_defog_write_reg_para2(a); break;
	default: tisp_defog_write_reg_para3(a); break;
	}
}

static const struct t33_i9_ops w_ops = { w_kmalloc, w_vmalloc, w_fill, w_copy, w_hook };

int bo_tisp_awb_init(u32 ch, u8 *sz)
{
	return t33_awb_init_ctx(ch, sz, o_awbInfo, o_pst_awb_ct_detect, o_tparamsP, o_awb_ls_wgt_lut_inter,
				o_riscvEnable, &o_tispAwbgRefresh, &o_tispAwbRefresh, &w_ops);
}

int bo_tisp_defog_init(u32 ch, u8 *sz)
{
	return t33_defog_init_ctx(ch, sz, o_defog_weight_cal_max_size, o_defog_weight, o_defog_info, o_tparamsP,
				  &o_defogCfgMode, &o_tispDefogRefresh, &w_ops);
}
