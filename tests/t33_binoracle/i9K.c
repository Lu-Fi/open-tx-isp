/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "i9K": denoise block deinit functions */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9K.h"

extern void private_kfree(void *p);
extern int tisp_rcfg_module_release(u32 ch, u32 id);
extern int printk(const char *fmt, ...);

u32 ydnsInfo[4];
u32 sdnsInfo[4];
u32 cdnsInfo[4];
u32 rdnsInfo[4];

static int i9k_err(u32 id, int r)
{
	return printk("deinit %u %d\n", id, r);
}

int bo_tisp_ydns_deinit(u32 ch) { return t33_ydns_deinit_ch(ydnsInfo, ch, private_kfree, tisp_rcfg_module_release, i9k_err); }
int bo_tisp_sdns_deinit(u32 ch) { return t33_sdns_deinit_ch(sdnsInfo, ch, private_kfree, tisp_rcfg_module_release, i9k_err); }
int bo_tisp_cdns_deinit(u32 ch) { return t33_cdns_deinit_ch(cdnsInfo, ch, private_kfree, tisp_rcfg_module_release, i9k_err); }
int bo_tisp_rdns_deinit(u32 ch) { return t33_rdns_deinit_ch(rdnsInfo, ch, private_kfree, tisp_rcfg_module_release, i9k_err); }
