/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mdnsR": tisp_mdns_init with the vendor calling convention
 * (spec: mdnsR.conf).  Module statics are plain globals named like the vendor's.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mdnsR.h"

u32 mdnsInfo[4];
u32 tparamsP[4];
u8 stMemInfo[304];
u8 uvLimitFlag[4];
u8 mdnsCfgMode;
u8 riscvEnable[4];
u8 riscvMdnsEn;
u8 stRiscvPara[51200];
u8 stIspInitPar[4032];
u8 mdnsInit;
u8 global_ybsnthres[44];
u8 global_ypbv0stathres[44];
u8 global_ypbv1stathres[44];
u8 global_yrefweimin[44];
u8 global_yrefweiminadjthres[44];

extern void *__kmalloc(u32 size, u32 flags);

/* the vendor's private_kmalloc is a thin wrapper of the kernel allocator */
void *private_kmalloc(u32 size, u32 flags)
{
	return __kmalloc(size, flags);
}

int bo_tisp_mdns_init(u32 chn, u8 *ip) { return t33_tisp_mdns_init(chn, ip); }
