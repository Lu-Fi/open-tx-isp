/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "bcshI": the BCSH EV interpolation helpers
 * (driver/t32/tx_isp_t33_bcshI.h) with the calling convention of the
 * vendor functions they port, built for mipsel (tests/t33_binoracle/run.sh)
 * and run against the vendor tx-isp-t33.ko instructions by
 * tools/ko_emu/t33_binoracle.py (spec: bcshI.conf, FUNCS).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_bcshI.h"

/* the vendor's bcsh_para[]: one state pointer per channel */
void *bcsh_para[4];

int bo_tisp_bcsh_sub_interp(u32 f, int a, int b) { return t33_bcsh_sub_interp(f, a, b); }
void bo_tisp_bcsh_get_new_para(u8 *sec, u8 *t) { t33_bcsh_get_new_para(sec, t); }
int bo_tisp_bcsh_interp_by_ev(u32 ch, u32 unused, u32 ev_lo, u32 ev_hi, u32 force)
{
	return t33_bcsh_interp_by_ev(ch, unused, ev_lo, ev_hi, force);
}
