/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "awbQ": tisp_awb_process / params_refresh / dn_params_refresh /
 * set_hardware_param (driver/t32/tx_isp_t33_awbQ.h) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_awbQ.h"

extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 slot, u32 val);
extern int tisp_awb_set_regional_threshold(u32 ch);
extern int tisp_awb_set_lum_th_freq(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 mod);
extern int tisp_awb_long_par_update(u32 ch, u8 *tbl);
extern int tisp_awb_spec_calculate(u32 ch);
extern int tisp_t33_awb_long_alogrithm(u8 *tbl, u32 ch);
extern int tisp_awb_set_gain(u32 ch);
extern int private_complete(void *c);
extern int tisp_event_push(u32 ch, u32 *ev);

u32 *bo_awbInfo[4];
u8 *bo_tparamsP[4];
u8 *bo_pst_awb_ct_detect[4];
u32 bo_tawb_custom_en;
u16 bo_awb_count;
u8 bo_tispAwbRefresh;
u8 bo_awb_algo_comp[48];

static void q_reg(u32 ch, u32 m, u32 s, u32 v) { tisp_reg_write_t33(ch, m, s, v); }
static void q_reg_thr(u32 ch) { tisp_awb_set_regional_threshold(ch); }
static void q_lum(u32 ch) { tisp_awb_set_lum_th_freq(ch); }
static u32 q_dma(u32 m) { return (u32)tisp_rcfg_get_dma_state(m); }
static void q_lpu(u32 ch, u8 *t) { tisp_awb_long_par_update(ch, t); }
static void q_spec(u32 ch) { tisp_awb_spec_calculate(ch); }
static void q_long(u8 *t, u32 ch) { tisp_t33_awb_long_alogrithm(t, ch); }
static void q_gain(u32 ch) { tisp_awb_set_gain(ch); }
static void q_compl(void *c) { private_complete(c); }
static void q_ev(u32 ch, u32 *e) { tisp_event_push(ch, e); }

static const struct t33_awbQ_ops q_ops = {
	q_reg, q_reg_thr, q_lum, q_dma, q_lpu, q_spec, q_long, q_gain, q_compl, q_ev,
};

static u8 *q_ptr(u32 v) { return (u8 *)(unsigned long)v; }

int bo_tisp_awb_process(u32 ch)
{
	u32 *info = bo_awbInfo[ch];

	t33_awbQ_process(&q_ops, ch, bo_pst_awb_ct_detect[ch], bo_tawb_custom_en, bo_awb_algo_comp,
			 q_ptr(info[0]), &bo_awb_count);
	return 0;
}

int bo_tisp_awb_params_refresh(u32 ch)
{
	u32 *info = bo_awbInfo[ch];

	t33_awbQ_params_refresh(q_ptr(info[0]), q_ptr(info[1]));
	return 0;
}

int bo_tisp_awb_dn_params_refresh(u32 ch)
{
	u32 *info = bo_awbInfo[ch];
	u8 *par = bo_tparamsP[ch] + 3400;

	info[0] = (u32)(unsigned long)par;
	t33_awbQ_dn_refresh(&q_ops, ch, q_ptr(info[1]), par, q_ptr(info[2]), &bo_tispAwbRefresh);
	return 0;
}

int bo_tisp_awb_set_hardware_param(u32 ch)
{
	u32 *info = bo_awbInfo[ch];

	t33_awbQ_set_hw(&q_ops, ch, q_ptr(info[2]), &bo_tispAwbRefresh);
	return 0;
}
