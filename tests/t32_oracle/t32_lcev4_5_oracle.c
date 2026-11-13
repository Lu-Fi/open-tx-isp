// SPDX-License-Identifier: GPL-2.0
/* phase 5: quadrant 0 of tile row 0 (16 tile group if 16 columns, then the scalar tiles, blk4); vendor truncated at the first arrival at L_4f178.  See t32_lcev4_common.inc. */
#define ORACLE_NAME "t32_lcev4_5_oracle"
#include "t32_lcev4_common.inc"
static void gen_phase(u8 *par)
{
	par[12] = (u8)(1 + rnd() % 18);
	par[13] = (u8)(rnd() % 33);
}
static int port_phase(struct t32_lce_v4_st *st)
{
	return t32_lce_v4_quad(st, 0, 0, st->par[13], 0);
}
