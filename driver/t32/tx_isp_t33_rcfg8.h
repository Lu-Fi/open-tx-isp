/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) register-list init for the TMO / YDNS / YSP modules
 * (tisp_rcfg_tmo_reg_init, tisp_rcfg_ydns_reg_init, tisp_rcfg_ysp_reg_init).
 *
 * Each vendor function fills the module-attribute table entry e (u32 words)
 * with the list length, the command words and the value slots, and returns
 * the next module id.
 */
#ifndef TX_ISP_T33_RCFG8_H
#define TX_ISP_T33_RCFG8_H

#include "tx_isp_t33_blk.h"

/* tisp_rcfg_tmo_reg_init: TMO list, returns the next module id */
static inline u32 t33_rcfg_tmo_init(u32 *e)
{
	e[0] = 0xc1;		/* TMO list length */
	e[1] = 0x80001c04;
	e[3] = 0x80441c08;
	e[22] = 0x801c1c1e;
	e[31] = 0x800c1c3b;
	e[36] = 0x00301c1a;
	e[63] = 0x00301c1f;
	e[77] = 0x00241c23;
	e[88] = 0x80041c37;
	e[91] = 0x8000c89c;
	e[195] = 0x8000c89c;
	e[197] = 0x8000c898;
	e[1700] = 0x8000c898;
	e[55] = 0x000c1c1c;
	e[93] = 0x0190c89d;
	e[50] = 0x000c1c1b;
	e[60] = 0x00041c1d;
	e[199] = 0x176cc899;
	return 0xc2;
}

/* tisp_rcfg_ydns_reg_init: YDNS list, returns the next module id */
static inline u32 t33_rcfg_ydns_init(u32 *e)
{
	e[0] = 7;
	e[1] = 0x80003804;
	e[3] = 0x800c3814;
	return 8;
}

/* tisp_rcfg_ysp_reg_init: YSP list, returns the next module id */
static inline u32 t33_rcfg_ysp_init(u32 *e)
{
	e[0] = 113;
	e[1] = 0x80004404;
	e[3] = 0x800c4410;
	e[8] = 0x80404416;
	e[26] = 0x8004445f;
	e[29] = 0x80004465;
	e[31] = 0x8000446d;
	e[33] = 0x80104470;
	e[39] = 0x80104486;
	e[45] = 0x001c4427;
	e[54] = 0x001c4437;
	e[63] = 0x000c4447;
	e[68] = 0x000c444f;
	e[73] = 0x000c4457;
	e[78] = 0x000c4461;
	e[83] = 0x00044466;
	e[86] = 0x0008446a;
	e[90] = 0x00044475;
	e[93] = 0x00044477;
	e[96] = 0x00044479;
	e[99] = 0x0010447b;
	e[105] = 0x00044480;
	e[108] = 0x00044482;
	e[111] = 0x00044484;
	return 114;
}

#endif /* TX_ISP_T33_RCFG8_H */
