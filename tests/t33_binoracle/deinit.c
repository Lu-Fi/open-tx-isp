/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "deinit": the T33 teardown helpers and tisp_all_reset
 * (driver/t32/tx_isp_t33_deinit.h) with the vendor calling convention and callees (spec: deinit.conf).
 */
#include <stdint.h>
#include "tx_isp_t33_deinit.h"

u32 o_blc_para[4], o_ldc_attr[4], o_msca_attr[4], o_msca_info[4], o_ae_info[4], o_fliker_info[4], o_fliker_para[4];
u8 o_wdma_vinum;
u32 o_wdma_thread;

extern u32 system_reg_read(u32 reg);
extern void system_reg_write(u32 reg, u32 val);
extern void private_vfree(u32 p);
extern void private_kfree(u32 p);
extern int tisp_rcfg_module_release(u32 ch, u32 id);
extern void private_kthread_stop(u32 task);
extern int isp_printf(int level, const char *fmt, ...);
extern int printk(const char *fmt, ...);

static u32 a_log(u32 level, u32 line)
{
	if (line == 289)
		return isp_printf(level, "[ISP][%s, %d]: isp stop timeout !!!\n", "tisp_all_reset", line);
	if (line == 301)
		return isp_printf(level, "[ISP][%s, %d]: isp all reset timeout !!!\n", "tisp_all_reset", line);
	return isp_printf(level, "[ISP][%s, %d]: [ISP][%s, %d]: isp all reset !!!\n", "tisp_all_reset", line,
			  "tisp_all_reset", line);
}

static void a_err(u32 which, int ret)
{
	if (which)
		printk("[ISP][%s, %d]: MSCA rcfg release error !!! ret is 0x%08x \n", "tisp_msca_t33_deinit", 1677, ret);
	else
		printk("[ISP][%s, %d]: BLC rcfg release error !!! ret is 0x%08x \n", "tisp_blc_deinit", 478, ret);
}

static const struct t33_deinit bo_d = {
	.blc_para = o_blc_para, .ldc_attr = o_ldc_attr, .msca_attr = o_msca_attr, .msca_info = o_msca_info,
	.ae_info = o_ae_info, .fliker_info = o_fliker_info, .fliker_para = o_fliker_para,
	.wdma_vinum = &o_wdma_vinum, .wdma_thread = &o_wdma_thread,
	.rd = system_reg_read, .wr = system_reg_write, .vfree = private_vfree, .kfree = private_kfree,
	.release = tisp_rcfg_module_release, .kthread_stop = private_kthread_stop, .log = a_log, .err = a_err,
};

u32 bo_all_reset(void) { return t33_all_reset(&bo_d); }
void bo_blc(u32 ch) { t33_blc_deinit(&bo_d, ch); }
int bo_ldc(u32 ch) { return t33_ldc_deinit(&bo_d, ch); }
int bo_msca(u32 ch) { return t33_msca_deinit(&bo_d, ch); }
void bo_ae(u32 ch) { t33_normal_ae_deinit(&bo_d, ch); }
void bo_dma(void) { t33_dma_deinit(&bo_d); }
