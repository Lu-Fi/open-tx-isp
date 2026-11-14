/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "lscmisc": lens shading info helpers with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_lscmisc.h"

u32 *lsc_para[4];
u8 lscCfgMode;
u8 tispLscRefresh;

extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 sel, u32 val);
extern int tisp_rcfg_get_dma_state(u32 blk);

static void lsc_wr(u32 ch, u32 blk, u32 sel, u32 val)
{
	tisp_reg_write_t33(ch, blk, sel, val);
}

static int lsc_dma(u32 blk)
{
	return tisp_rcfg_get_dma_state(blk);
}

int bo_tisp_lsc_bin2user(u32 ch)
{
	t33_lsc_bin_to_user(lsc_para[ch]);
	return 0;
}

void bo_func_lsc_para_update(u32 ch)
{
	t33_lsc_para_update_info(lsc_para[ch]);
}

void bo_tisp_lsc_lut_valid_check(int h, int w, int mh, int mw, u8 *rem_h, u8 *rem_w, u8 *rows, u8 *cols,
				 u32 *cells, u8 *valid, u32 *stride)
{
	t33_lsc_mesh_check(h, w, mh, mw, rem_h, rem_w, rows, cols, cells, valid, stride);
}

int bo_tisp_lsc_write_reg(u32 ch)
{
	tispLscRefresh = (u8)t33_lsc_write_regs(lsc_para[ch], ch, lscCfgMode, lsc_wr, lsc_dma);
	return 0;
}
