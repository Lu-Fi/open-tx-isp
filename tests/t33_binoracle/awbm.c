/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "awbm": tisp_awb_malloc_info with the vendor calling convention (split from aemisc). */
#include <stdint.h>
#include <string.h>
typedef unsigned long long u64;
#include "tx_isp_t33_aemisc.h"

extern void *private_kmalloc(u32 size, u32 flags);
extern void private_kfree(void *p);
extern int isp_printf(int level, const char *fmt, ...);
extern int tisp_reg_write_t33(u32 ch, u32 mod, u32 slot, u32 val);

u32 stAwbStatBuff[4];
u32 awbInfo[4];

static void *am_kmalloc(u32 size, u32 flags) { return private_kmalloc(size, flags); }
static void am_kfree(void *p) { private_kfree(p); }
static void am_reg(u32 ch, u32 mod, u32 slot, u32 val) { tisp_reg_write_t33(ch, mod, slot, val); }
static void am_err(int level, int line) { isp_printf(level, "aemisc %s %d\n", "aemisc", line); }

static const struct t33_aemisc_ops am_ops = { am_kmalloc, am_kfree, am_reg, am_err };

void bo_tisp_awb_malloc_info(u32 ch)
{
	u32 c = ch & 0xff;

	t33_awb_malloc_info(ch, &stAwbStatBuff[c], &awbInfo[c], &am_ops);
}
