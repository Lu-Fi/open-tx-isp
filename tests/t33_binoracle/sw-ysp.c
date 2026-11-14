/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "sw-ysp": tisp_ysp_switch_para with the vendor calling convention.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_swysp.h"

extern int tisp_ysp_bin_data_refresh(u32 ch);
extern int tisp_ysp_reg_data_refresh(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 id);
extern int tisp_reg_write_t33(u32 ch, u32 id, u32 slot, u32 val);
extern int tisp_mdns_decode_reg_write(u32 ch, u32 *info);

u8 *tparamsP[4];
u8 *ysp_paramsP[4];
u32 *yspInfo[4];
u8 yspCfgMode;
u8 tispYspRefresh;

static void w_copy(void *d, const void *s, u32 n) { memcpy(d, s, n); }
static void w_bin(u32 ch) { tisp_ysp_bin_data_refresh(ch); }
static void w_reg(u32 ch) { tisp_ysp_reg_data_refresh(ch); }
static int w_dma(u32 id) { return tisp_rcfg_get_dma_state(id); }
static void w_write(u32 ch, u32 id, u32 slot, u32 val) { tisp_reg_write_t33(ch, id, slot, val); }
static void w_mdns(u32 ch, u32 *info) { tisp_mdns_decode_reg_write(ch, info); }

static const struct t33_swysp_ops ops = { w_copy, w_bin, w_reg, w_dma, w_write, w_mdns };

int bo_tisp_ysp_switch_para(u32 ch, u32 *info)
{
	if (info)
		yspInfo[0] = info;
	return t33_ysp_switch_para(&ops, ch & 0xff, tparamsP[ch], ysp_paramsP[ch], yspInfo[ch], yspCfgMode,
				   &tispYspRefresh);
}
