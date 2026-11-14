/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "msca-line": tisp_msca_t33_api_set_line_out_cfg / tisp_msca_t33_set_line_reg
 * (driver/t32/tx_isp_t33_mscaline.h) with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mscaline.h"

extern void tisp_reg_write_t33(u32 ch, u32 list, u32 slot, u32 v);
extern int printk(const char *fmt, ...);
extern int isp_printf(int level, const char *fmt, ...);

u8 *stMscaAttr[3];
u32 g_colotList[5];
u8 mscaCfgMode;
u8 tispMscaRefresh;

static void bo_wr(u32 ch, u32 reg, u32 v) { tisp_reg_write_t33(ch, 23, reg, v); }
static void bo_refresh(u32 ch) { (void)ch; tispMscaRefresh = 1; }
static void bo_note(u32 what)
{
	if (what == T33_ML_NOTE_FULL || what == T33_ML_NOTE_SAME)
		isp_printf(1, "line\n");
	else
		printk("line\n");
}

static const struct t33_line_ops bo_ops = { bo_wr, bo_refresh, bo_note };

void bo_tisp_msca_t33_set_line_reg(u32 ch)
{
	ch &= 0xff;
	t33_msca_line_set_reg(stMscaAttr[ch], g_colotList, mscaCfgMode != 0, &bo_ops, ch);
}

void bo_tisp_msca_t33_api_set_line_out_cfg(u32 ch, u8 *p)
{
	ch &= 0xff;
	t33_msca_line_set_out(stMscaAttr[ch], g_colotList, mscaCfgMode != 0, &bo_ops, ch, p);
}
