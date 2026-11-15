/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "tmoP": tmo size update / statistics / interrupt / mode change refresh
 * with the vendor calling convention (spec: tmoP.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_tmoP.h"

extern u32 system_reg_read(u32 a);
extern void system_reg_write(u32 a, u32 v);
extern void tisp_tmo_tmo_soft_process(u32 ch);
extern void private_dma_cache_sync(u32 a0, void *a1, u32 a2, u32 a3);

u32 tmo_info_ptrs[4];		/* tmoInfo */
u32 iadam_counter;
u8 static_tmo_curve_x[402];

static u32 op_rd(u32 a) { return system_reg_read(a); }
static void op_wr(u32 a, u32 v) { system_reg_write(a, v); }
static void op_soft(u32 ch) { tisp_tmo_tmo_soft_process(ch); }
static void op_sync(u32 a0, void *p, u32 n, u32 d) { private_dma_cache_sync(a0, p, n, d); }
static void op_copy(void *d, const void *s, u32 n) { memcpy(d, s, n); }

static const struct t33_tmop_ops ops = { op_rd, op_wr, op_soft, op_sync, op_copy };

static u32 *info_of(u32 ch) { return (u32 *)(unsigned long)tmo_info_ptrs[ch]; }

void bo_tisp_tmo_tmo_size_update(u32 ch, u8 *par)
{
	u32 *v = info_of(ch);

	t33_tmop_size_update(v, (u8 *)(unsigned long)v[2732 / 4], par);
}

void bo_tisp_tmo_tmoadr_get_statistics(u32 ch) { t33_tmop_get_stat(info_of(ch), &ops); }

int bo_tisp_tmo_interrupt_static(u32 ch) { return t33_tmop_irq_static(info_of(ch), &ops, &iadam_counter, ch); }

void bo_tisp_tmo_modechange_stat_refresh(u32 ch) { t33_tmop_mode_refresh(info_of(ch), static_tmo_curve_x, &ops); }
