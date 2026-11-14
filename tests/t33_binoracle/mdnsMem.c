/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mdnsMem": tisp_mdns_t33_set_mem_cfg with the vendor calling convention.
 * Register writes / trigger / printk are extern callees recorded by the oracle.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mdnsMem.h"

extern void tisp_reg_write_t33(u32 chn, u32 blk, u32 reg, u32 val);
extern void system_reg_write(u32 reg, u32 val);
extern void tisp_rcfg_module_cfg_trig(u32 chn, u32 blk);
extern int printk(const char *fmt, ...);

u8 mdnsCfgMode;
u8 riscvEnable[4] __attribute__((aligned(4)));
u32 *mdnsInfo[4];
u8 stIspInitPar[4032] __attribute__((aligned(4)));
u8 stMemInfo[304] __attribute__((aligned(4)));
u8 stRiscvPara[51200] __attribute__((aligned(4)));

static void bo_log(int line)
{
	printk("mdns mem cfg: riscv para mismatch %d\n", line);
}

void bo_tisp_mdns_t33_set_mem_cfg(u32 chn, u32 pa)
{
	struct t33_mdns_mem_ops ops;
	struct t33_mdns_mem_env env;

	ops.reg_write = tisp_reg_write_t33;
	ops.sys_write = system_reg_write;
	ops.trigger = tisp_rcfg_module_cfg_trig;
	ops.log = bo_log;
	env.init_par = stIspInitPar;
	env.mem_info = stMemInfo;
	env.mdns_info = (const u32 *const *)mdnsInfo;
	env.cfg_mode = &mdnsCfgMode;
	env.riscv_en = riscvEnable;
	env.riscv_par = stRiscvPara;
	t33_mdns_set_mem_cfg((u8)chn, pa, &env, &ops);
}
