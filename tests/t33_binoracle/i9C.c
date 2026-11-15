/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "i9C": tisp_gsm_init, tisp_dma_init, tisp_rcfg_init with the vendor calling convention. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9C.h"

extern void private_spin_lock_init(void *l);
extern void system_irq_func_set(u32 ch, u32 irq, void *h);
extern void tisp_event_set_cb(u32 ch, u32 ev, void *h);
extern void system_reg_write(u32 reg, u32 val);
extern void __init_waitqueue_head(void *wq, const char *name, void *key);
extern u32 private_kthread_run(void *fn, void *data, const char *name);
extern int printk(const char *fmt, ...);
extern int isp_printf(int level, const char *fmt, ...);
extern void *private_vmalloc(u32 n);
extern void *private_kmalloc(u32 n, u32 flags);

u32 t33_gsm_area_div[2];
u32 t33_gsm_area;
u8 t33_gsm_hist_para[3080];
struct t33_dma_cfg t33_st_dma_cfg[4];
u8 t33_g_sensor_num;
u32 t33_sp_wdma[3];
u32 t33_wdma_thread;
u32 t33_snap_compl[6];
u32 *t33_rcfg_buf_info;
u32 t33_rcfg_trig_cnt[4];
u32 t33_init_par[227];

static u8 t33_dummy;

static void h_lock(void) { private_spin_lock_init(&t33_dummy); }
static void h_irq(u32 ch, u32 irq, void *h) { system_irq_func_set(ch, irq, h); }
static void h_evcb(u32 ch, u32 ev, void *h) { tisp_event_set_cb(ch, ev, h); }
static void h_wr(u32 reg, u32 val) { system_reg_write(reg, val); }
static u32 h_kthread(void) { return private_kthread_run((void *)h_lock, 0, "t33"); }
static void h_wq(void) { __init_waitqueue_head(&t33_snap_compl[1], "t33", &t33_dummy); }
static void h_err(void) { printk("t33"); }
static void h_log(int level) { isp_printf(level, "t33"); }
static void *h_vmalloc(u32 n) { return private_vmalloc(n); }
static void *h_kmalloc(u32 n, u32 f) { return private_kmalloc(n, f); }

int bo_tisp_gsm_init(u32 ch, u8 *attr)
{
	struct t33_gsm_ops o = { h_lock, h_irq, h_evcb, h_wr, (void *)h_irq, (void *)h_wr, (void *)h_evcb };

	return t33_gsm_init(&o, ch, attr, t33_gsm_area_div, &t33_gsm_area, t33_gsm_hist_para);
}

u32 bo_tisp_dma_init(u32 ch, u8 *attr)
{
	struct t33_dma_ops o = { h_irq, h_kthread, h_wq, h_err, (void *)h_irq, (void *)h_wr, (void *)h_evcb };

	return t33_dma_init(&o, ch, attr, t33_st_dma_cfg, &t33_g_sensor_num, t33_sp_wdma, &t33_snap_compl[0],
			    &t33_wdma_thread);
}

void bo_tisp_rcfg_init(u32 ch)
{
	struct t33_rcfg_ops o = { h_vmalloc, h_kmalloc, h_log, h_wr };

	t33_rcfg_init(&o, ch, &t33_rcfg_buf_info, t33_init_par[226], t33_rcfg_trig_cnt);
}
