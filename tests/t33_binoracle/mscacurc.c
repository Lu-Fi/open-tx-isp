/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mscacurc": tisp_msca_t33_curve_calc with its real callees on both sides (vendor:
 * tisp_sin + the normalisation functions of the module; ours: t33_sin on the vendor kernel tables and the ported
 * normalisation): the coefficient sets the channel start computes (openimp #99, #643)
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r13B.h"
#include "tx_isp_t33_sinlut.h"
#include "tx_isp_t33_mscacur.h"

extern int tisp_reg_write_t33(u32 id, u32 mod, u32 reg, u32 val);

u8 *bo_msca_info[4];
u8 *bo_msca_attr[4];

static void w_sin(u32 n, u32 cnt, short *out) { t33_sin(n, cnt, out); }
static void w_two(u32 n, u32 taps, short *c) { t33_msca_two_point_v(n, taps, c); }
static void w_thr(u32 n, u32 taps, short *c) { t33_msca_thr_point_v(n, taps, c); }
static void w_four(u32 n, u32 taps, short *c) { t33_msca_four_point_v(n, taps, c); }
static void w_six(u32 n, u32 taps, short *c) { t33_msca_six_point_v(n, taps, c); }

static const struct t33_mscacur m = {
	bo_msca_info, bo_msca_attr, NULL, NULL, NULL, tisp_reg_write_t33, w_sin,
	w_two, w_thr, w_four, w_six,
};

void bo_tisp_msca_t33_curve_calc(u32 id, u32 ch)
{
	t33_msca_curve_calc_v(&m, id, ch);
}
