/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 block "refB": the per-channel refresh entry points of YDNS, SDNS and
 * DMSC (vendor tisp_ydns_refresh / tisp_sdns_refresh / tisp_dmsc_refresh).
 *
 * Each one looks up the channel's info block in the module's pointer array
 * (<blk>Info[ch]), takes the gain word at a fixed offset of that block and
 * tail-calls the block's par_refresh with (ch, a1 passed through, gain).
 * The par_refresh callees are ported elsewhere; they are passed in as
 * function pointers so this header stays independent of them.
 */
#ifndef TX_ISP_T33_REFB_H
#define TX_ISP_T33_REFB_H

#include "tx_isp_t33_blk.h"

/* gain word byte offsets inside the per-channel info block */
#define T33_YDNS_GAIN_OFF	100
#define T33_SDNS_GAIN_OFF	152
#define T33_DMSC_GAIN_OFF	2692

typedef void (*t33_par_refresh_fn)(u32 ch, u32 arg, u32 gain);

/* tisp_ydns_refresh: ydnsInfo[ch] -> tisp_ydns_par_refresh(ch, a1, gain); returns 0 */
static inline int t33_ydns_refresh(u32 ch, u32 a1, u32 *const *infos, t33_par_refresh_fn par_refresh)
{
	u32 *v = infos[ch];

	par_refresh(ch, a1, v[T33_YDNS_GAIN_OFF >> 2]);
	return 0;
}

/* tisp_sdns_refresh: sdnsInfo[ch] -> tisp_sdns_par_refresh(ch, a1, gain); returns 0 */
static inline int t33_sdns_refresh(u32 ch, u32 a1, u32 *const *infos, t33_par_refresh_fn par_refresh)
{
	u32 *v = infos[ch];

	par_refresh(ch, a1, v[T33_SDNS_GAIN_OFF >> 2]);
	return 0;
}

/* tisp_dmsc_refresh: dmscInfo[ch] -> tisp_dmsc_par_refresh(ch, a1, gain); returns 0 */
static inline int t33_dmsc_refresh(u32 ch, u32 a1, u32 *const *infos, t33_par_refresh_fn par_refresh)
{
	u32 *v = infos[ch];

	par_refresh(ch, a1, v[T33_DMSC_GAIN_OFF >> 2]);
	return 0;
}

#endif /* TX_ISP_T33_REFB_H */
