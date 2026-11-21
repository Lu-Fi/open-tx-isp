/* SPDX-License-Identifier: GPL-2.0 */
/*
 * binary oracle, block "t33-lscmf": tisp_lsc_mirror_flip of the vendor tx-isp-t33.ko (T33_OEM_KO) vs
 * t33_lsc_mirror_flip / t33_lsc_do_mirror_flip of driver/t32/tx_isp_t33_lscmf.h and the message of
 * tx_isp_t32_hw.c t33_lsc_hvflip (openimp #727): lsc_para[ch] = { section (table count at 0x94), parameters
 * (CT u16 at + 8, + 0xc), mesh info (lines / columns at + 10 / + 11), counters (+ 8 / + 9) }, stLutInfo[ch] + 0x14 =
 * the CT tables; the vendor's do_mirror_flip runs unchanged on its side (its line buffer from the vmalloc HLE),
 * tisp_lsc_interp_by_ct / func_lsc_para_update / tisp_lsc_write_reg / isp_printf are recorded calls.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_lscmf.h"

u32 *lsc_para[4];
u8 *stLutInfo[4];
extern int tisp_lsc_interp_by_ct(u32 ch, u32 ct, u32 force);
extern int func_lsc_para_update(u32 ch);
extern int tisp_lsc_write_reg(u32 ch, u32 mode);
extern int isp_printf(int level, const char *fmt, ...);

static const struct t33_lscmf_ops ops = { tisp_lsc_interp_by_ct, func_lsc_para_update, tisp_lsc_write_reg };

void bo_tisp_lsc_mirror_flip(u32 ch, u32 a1, u32 a2, u32 flip, u32 mirror)
{
	u8 *tab = (u8 *)(uintptr_t)t33_lscmf_ld32(stLutInfo[ch] + 0x14);
	const u8 *cnt;
	u32 df, dm;
	u8 mf = 0;			/* fresh tables: the #728 delta equals the vendor's 0 / 1 arguments */

	(void)a1; (void)a2;
	t33_lsc_mf_delta(&mf, flip, mirror, &df, &dm);
	t33_lsc_mirror_flip(ch, lsc_para[ch], tab, df, dm, &ops);
	cnt = (const u8 *)(uintptr_t)lsc_para[ch][3];
	isp_printf(0, "\n  [func is %s, line is %d] lsc filp times is %d, mirror times is %d\n", "tisp_lsc_mirror_flip",
		   0x5c0, cnt[8], cnt[9]);
}
