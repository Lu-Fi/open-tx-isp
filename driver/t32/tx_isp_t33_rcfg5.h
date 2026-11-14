/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) register-list initialisers for the DMSC, DPC and GAMMA
 * modules: the vendor's tisp_rcfg_<blk>_reg_init fills the module
 * attribute table (an array of u32 words) with the command words of the
 * layout and returns the next module id.
 *
 * Each entry is { word index, value }; the value is a raw register address
 * (the vendor's lui/addiu pairs).  Word 0 is the command count.
 */
#ifndef TX_ISP_T33_RCFG5_H
#define TX_ISP_T33_RCFG5_H

#include "tx_isp_t33_blk.h"

struct t33_rc5_ent {
	u16 word;
	u32 val;
};

static inline void t33_rc5_fill(u32 *e, const struct t33_rc5_ent *t, unsigned int n)
{
	unsigned int k;

	for (k = 0; k < n; k++)
		e[t[k].word] = t[k].val;
}

/* tisp_rcfg_dmsc_reg_init: 144 words, module 145 */
static inline u32 t33_rcfg_dmsc_init(u32 *e)
{
	static const struct t33_rc5_ent t[] = {
		{ 0, 144 },
		{ 1, 0x80002004 },
		{ 3, 0x80402010 },
		{ 21, 0x80042025 },
		{ 24, 0x8038202b },
		{ 40, 0x8000205f },
		{ 42, 0x80102062 },
		{ 48, 0x80082071 },
		{ 52, 0x800c2077 },
		{ 57, 0x802c207f },
		{ 70, 0x80042095 },
		{ 73, 0x8044209f },
		{ 92, 0x802020b5 },
		{ 102, 0x800020c1 },
		{ 104, 0x803c20c9 },
		{ 121, 0x000c203a },
		{ 126, 0x00182042 },
		{ 134, 0x00182049 },
		{ 142, 0x0004208b },
	};

	t33_rc5_fill(e, t, ARRAY_SIZE(t));
	return 145;
}

/* tisp_rcfg_dpc_reg_init: 47 words, module 48 */
static inline u32 t33_rcfg_dpc_init(u32 *e)
{
	static const struct t33_rc5_ent t[] = {
		{ 0, 47 },
		{ 1, 0x80000804 },
		{ 3, 0x80200810 },
		{ 13, 0x8008081d },
		{ 17, 0x80140824 },
		{ 24, 0x8000082d },
		{ 26, 0x80080831 },
		{ 30, 0x80080838 },
		{ 34, 0x8000085c },
		{ 36, 0x00040819 },
		{ 39, 0x0004081b },
		{ 42, 0x00040820 },
		{ 45, 0x00040822 },
	};

	t33_rc5_fill(e, t, ARRAY_SIZE(t));
	return 48;
}

/* tisp_rcfg_gamma_reg_init: 266 words, module 267 */
static inline u32 t33_rcfg_gamma_init(u32 *e)
{
	static const struct t33_rc5_ent t[] = {
		{ 0, 266 },
		{ 1, 0x8000c810 },
		{ 3, 0x01fcc811 },
		{ 132, 0x8000c810 },
		{ 134, 0x8000c820 },
		{ 136, 0x01fcc821 },
		{ 265, 0x8000c820 },
	};

	t33_rc5_fill(e, t, ARRAY_SIZE(t));
	return 267;
}

#endif /* TX_ISP_T33_RCFG5_H */
