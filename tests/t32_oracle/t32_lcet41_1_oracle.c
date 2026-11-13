// SPDX-License-Identifier: GPL-2.0
/* phase 1 of Tisp_lce_soft_t41_simd: blk1 (prologue .. new target); vendor truncated at L_55268.  See t32_lcet41_common.inc. */
#define ORACLE_NAME "t32_lcet41_1_oracle"
#define PHASE 1
#include "t32_lcet41_common.inc"
static void port_phase(struct t32_lce_t41_st *st)
{
	t32_lce_t41_blk1(st->par, st->w, st->mode, st->ops);
}
