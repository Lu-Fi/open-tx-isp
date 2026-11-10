// SPDX-License-Identifier: GPL-2.0
/* phase 1 of Tisp_lce_soft_t41_simd: t32_lce_t41_blk1 (prologue .. new target); vendor truncated at L_55268.  See t32_lcet41_common.inc. */
#define ORACLE_NAME "t32_lcet41_1_oracle"
#include "t32_lcet41_common.inc"
static u32 port_bss_len(void) { return 0; }
static u32 port_bss_off(void) { return 0; }
static const u8 *port_bss(void) { return 0; }
static void gen_extra(u8 *par, u8 *w) { (void)par; (void)w; }
static void port_phase(const u8 *par, u8 *w, u32 mode, const struct t32_lce_t41_ops *o)
{
	t32_lce_t41_blk1(par, w, mode, o);
}
