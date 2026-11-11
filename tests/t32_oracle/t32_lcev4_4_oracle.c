// SPDX-License-Identifier: GPL-2.0
/* phase 4: the whole first 16 tile group (t32_lce_v4_grp); vendor truncated at the first arrival at L_4ecf0.  See t32_lcev4_common.inc. */
#define ORACLE_NAME "t32_lcev4_4_oracle"
#include "t32_lcev4_common.inc"
static void gen_phase(u8 *par)
{
	par[12] = (u8)(1 + rnd() % 18);
	par[13] = (u8)(16 + rnd() % 17);
}
static int port_phase(struct t32_lce_v4_st *st)
{
	t32_lce_v4_grp(st, 0, 0);
	return 0;
}
