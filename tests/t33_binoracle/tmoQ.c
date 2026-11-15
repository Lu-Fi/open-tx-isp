/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "tmoQ": tisp_tmo_ev_update, tisp_tmo_deinit and
 * tisp_tmo_tmo_soft_process (driver/t32/tx_isp_t33_tmoQ.h) with the vendor
 * calling convention, built for mipsel and run against the vendor
 * tx-isp-t33.ko instructions (spec: tmoQ.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_tmoQ.h"

/* module data objects (GLOBAL lines) */
u8 *tmoInfo[4];
u8 tmoCfgMode;
u8 mulsensornum;
u8 tispTmoRefresh[1];

/* callees: neither side runs them, calls are recorded and compared */
extern int tisp_reg_write_t33(u32 ch, u32 a1, u32 reg, u32 val);
extern void system_reg_write(u32 addr, u32 val);
extern void tisp_tmo_tmo_lumcurve_ev_interp(u32 ch);
extern void tisp_tmo_tmo_satcurve_ev_interp(u32 ch);
extern void tisp_tmo_ramY_vi0_reg_refresh(u32 ch);
extern void func_tmo_ic_para_update(u32 ch);
extern void private_kfree(void *p);
extern int tisp_rcfg_module_release(u32 ch, u32 n);
extern int printk(const char *fmt, ...);

static void tmoq_release_err(int r)
{
	printk("%s[%d]: release failed %d\n", "tisp_tmo_deinit", 1924, r);
}

static const struct t33_tmoq_ops tmoq_ops = {
	.reg_write = tisp_reg_write_t33,
	.sys_write = system_reg_write,
	.lumcurve = tisp_tmo_tmo_lumcurve_ev_interp,
	.satcurve = tisp_tmo_tmo_satcurve_ev_interp,
	.ramy = tisp_tmo_ramY_vi0_reg_refresh,
	.ic_para = func_tmo_ic_para_update,
	.kfree = private_kfree,
	.module_release = tisp_rcfg_module_release,
	.release_err = tmoq_release_err,
};

void bo_tisp_tmo_ev_update(u32 ch, unsigned long long ev, u32 gain)
{
	t33_tmo_ev_update(tmoInfo, tmoCfgMode, tispTmoRefresh, ch, ev, gain, &tmoq_ops);
}

void bo_tisp_tmo_deinit(u32 ch)
{
	t33_tmo_deinit(tmoInfo, &mulsensornum, &tmoq_ops, ch);
}

void bo_tisp_tmo_tmo_soft_process(u32 ch)
{
	t33_tmo_soft_process(tmoInfo, tmoCfgMode, tispTmoRefresh, &tmoq_ops, ch);
}
