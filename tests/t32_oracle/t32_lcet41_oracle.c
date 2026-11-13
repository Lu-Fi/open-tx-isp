// SPDX-License-Identifier: GPL-2.0
/* end to end: the whole Tisp_lce_soft_t41_simd (all 72 tile groups) against t32_lce_t41; compares the whole working buffer.  See t32_lcet41_common.inc. */
#define ORACLE_NAME "t32_lcet41_oracle"
#define PHASE 0
#include "t32_lcet41_common.inc"
static void port_phase(struct t32_lce_t41_st *st)
{
	t32_lce_t41(st->par, st->w, st->mode, st->ops);
}
