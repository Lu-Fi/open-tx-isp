/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) register-list init functions of the BCSH, LDC and LSF
 * modules: the vendor's tisp_rcfg_<blk>_reg_init fills the module
 * attribute table entry (an array of u32 words) with the list constants
 * and returns the next module id.
 */
#ifndef TX_ISP_T33_RCFG9_H
#define TX_ISP_T33_RCFG9_H

#include "tx_isp_t33_blk.h"

/* one { word index, value } of a module attribute table entry */
#ifndef T33_RCFG_WORD_T
#define T33_RCFG_WORD_T
struct t33_rcfg_word {
	u16 idx;
	u32 val;
};
#endif

#ifndef T33_RCFG_FILL_FN
#define T33_RCFG_FILL_FN
static inline void t33_rcfg_fill(u32 *e, const struct t33_rcfg_word *w, unsigned int n)
{
	unsigned int k;

	for (k = 0; k < n; k++)
		e[w[k].idx] = w[k].val;
}
#endif

/* tisp_rcfg_bcsh_reg_init: module 53, next 54 */
static inline u32 t33_rcfg_bcsh_init(u32 *e)
{
	static const struct t33_rcfg_word w[] = {
		{ 0, 53 },
		{ 1, 0x80183c1f },
		{ 9, 0x80303c33 },
		{ 23, 0x00143c10 },
		{ 30, 0x00083c16 },
		{ 34, 0x00103c19 },
		{ 40, 0x00303c26 },
	};

	t33_rcfg_fill(e, w, ARRAY_SIZE(w));
	return 54;
}

/* tisp_rcfg_ldc_reg_init: module 19, next 20 */
static inline u32 t33_rcfg_ldc_init(u32 *e)
{
	static const struct t33_rcfg_word w[] = {
		{ 0, 19 },
		{ 1, 0x80089c10 },
		{ 5, 0x80089c1b },
		{ 9, 0x80049c08 },
		{ 12, 0x80048413 },
		{ 15, 0x80008421 },
		{ 17, 0x80049c08 - 7154 },
	};

	t33_rcfg_fill(e, w, ARRAY_SIZE(w));
	return 20;
}

/* tisp_rcfg_lsf_reg_init: module 70, next 71 */
static inline u32 t33_rcfg_lsf_init(u32 *e)
{
	static const struct t33_rcfg_word w[] = {
		{ 0, 70 },
		{ 1, 0x80109410 },
		{ 21, 0x80109438 },
		{ 27, 0x00109415 },
		{ 7, 0x8030941a },
		{ 33, 0x001c9428 },
		{ 42, 0x001c9430 },
		{ 51, 0x001c943e },
		{ 60, 0x001c9446 },
		{ 69, 0x00009410 },
	};

	t33_rcfg_fill(e, w, ARRAY_SIZE(w));
	return 71;
}

#endif /* TX_ISP_T33_RCFG9_H */
