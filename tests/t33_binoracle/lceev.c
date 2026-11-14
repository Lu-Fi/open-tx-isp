/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "lceev" (openimp #500): the LCE / LSF EV update (driver/t32/tx_isp_t33_lceev.h) and
 * the init it starts from, built for mipsel (tests/t33_binoracle/build_emu.sh) and run against the vendor
 * tisp_lce_init / tisp_lce_ev_update / func_lce_para_update / tisp_lce_bin_para_update / tisp_lce_gausswgt_calc
 * of tx-isp-t33.ko by tools/ko_emu/diff_t33_lceev.py on the same lceInfo block, section and lists.
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_lceev.h"

u32 emu_lcemode[2];	/* the vendor's lcemodeflag[0] / lcemodecounter[0] */

/* tisp_lce_init's lceInfo part; s = the section (sized like the vendor's tisp_lce_size_para_calc) */
void emu_init(struct t33_lce_pf *c, u8 *s, u32 w, u32 h)
{
	t33_lce_size(s, h, w);
	t33_lcepf_init(c, s, w, h);
}

/* tisp_lce_ev_update(0, 0, lo, hi, bv); bufs: curve pa, hist pa */
void emu_ev(struct t33_lce_pf *c, u8 *s, u32 lo, u32 hi, u32 bv, const u32 *bufs, u32 *l16, u32 *l37)
{
	t33_lcepf_ev_update(c, s, lo, hi, bv, bufs[0], bufs[1], l16, l37, emu_lcemode);
}

/* func_lce_para_update / tisp_lce_bin_para_update / tisp_lce_gausswgt_calc on their own */
void emu_para(struct t33_lce_pf *c, u8 *s) { t33_lcepf_para_update(c, s); }
void emu_bin(struct t33_lce_pf *c, u8 *s) { t33_lcepf_bin_para(c, s); }
void emu_gw(struct t33_lce_pf *c, u8 *s) { t33_lcepf_gausswgt(c, s); }
