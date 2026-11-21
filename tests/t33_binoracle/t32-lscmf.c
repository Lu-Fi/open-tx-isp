/* SPDX-License-Identifier: GPL-2.0 */
/*
 * binary oracle, block "t32-lscmf": tisp_lsc_mirror_flip of the vendor tx-isp-t32.ko (T32_OEM_KO) vs the
 * exchange t32_lsc_mesh_flip of driver/t32/tx_isp_t32_lsc.h and the flow of tx_isp_t32_lscct.c t32_lsc_drv_hvflip
 * (openimp #722): lsc_para[ch] = { section, cur, info (grid rows / columns at + 0x11 / + 0x12), counters, lock },
 * stLutInfo[ch] + 0x14 = the mesh tables; the vendor's lsc_exchange_line / _data run unchanged on its side, the
 * spin lock is a NOP, tisp_lsc_interp_by_ct / tisp_lsc_write_all_reg / printk are recorded calls.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_lsc.h"

u8 *lsc_para[4];
u8 *stLutInfo[4];
extern int tisp_lsc_interp_by_ct(u32 ch, u32 ct, u32 force);
extern int tisp_lsc_write_all_reg(u32 ch, u32 mode, u32 sonly, u32 mesh);
extern int printk(const char *fmt, ...);

static u8 *bo_ptr(const u8 *p) { u32 v; memcpy(&v, p, 4); return (u8 *)(uintptr_t)v; }

void bo_tisp_lsc_mirror_flip(u32 ch, u32 a1, u32 a2, u32 flip, u32 mirror)
{
	u8 *b = lsc_para[ch], *sec = bo_ptr(b), *cur = bo_ptr(b + 4), *info = bo_ptr(b + 8), *st = bo_ptr(b + 12);
	u8 *tab = bo_ptr(stLutInfo[ch] + 0x14) + 2;
	u32 five = 5, ct, df, dm;
	u8 mf = 0;			/* fresh tables: the #728 delta equals the vendor's arguments */

	(void)a1; (void)a2;
	t32_lsc_mf_delta(&mf, flip, mirror, &df, &dm);
	if (df)
		t32_lsc_mesh_flip(tab, info[17], info[18], sec[116], 1, 0, st, &t32_lsc_xops_default);
	if (dm)
		t32_lsc_mesh_flip(tab, info[17], info[18], sec[116], 0, 1, st, &t32_lsc_xops_default);
	memcpy(cur + 12, &five, 4);
	memcpy(&ct, cur + 8, 4);
	tisp_lsc_interp_by_ct(ch, ct, 1);
	tisp_lsc_write_all_reg(ch, 2, 0, 1);
	printk("\n  [func is %s, line is %d] lsc filp times is %d, mirror times is %d\n", "tisp_lsc_mirror_flip", 0x7f9,
	       st[9], st[10]);
}
