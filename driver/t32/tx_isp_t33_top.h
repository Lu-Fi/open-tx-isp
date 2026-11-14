/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) TOP block: bypass word and register-write trigger.
 *
 * The vendor keeps the TOP state in stTopPar (an array of pointers, one per
 * ISP id, each pointing at a 32-byte bypass word at +12) and a refresh flag
 * tispTopRefresh.  tisp_top_reg_write asks the rcfg layer whether the DMA is
 * running and either programs the TOP registers (tisp_top_reg_cfg) or just
 * marks the refresh pending.
 */
#ifndef TX_ISP_T33_TOP_H
#define TX_ISP_T33_TOP_H

#include "tx_isp_t33_blk.h"

/* vendor module data objects (defined by the driver, plain globals) */
extern u8 ipcCfgMode;
extern u8 tispTopRefresh;
extern u32 *stTopPar[256];

/* callees ported elsewhere / hardware (recorded by the oracle) */
extern u32 tisp_reg_read_t33(u32 id, u32 reg, u32 sel);
extern u32 tisp_rcfg_get_dma_state(u32 id);
extern void tisp_top_reg_cfg(u32 id);

/* tisp_top_reg_write: program TOP now when the DMA is idle, else mark refresh */
static inline u32 t33_top_reg_write(u32 id)
{
    u32 r;

    if (!tisp_rcfg_get_dma_state(33)) {
        tisp_top_reg_cfg(id & 0xff);
        r = 1;
    } else {
        r = 2;
    }
    tispTopRefresh = (u8)r;
    return r;
}

/* tisp_top_get_bypass: read the TOP bypass register (sel 64, or 1 when ipcCfgMode) */
static inline u32 t33_top_get_bypass(u32 id)
{
    u32 sel = ipcCfgMode ? 1 : 64;

    return tisp_reg_read_t33(id & 0xff, 33, sel);
}

/* tisp_top_set_bypass: store the 32 bypass bits into stTopPar[id] + 12, then trigger */
static inline u32 t33_top_set_bypass(u32 id, u32 bits)
{
    u32 *p = stTopPar[id & 0xff];
    unsigned int k;

    for (k = 0; k < 32; k++)
        ((u8 *)p)[12 + k] = (bits >> k) & 1;
    t33_top_reg_write(id & 0xff);
    return 0;
}

#endif /* TX_ISP_T33_TOP_H */
