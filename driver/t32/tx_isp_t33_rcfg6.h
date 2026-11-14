/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) register-list init functions of the IPC, LCE and LSC blocks
 * (vendor tisp_rcfg_ipc_reg_init / tisp_rcfg_lce_reg_init /
 * tisp_rcfg_lsc_reg_init).  Each writes the module id and the command words
 * of its layout into the module-attribute table entry e (u32 words) and
 * returns the next module id.
 */
#ifndef TX_ISP_T33_RCFG6_H
#define TX_ISP_T33_RCFG6_H

#include "tx_isp_t33_blk.h"

/* tisp_rcfg_ipc_reg_init: module 13, returns 14 */
static inline u32 t33_rcfg_ipc_init(u32 *e)
{
	e[0] = 13;
	e[1] = 0x80000010;
	e[3] = 0x80000018;
	e[5] = 0x8004001b;
	e[8] = 0x8000001e;
	e[10] = 0x80000040;
	e[12] = 0x800000bd;
	return 14;
}

/* tisp_rcfg_lce_reg_init: module 40, returns 41 */
static inline u32 t33_rcfg_lce_init(u32 *e)
{
	e[0] = 40;
	e[1] = 0x80003004;
	e[3] = 0x80303010;
	e[17] = 0x80043020;
	e[27] = 0x80003038;
	e[20] = 0x80143024;
	e[29] = 0x0010301b;
	e[35] = 0x00103036;
	return 41;
}

/* tisp_rcfg_lsc_reg_init: module 18, returns 19 */
static inline u32 t33_rcfg_lsc_init(u32 *e)
{
	e[0] = 18;
	e[1] = 0x80001404;
	e[3] = 0x80241410;
	e[14] = 0x800c141c;
	return 19;
}

#endif /* TX_ISP_T33_RCFG6_H */
