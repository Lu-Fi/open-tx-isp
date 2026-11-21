/* SPDX-License-Identifier: GPL-2.0 */
/*
 * binary oracle, block "t32-sharp": tisp_set_sharpness of the vendor tx-isp-t32.ko (T32_OEM_KO) vs
 * driver/t32/tx_isp_t32_sharp.h (openimp #711).  The vendor's register stores go through tisp_reg_write
 * (recorded as calls, in order; the block triggers 0x8010 / 0x11010 / 0xd014 are tail calls of it, too);
 * the wrapper hands our write list to the same callee.  The vendor's own tisp_dmsc_intp / tisp_ysp_intp / *_reg_cfg / tisp_ratio /
 * tisp_simple_intp_* run unchanged on its side.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_sharp.h"

struct t32_dmsc_ctx *dmsc_info[4];
struct t32_ysp_ctx *ysp_info[4];
u8 *ysp_paramsP;
u32 ysp_ratio;
u8 cfgMode;
u8 *ispcore_sd;
extern void tisp_reg_write(u32 ch, u32 reg, u32 val);

static struct t32_front_write bo_w[T32_SHARP_MAX_WRITES];

void bo_tisp_set_sharpness(u32 ch, u32 level)
{
	int n = t32_sharpness_set(dmsc_info[ch], ysp_info[ch], (u8)ch, ysp_paramsP, &ysp_ratio, level, bo_w), i;

	for (i = 0; i < n; i++)
		tisp_reg_write(ch, bo_w[i].reg, bo_w[i].val);
}
