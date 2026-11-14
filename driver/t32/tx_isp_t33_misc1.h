/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 misc block 1: ISP soft reset, IPC cache counters, AE day/night refresh.
 * The including file provides system_reg_read/write, isp_printf, __udelay,
 * tisp_ae_clac_deflicker_cfg and tisp_rcfg_module_cfg_trig.
 */
#ifndef TX_ISP_T33_MISC1_H
#define TX_ISP_T33_MISC1_H

#include "tx_isp_t33_blk.h"

/* lib_tisp_func_reset: soft reset, wait for the two status polls, then a 1 us delay */
static inline void t33_func_reset(void)
{
	u32 n;
	int ok = 0;

	system_reg_write(0x200, 0x80020001);
	system_reg_read(0x204);
	n = 20000;
	do {
		if (system_reg_read(0x204) == 1) {
			ok = 1;
			break;
		}
	} while (--n);
	if (!ok) {
		isp_printf(2, "reset: first poll timeout %s %d\n", __func__, 324);
		return;
	}
	system_reg_write(0x24, 0x40000004);
	n = 20000;
	ok = 0;
	do {
		if ((system_reg_read(0x24) & 0xf) == 0) {
			ok = 1;
			break;
		}
	} while (--n);
	if (!ok) {
		isp_printf(2, "reset: second poll timeout %s %d\n", __func__, 337);
		return;
	}
	isp_printf(1, "reset: done %s %d\n", __func__, 341);
	__udelay(1);
}

/* lib_tisp_get_ipc_all_cache_data: sum of the two cache counters */
static inline u32 t33_ipc_all_cache(void)
{
	u32 a, b;

	a = system_reg_read(4968);
	b = system_reg_read(4972);
	return a + b;
}

/* lib_tisp_get_ipc_cache_data_byindex: counter selected by the low byte of idx */
static inline u32 t33_ipc_cache_byindex(u32 idx)
{
	return system_reg_read((idx & 0xff) ? 4972 : 4968);
}

/* normal_ae_dn_par_refresh: flag the AE cache of channel ch and trigger module 30 */
static inline void t33_ae_dn_par_refresh(u8 *const *cache, u32 ch)
{
	tisp_ae_clac_deflicker_cfg(ch);
	cache[ch][1285] = 1;
	tisp_rcfg_module_cfg_trig(ch & 0xff, 30);
}

#endif /* TX_ISP_T33_MISC1_H */
