/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) register-list init functions of the AWBG, BLC and CCM
 * modules: the vendor's tisp_rcfg_<blk>_reg_init fills the module
 * attribute table entry (an array of u32 words) with the module id, the
 * list base addresses and the list length, and returns the next module id.
 *
 * The words are the vendor's own constants (list base | offset); the
 * layout is a small table of { word index, value } per module.
 */
#ifndef TX_ISP_T33_RCFG2_H
#define TX_ISP_T33_RCFG2_H

#include "tx_isp_t33_blk.h"

/* list base addresses of the three modules (vendor constants) */
#define T33_RCFG_AWBG_BASE	0x80000000U
#define T33_RCFG_BLC_BASE	0x80000000U
#define T33_RCFG_CCM_BASE	0x80000000U

struct t33_rcfg_word {
	u16 idx;	/* word index in the attribute table */
	u32 val;
};

/* tisp_rcfg_awbg_reg_init: module 5, next 6 */
static inline u32 t33_rcfg_awbg_init(u32 *e)
{
	static const struct t33_rcfg_word w[] = {
		{ 0, 5 },
		{ 1, T33_RCFG_AWBG_BASE + 6148 },
		{ 3, 0x80040000U + 6160 },
	};
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(w); k++)
		e[w[k].idx] = w[k].val;
	return 6;
}

/* tisp_rcfg_blc_reg_init: module 9, next 10 */
static inline u32 t33_rcfg_blc_init(u32 *e)
{
	static const struct t33_rcfg_word w[] = {
		{ 0, 9 },
		{ 1, T33_RCFG_BLC_BASE + 3076 },
		{ 3, 0x80140000U + 3089 },
	};
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(w); k++)
		e[w[k].idx] = w[k].val;
	return 10;
}

/* tisp_rcfg_ccm_reg_init: module 37, next 38 */
static inline u32 t33_rcfg_ccm_init(u32 *e)
{
	static const struct t33_rcfg_word w[] = {
		{ 0, 37 },
		{ 1, T33_RCFG_CCM_BASE + 9220 },
		{ 3, 0x80200000U + 9232 },
		{ 13, 0x80000000U + 9249 },
		{ 15, 0x00100000U + 9247 },
		{ 21, 0x002c0000U + 9250 },
		{ 34, 0x00080000U + 9261 },
	};
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(w); k++)
		e[w[k].idx] = w[k].val;
	return 38;
}

#endif /* TX_ISP_T33_RCFG2_H */
