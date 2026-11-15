/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) block refresh wrappers: tisp_cdns_refresh, tisp_rdns_refresh,
 * tisp_ysp_refresh.  Each looks up the per-channel block state in the module
 * pointer array and tail-calls the block's par_refresh with the state's gain
 * word (byte offset 440 / 844 / 2180 of the state).  The second argument
 * register is passed through untouched, as in the vendor code.
 */
#ifndef TX_ISP_T33_REFA_H
#define TX_ISP_T33_REFA_H

#include "tx_isp_t33_blk.h"

#ifndef T33_REFA_GLUE
extern u32 *cdnsInfo[4];
extern u32 *rdnsInfo[4];
extern u32 *yspInfo[4];
extern void tisp_cdns_par_refresh(u32 ch, u32 *inf, u32 gain);
extern void tisp_rdns_par_refresh(u32 ch, u32 *inf, u32 gain);
extern void tisp_ysp_par_refresh(u32 ch, u32 *inf, u32 gain);
#endif

/* tisp_cdns_refresh: gain word at byte 440 of cdnsInfo[ch] */
static inline void t33_cdns_refresh(u32 ch, u32 *inf)
{
	u32 *st = cdnsInfo[ch];

	tisp_cdns_par_refresh(ch, inf, st[440 / 4]);
}

/* tisp_rdns_refresh: gain word at byte 844 of rdnsInfo[ch] */
static inline void t33_rdns_refresh(u32 ch, u32 *inf)
{
	u32 *st = rdnsInfo[ch];

	tisp_rdns_par_refresh(ch, inf, st[844 / 4]);
}

/* tisp_ysp_refresh: gain word at byte 2180 of yspInfo[ch] */
static inline void t33_ysp_refresh(u32 ch, u32 *inf)
{
	u32 *st = yspInfo[ch];

	tisp_ysp_par_refresh(ch, inf, st[2180 / 4]);
}

#endif /* TX_ISP_T33_REFA_H */
