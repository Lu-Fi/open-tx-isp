// SPDX-License-Identifier: GPL-2.0
/* end to end: the whole Tisp_lce_soft_simd_v4 (all 18 x 32 tiles, four quadrants) against t32_lce_v4.  See t32_lcev4_common.inc. */
#define ORACLE_NAME "t32_lcev4_oracle"
#include "t32_lcev4_common.inc"
static void gen_phase(u8 *par)
{
	(void)par;
}
static int port_phase(struct t32_lce_v4_st *st)
{
	return t32_lce_v4(st->par, st->w, st->mode, st->band, st->ops, st);
}
