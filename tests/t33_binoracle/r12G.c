/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r12G": VIC / stream-on helpers (driver/t32/tx_isp_t33_r12G.h) with the vendor
 * calling convention, run against tx-isp-t33.ko by tools/ko_emu/t33_binoracle.py (spec: r12G.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_blk.h"

extern u32 system_reg_read(u32 reg);
extern u32 system_reg_write(u32 reg, u32 val);
extern int printk(const char *fmt, ...);
extern int isp_printf(u32 level, const char *fmt, ...);
extern void tisp_rcfg_t33_write_all_reg(u32 a);
extern void __private_spin_lock_irqsave(void *lock, u32 *flags);
extern void private_spin_unlock_irqrestore(void *lock, u32 flags);

#include "tx_isp_t33_r12G.h"

u8 t33_ispInitStatic[2];
u8 *dump_vsd;

void bo_lib_tisp_stream_on(void) { t33_lib_stream_on(t33_ispInitStatic[0]); }
u32 bo_lib_tisp_vic_ctrl(u32 ch, u32 val) { return t33_lib_vic_ctrl(ch, val); }
u32 bo_lib_tisp_vic_intp_enable(u32 ch) { return t33_lib_vic_intp_enable(ch); }
u32 bo_lib_tisp_vic_intp_disable(u32 ch) { return t33_lib_vic_intp_disable(ch); }
u32 bo_lib_tisp_vic_stop(void) { return t33_lib_vic_stop(); }
u32 bo_lib_tisp_vic_stream_on(void) { return t33_lib_vic_stream_on(); }
u32 bo_lib_tisp_vic_reset(void) { return t33_lib_vic_reset(); }
int bo_tx_isp_vic_start(u8 *vic, u32 force) { return t33_tx_vic_start(vic, force); }
void bo_tx_vic_enable_irq(u32 n, u32 cb) { (void)cb; t33_tx_vic_enable_irq(dump_vsd, n); }
void bo_tx_vic_disable_irq(u32 n, u32 cb) { (void)cb; t33_tx_vic_disable_irq(dump_vsd, n); }
