/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) module-attribute table entries for the AE / AF / AWB blocks
 * (tisp_rcfg_ae_reg_init, tisp_rcfg_af_reg_init, tisp_rcfg_awb_reg_init).
 *
 * Each vendor function fills a few words of the module-attribute table
 * entry (an array of u32 words) with the module id and the address/offset
 * words of the block's register list, and returns the next module id.
 * The words are the vendor's own constants (base 0x80000000 + offset).
 */
#ifndef TX_ISP_T33_RCFG1_H
#define TX_ISP_T33_RCFG1_H

#include "tx_isp_t33_blk.h"

#define T33_RCFG_BASE	0x80000000U

/* tisp_rcfg_ae_reg_init: module 22, returns 23 */
static inline u32 t33_rcfg_ae_init(u32 *e)
{
	e[0] = 22;
	e[1] = T33_RCFG_BASE + 23556;
	e[3] = 0x80040000U + 23568;
	e[6] = 0x80340000U + 23577;
	e[21] = T33_RCFG_BASE + 23598;
	return 23;
}

/* tisp_rcfg_af_reg_init: module 52, returns 53 */
static inline u32 t33_rcfg_af_init(u32 *e)
{
	e[0] = 52;
	e[1] = T33_RCFG_BASE + 24580;
	e[3] = 0x80040000U + 24592;
	e[6] = 0x80a40000U + 24605;
	e[49] = 0x80080000U + 24649;
	return 53;
}

/* tisp_rcfg_awb_reg_init: module 45, returns 46 */
static inline u32 t33_rcfg_awb_init(u32 *e)
{
	e[0] = 45;
	e[1] = T33_RCFG_BASE + 22532;
	e[3] = 0x80040000U + 22544;
	e[6] = 0x80980000U + 22553;
	return 46;
}

#endif /* TX_ISP_T33_RCFG1_H */
