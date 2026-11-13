// SPDX-License-Identifier: GPL-2.0
/* phase 2: blk1, blk2 (cdf * par[112]), blk3 (tile gain loop of group 0 / row 0); vendor truncated at the first arrival at L_55554.  See t32_lcet41_common.inc. */
#define ORACLE_NAME "t32_lcet41_2_oracle"
#define PHASE 2
#include "t32_lcet41_common.inc"
static void port_phase(struct t32_lce_t41_st *st)
{
	t32_lce_t41_blk1(st->par, st->w, st->mode, st->ops);
	t32_lce_t41_blk2(st);
	t32_lce_t41_blk3(st, 0, 0);
}
