/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) register-list init for the CSCCR, DEFOG and DG modules
 * (openimp #99).  Each vendor tisp_rcfg_<blk>_reg_init writes the command
 * words of a module list into the attribute table entry e (u32 words) and
 * returns the next module id.  The words are the list length, then per
 * command a header word (count / register) and its value slots; the block
 * code fills the value slots later (see tx_isp_t33_blk.h).
 */
#ifndef TX_ISP_T33_RCFG4_H
#define TX_ISP_T33_RCFG4_H

#include "tx_isp_t33_blk.h"

/* tisp_rcfg_csccr_reg_init: module 8, one 8-value command at 0x8004/0x8000 */
static inline u32 t33_rcfg_csccr_init(u32 *e)
{
	e[0] = 8;
	e[1] = 0x80005004;	/* 0x8000 + 20484 */
	e[3] = 0x80045010;	/* 0x8004 + 20496 */
	e[6] = 0x80045014;	/* 0x8004 + 20500 */
	return 9;
}

/* tisp_rcfg_defog_reg_init: module 330, the defog list */
static inline u32 t33_rcfg_defog_init(u32 *e)
{
	e[0] = 330;
	e[1] = 0x80002804;	/* 0x8000 + 10244 */
	e[3] = 0x800c2810;	/* 0x800c + 10256 */
	e[8] = 0x80242881;	/* 0x8024 + 10369 */
	e[19] = 0x80002897;	/* 0x8000 + 10391 */
	e[21] = 0x002c2814;	/* 0x2c + 10260 */
	e[34] = 0x003c2820;	/* 0x3c + 10272 */
	e[51] = 0x001c2830;	/* 0x1c + 10288 */
	e[60] = 0x001c2838;	/* 0x1c + 10296 */
	e[69] = 0x001c2840;	/* 0x1c + 10304 */
	e[78] = 0x001c2848;	/* 0x1c + 10312 */
	e[87] = 0x001c2850;	/* 0x1c + 10320 */
	e[96] = 0x001c2858;	/* 0x1c + 10328 */
	e[105] = 0x001c2860;	/* 0x1c + 10336 */
	e[114] = 0x00202868;	/* 0x20 + 10344 */
	e[124] = 0x001c2871;	/* 0x1c + 10353 */
	e[133] = 0x001c2879;	/* 0x1c + 10361 */
	e[142] = 0x8000288a;	/* 0x8000 + 10378 */
	e[144] = 0x8000c828;	/* 0x8000 | 0xc828 */
	e[146] = 0x02ccc829;	/* 0x2cc << 16 | 0xc829 */
	e[327] = 0x8000c828;	/* 0x8000 | 0xc828 */
	e[329] = 0x8000288a;	/* 0x8000 + 10378 */
	return 331;
}

/* tisp_rcfg_dg_reg_init: module 6, one 4-value command at 0x8008/0x8000 */
static inline u32 t33_rcfg_dg_init(u32 *e)
{
	e[0] = 6;
	e[1] = 0x80001004;	/* 0x8000 + 4100 */
	e[3] = 0x80081018;	/* 0x8008 + 4120 */
	return 7;
}

#endif /* TX_ISP_T33_RCFG4_H */
