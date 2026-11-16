/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "mscachx": tisp_msca_t33_chx_cfg_load / tisp_msca_t33_chx_en (openimp #99, #643) */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_mscachx.h"

extern void tisp_msca_t33_scaling_algorithm(u32 id, u32 ch);
extern void tisp_msca_t33_curve_calc(u32 id, u32 ch);
extern void tisp_msca_t33_ch_curve_write(u32 id, u32 ch);
extern int tisp_msca_t33_ctrl_reg_write(u32 id, u32 ch);
extern u32 tisp_reg_read_t33(u32 id, u32 blk, u32 reg);
extern void tisp_reg_write_t33(u32 id, u32 blk, u32 reg, u32 val);

u8 *bo_msca_info[4];
u8 *bo_msca_attr[4];
u8 bo_cfg_mode;

static void w_scl(u32 a, u32 b) { tisp_msca_t33_scaling_algorithm(a, b); }
static void w_cc(u32 a, u32 b) { tisp_msca_t33_curve_calc(a, b); }
static void w_cw(u32 a, u32 b) { tisp_msca_t33_ch_curve_write(a, b); }
static int w_ctl(u32 a, u32 b) { return tisp_msca_t33_ctrl_reg_write(a, b); }
static u32 w_rd(u32 a, u32 b, u32 c) { return tisp_reg_read_t33(a, b, c); }
static void w_wr(u32 a, u32 b, u32 c, u32 d) { tisp_reg_write_t33(a, b, c, d); }

static const struct t33_mscachx m = {
	bo_msca_info, bo_msca_attr, &bo_cfg_mode, w_scl, w_cc, w_cw, w_ctl, w_rd, w_wr,
};

int bo_tisp_msca_t33_chx_cfg_load(u32 id, u32 ch)
{
	return t33_msca_chx_cfg_load_v(&m, id, ch);
}

void bo_tisp_msca_t33_chx_en(u32 id, u32 ch, u32 en)
{
	t33_msca_chx_en_v(&m, id, ch, en);
}
