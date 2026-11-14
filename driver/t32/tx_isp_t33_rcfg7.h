/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) register-list init functions of the MDNS / RDNS / SDNS
 * modules: the vendor's tisp_rcfg_<blk>_reg_init fill the module-attribute
 * table entry (an array of u32 words) with the address/offset words of the
 * block's register list and return the next module id.
 *
 * The MDNS list picks one word from stIspInitPar (mode word at byte 904):
 * mode 1 selects the 0x801037c0 variant, anything else 0x80103420.
 */
#ifndef TX_ISP_T33_RCFG7_H
#define TX_ISP_T33_RCFG7_H

#include "tx_isp_t33_blk.h"

/* the vendor's stIspInitPar mode word (byte offset 904) */
#define T33_ST_ISP_INIT_PAR_MODE_BYTE	904
#define T33_ST_ISP_INIT_PAR_MODE_WORD	(T33_ST_ISP_INIT_PAR_MODE_BYTE / 4)
extern u32 t33_st_isp_init_par_mode[];

/* { word index, value } of a register-list init */
#ifndef T33_RCFG_WORD_T
#define T33_RCFG_WORD_T
struct t33_rcfg_word {
	u16 idx;
	u32 val;
};
#endif

#ifndef T33_RCFG_FILL_FN
#define T33_RCFG_FILL_FN
static inline void t33_rcfg_fill(u32 *e, const struct t33_rcfg_word *t, unsigned int n)
{
	unsigned int k;

	for (k = 0; k < n; k++)
		e[t[k].idx] = t[k].val;
}
#endif

/* tisp_rcfg_mdns_reg_init: 490 words, mode word selects word 18 */
static inline u32 t33_rcfg_mdns_init(u32 *e)
{
	static const struct t33_rcfg_word t[] = {
		{ 0, 489 }, { 1, 0x80183405 }, { 9, 0x801c3410 },
		{ 24, 0x80183440 }, { 32, 0x803c3462 }, { 49, 0x80203474 },
		{ 59, 0x80143480 }, { 66, 0x80143489 }, { 73, 0x80083491 },
		{ 77, 0x801c3496 }, { 86, 0x801434a1 }, { 93, 0x801034ab },
		{ 99, 0x800834b2 }, { 103, 0x801434b8 }, { 110, 0x80003780 },
		{ 112, 0x006c3460 }, { 141, 0x009c3461 }, { 182, 0x00683473 },
		{ 210, 0x004c347e }, { 231, 0x0008347f }, { 235, 0x00283487 },
		{ 247, 0x005c3488 }, { 272, 0x00403490 }, { 290, 0x00243495 },
		{ 301, 0x004c349f }, { 322, 0x000834a0 }, { 326, 0x004834a8 },
		{ 346, 0x005c34a9 }, { 371, 0x002c34aa }, { 384, 0x005434b1 },
		{ 407, 0x004834b6 }, { 427, 0x001834b7 }, { 435, 0x003834bf },
		{ 451, 0x806c3540 }, { 480, 0x80203580 },
	};
	u32 mode = t33_st_isp_init_par_mode[T33_ST_ISP_INIT_PAR_MODE_WORD];

	t33_rcfg_fill(e, t, ARRAY_SIZE(t));
	e[18] = mode == 1 ? 0x801037c0 : 0x80103420;
	return 490;
}

/* tisp_rcfg_rdns_reg_init: 45 words */
static inline u32 t33_rcfg_rdns_init(u32 *e)
{
	static const struct t33_rcfg_word t[] = {
		{ 0, 44 }, { 1, 0x80009808 }, { 6, 0x80009828 }, { 8, 0x8014982d },
		{ 15, 0x001c9812 }, { 24, 0x001c981a }, { 33, 0x00149822 },
		{ 40, 0x000c9829 }, { 3, 0x80049810 },
	};

	t33_rcfg_fill(e, t, ARRAY_SIZE(t));
	return 45;
}

/* tisp_rcfg_sdns_reg_init: 11 words */
static inline u32 t33_rcfg_sdns_init(u32 *e)
{
	static const struct t33_rcfg_word t[] = {
		{ 0, 10 }, { 1, 0x80004c04 }, { 3, 0x80184c08 },
	};

	t33_rcfg_fill(e, t, ARRAY_SIZE(t));
	return 11;
}

#endif /* TX_ISP_T33_RCFG7_H */
