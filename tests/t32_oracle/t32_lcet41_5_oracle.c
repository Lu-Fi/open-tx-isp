// SPDX-License-Identifier: GPL-2.0
/* phase 5: + blk7 (limiter, blend; 8 tiles); vendor truncated at the first arrival at L_56354.  See t32_lcet41_common.inc. */
#define ORACLE_NAME "t32_lcet41_5_oracle"
#define PHASE 5
#include "t32_lcet41_common.inc"
static void port_phase(struct t32_lce_t41_st *st)
{
	t32_lce_t41_blk1(st->par, st->w, st->mode, st->ops);
	t32_lce_t41_blk2(st);
	t32_lce_t41_blk3(st, 0, 0);
	t32_lce_t41_blk4(st);
	t32_lce_t41_blk5(st);
	t32_lce_t41_blk6(st);
	t32_lce_t41_blk7(st);
}
