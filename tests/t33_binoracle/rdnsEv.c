/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "rdnsEv": tisp_rdns_ev_update with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_rdnsEv.h"

extern void func_rdns_bin_para_update(u32 ch);
extern void func_rdns_para_update(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 id);
extern int tisp_reg_write_t33(u32 ch, u32 id, u32 sel, u32 val);

u32 *bo_rdnsInfo[4] = { (u32 *)1, (u32 *)1, (u32 *)1, (u32 *)1 };
u8 bo_rdnsCfgMode = 1;
u8 bo_tispRdnsRefresh = 1;

void bo_tisp_rdns_ev_update(u32 ch, unsigned long long ev64)
{
	u32 *v = bo_rdnsInfo[ch];
	u32 c = ch & 0xff;
	struct t33_rdns_ev_wr w[T33_RDNS_EV_NWR];
	unsigned int n, k;
	int busy;

	v[T33_RDNS_EV_WORD] = t33_rdns_ev_value(ev64);
	func_rdns_bin_para_update(c);
	t33_rdns_ev_interp(v, (const u8 *)(unsigned long)v[T33_RDNS_SEC_WORD]);
	func_rdns_para_update(c);
	busy = tisp_rcfg_get_dma_state(3);
	if (!busy) {
		n = t33_rdns_ev_writes(v, bo_rdnsCfgMode, w);
		for (k = 0; k < n; k++)
			tisp_reg_write_t33(c, 3, w[k].sel, w[k].val);
	}
	bo_tispRdnsRefresh = t33_rdns_ev_flag(busy);
}
