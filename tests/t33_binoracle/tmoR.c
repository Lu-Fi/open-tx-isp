/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "tmoR": tisp_tmo_init with the vendor calling convention.
 * Callees are extern with the vendor names (CALLS in tmoR.conf); the state buffers are ours.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_tmoR.h"

extern int tisp_rcfg_module_register(u32 ch, u32 id, void *fn);
extern int printk(const char *fmt, ...);
extern void isp_printf(u32 lvl, const char *fmt, ...);
extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);
extern void system_reg_write(u32 reg, u32 val);
extern void system_irq_func_set(u32 ch, u32 irq, void *fn);
extern u32 tisp_ev_intp_uint8_t(u32 x, const u8 *k, const u8 *t);
extern u32 tisp_ev_intp_int8_t(u32 x, const u8 *k, const u8 *t);
extern u32 tisp_ev_intp_uint16_t(u32 x, const u8 *k, const u8 *t);
extern u32 tisp_bv_intp_uint8_t(u32 x, const u8 *k, const u8 *t);
extern u32 tisp_bv_intp_int8_t(u32 x, const u8 *k, const u8 *t);
extern u32 tisp_bv_intp_uint16_t(u32 x, const u8 *k, const u8 *t);

#define CHFN(name, vendor) extern void vendor(u32 ch); static void name(u32 ch) { vendor(ch); }
CHFN(w_size, tisp_tmo_tmo_size_update)
CHFN(w_mode, tisp_tmo_modechange_stat_refresh)
CHFN(w_bin, func_bin_para_update)
CHFN(w_lum, tisp_tmo_tmo_lumcurve_ev_interp)
CHFN(w_sat, tisp_tmo_tmo_satcurve_ev_interp)
CHFN(w_ic, func_tmo_ic_para_update)
CHFN(w_ramy, tisp_tmo_ramY_vi0_reg_refresh)
CHFN(w_ddrv, tisp_tmo_s2s3_ddrV_init)
CHFN(w_v23c, tisp_tmo_ramV_vi23_reg_ctrl)
CHFN(w_v23r, tisp_tmo_ramV_vi23_reg_refresh)
CHFN(w_disv, tisp_tmo_rcfg_reg_num_disV)
CHFN(w_v0r, tisp_tmo_ramV_vi0_reg_refresh)

u8 *o_tmoInfo[4];
u8 *o_tparamsP[4];
u8 o_tmoCfgMode;
u8 o_tispTmoRefresh;
u8 o_stIspInitPar[4032];
u32 o_iadam_counter;
u16 o_tmo_reg_num;
extern void *private_kmalloc(u32 size, u32 flags);

static int w_modreg(u32 ch, u32 id) { return tisp_rcfg_module_register(ch, id, 0); }
static void w_warnreg(void) { printk("tmo: register failed\n"); }
static void *w_kmalloc(u32 size, u32 flags) { return private_kmalloc(size, flags); }
static void w_warnalloc(void) { isp_printf(2, "tmo: alloc failed\n"); }
static void w_regwr(u32 ch, u32 blk, u32 reg, u32 val) { tisp_reg_write_t33(ch, blk, reg, val); }
static void w_sysw(u32 reg, u32 val) { system_reg_write(reg, val); }
static void w_irq(u32 ch, u32 irq) { system_irq_func_set(ch, irq, 0); }

int bo_tisp_tmo_init(u32 ch, u8 *par)
{
	struct t33_tmoR_ops o;

	o.module_register = w_modreg;
	o.warn_register = w_warnreg;
	o.kmalloc = w_kmalloc;
	o.warn_alloc = w_warnalloc;
	o.reg_write = w_regwr;
	o.sys_write = w_sysw;
	o.size_update = w_size;
	o.modechange = w_mode;
	o.bin_update = w_bin;
	o.intp[0][0] = tisp_ev_intp_uint8_t;
	o.intp[0][1] = tisp_ev_intp_int8_t;
	o.intp[0][2] = tisp_ev_intp_uint16_t;
	o.intp[1][0] = tisp_bv_intp_uint8_t;
	o.intp[1][1] = tisp_bv_intp_int8_t;
	o.intp[1][2] = tisp_bv_intp_uint16_t;
	o.lumcurve = w_lum;
	o.satcurve = w_sat;
	o.ic_para = w_ic;
	o.ramy = w_ramy;
	o.ddrv_init = w_ddrv;
	o.vi23_ctrl = w_v23c;
	o.vi23_refresh = w_v23r;
	o.disv = w_disv;
	o.vi0_refresh = w_v0r;
	o.irq_set = w_irq;
	return t33_tmoR_init(&o, ch, par, o_tmoInfo, o_tparamsP, &o_tmoCfgMode, &o_tispTmoRefresh,
			     o_stIspInitPar, &o_iadam_counter, &o_tmo_reg_num);
}
