/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "append": tisp_get_module_enum and tisp_bin_append_data_parse
 * (driver/t32/tx_isp_t33_append.h) with the vendor calling convention and the vendor's globals: tparamsP[ch]
 * (section 7 at +0x4450), stLutInfo[ch] (u16 size at +2, table pointer at +0x14), lutMaxSize[ch] (28 bytes each,
 * the LSC word at +4), ldcParExist, stLdcPar.
 */
#include <stdint.h>
#include "tx_isp_t33_append.h"

u8 *tparamsP[4];
u8 *stLutInfo[4];
u32 lutMaxSize[28];
u8 ldcParExist[8];
u8 stLdcPar[640];

extern u8 *private_vmalloc(u32 n);
extern void private_vfree(void *p);
extern int isp_printf(int level, const char *fmt, const char *fn, u32 line, u32 val);

static u8 *bo_info;

static int bo_grow(struct t33_app_lsc *l, u32 need)
{
	u8 *n = private_vmalloc(need);
	u32 i;

	for (i = 0; i < l->size16; i++)
		n[i] = l->lut[i];
	if (l->lut)
		private_vfree(l->lut);
	l->lut = n;
	*(u8 **)(bo_info + 0x14) = n;
	return 0;
}

static void bo_log(int what, u32 val)
{
	static const u16 line[] = { 913, 946, 982, 391 };

	isp_printf(what == T33_APP_LOG_NULL ? 2 : 1, "", "", line[what], val);
}

static const struct t33_app_ops bo_ops = { bo_grow, bo_log };

u32 bo_tisp_get_module_enum(const char *name)
{
	return t33_append_enum(name);
}

int bo_tisp_bin_append_data_parse(u32 ch, const u8 *app, u32 size)
{
	struct t33_app_lsc l;
	int r;

	ch &= 0xff;
	bo_info = stLutInfo[ch];
	l.sec = tparamsP[ch] + 0x4450;
	l.lut = *(u8 **)(bo_info + 0x14);
	l.size16 = *(u16 *)(bo_info + 2);
	l.cap = lutMaxSize[ch * 7 + 1];
	r = t33_bin_append_parse(ch, app, size, ldcParExist, stLdcPar, &l, &bo_ops);
	lutMaxSize[ch * 7 + 1] = l.cap;
	return r;
}
