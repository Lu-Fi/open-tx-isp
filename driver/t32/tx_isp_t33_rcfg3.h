/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) register-list init for the CDNS, CLM and CSC modules
 * (tisp_rcfg_cdns_reg_init, tisp_rcfg_clm_reg_init, tisp_rcfg_csc_reg_init).
 *
 * Each vendor function fills the module-attribute table entry (a1, an array
 * of u32 words) with the module id, the list base addresses and a few
 * offset words, and returns the next module id.  The words are the vendor's
 * own constants (address/offset words); the table below is { word index,
 * value } per module.
 */
#ifndef TX_ISP_T33_RCFG3_H
#define TX_ISP_T33_RCFG3_H

#include "tx_isp_t33_blk.h"

/* tisp_rcfg_cdns_reg_init: module 23, returns 24 */
static inline u32 t33_rcfg_cdns_init(u32 *e)
{
	e[0] = 23;
	e[1] = 0x80004804;
	e[3] = 0x80004810;
	e[5] = 0x80444814;
	return 24;
}

/* tisp_rcfg_clm_reg_init: module 868, returns 869 */
static inline u32 t33_rcfg_clm_init(u32 *e)
{
	e[0] = 868;
	e[1] = 0x80004004;
	e[3] = 0x80144010;
	e[10] = 0x801c4024;
	e[19] = 0x8000c830;
	e[21] = 0x068cc831;
	e[442] = 0x8000c830;
	e[444] = 0x8000c840;
	e[446] = 0x068cc841;
	e[867] = 0x8000c840;
	return 869;
}

/* tisp_rcfg_csc_reg_init: module 10, returns 11 */
static inline u32 t33_rcfg_csc_init(u32 *e)
{
	e[0] = 10;
	e[1] = 0x80002c04;
	e[3] = 0x80182c10;
	return 11;
}

#endif /* TX_ISP_T33_RCFG3_H */
