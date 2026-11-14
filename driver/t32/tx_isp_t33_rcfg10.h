/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) MSCA register list init (tisp_rcfg_msca_reg_init).
 *
 * The vendor function fills the module-attribute table entry (array of u32
 * words) with the MSCA command words; two words depend on the sensor mode
 * (stIspInitPar word 226, byte offset 904: 1 = the second mode).  The return
 * value is the next module id (410).
 */
#ifndef TX_ISP_T33_RCFG10_H
#define TX_ISP_T33_RCFG10_H

#include "tx_isp_t33_blk.h"

#define T33_MSCA_NEXT_ID	410

/* stIspInitPar word 226 (byte 904): the sensor mode selector */
#define T33_ISP_INIT_MODE_WORD	226

/* tisp_rcfg_msca_reg_init: fill e[] with the MSCA list, return the next id */
static inline u32 t33_rcfg_msca_init(u32 *e, const u32 *init_par)
{
	u32 mode = init_par[T33_ISP_INIT_MODE_WORD];
	u32 v;

	e[0] = 409;
	e[1] = 0x8003c004;
	e[26] = 0x8017c040;
	e[3] = 0x8057c010;
	e[33] = mode == 1 ? 0x8057c1ad : 0x8057c048;
	e[56] = 0x8017c080;
	e[63] = mode == 1 ? 0x8057c1ad : 0x8057c088;
	e[86] = 0x8017c0c0;
	e[93] = mode == 1 ? 0x8057c1ad : 0x8057c0c8;
	e[166] = 0x8017c134;
	e[173] = 0x8063c13c;
	e[199] = 0x000fc130;
	e[204] = 0x006fc158;
	e[233] = 0x0087c15c;
	e[116] = 0x80bbc100;
	e[164] = 0x8003c131;
	e[303] = 0x8003c131 + 1743;
	e[402] = 0x8003c131 + 1743;
	e[404] = 0x0003c05c;
	e[406] = 0x0003c05c + 64;
	e[408] = 0x0003c05c + 128;
	e[305] = 0x17fc801;
	e[268] = 0x0087c15c + 4;
	if (mode == 1) {
		v = 0x8003c131;
		e[404] = v - 213;
		e[406] = v - 149;
		e[408] = v - 85;
	} else {
		e[404] = 0x8013c1ad;
	}
	return T33_MSCA_NEXT_ID;
}

#endif /* TX_ISP_T33_RCFG10_H */
