/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 defog ratio setter: readable C of the vendor's tisp_s_defog_ratio.
 *
 * The defog context X = e2130[ch] holds the parameter section P (X +12),
 * the work block Q (X +16) and the current EV (X +24).  The ratio (slot 4
 * of the module ratio cache, ch * 128 + 1092 in tisp_get_tuning(); 128 when
 * there is no tuning or the slot is in mode 0) scales the five 9-byte
 * transmission tables of the active bank's tuning (dd0b4[ch * 6 + bank],
 * bank = byte 125 of the channel state t32_data_dc284 + ch * 904, tuning
 * offsets 0x8466 + 9 k) into P + 610 + 9 k (tisp_defog_strength_itp).
 * Unless the section is frozen (P[590]) the EV is copied to P + 4 and the
 * tables are interpolated at it into P + 256 (tisp_defog_interp_trans_k);
 * when the 5 result bytes are unchanged (dfg_array_cmp == 0) the 256 bytes
 * Q + 3178 keep only their low nibble.  The function returns nothing.
 *
 * Vendor: the bank byte is not range checked (bank >= 6 reads past the
 * channel's six bank pointers); the channel is used unmasked except for
 * tisp_defog_interp_trans_k.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_apidfg_oracle).
 */
#ifndef TX_ISP_T32_API_DFG_H
#define TX_ISP_T32_API_DFG_H

#include "tx_isp_t32_tp.h"

struct t32_api_dfg_ops {
	void (*strength_itp)(u32 ratio, const u8 *in, u8 *out);
	void (*interp_trans_k)(u32 ch, u32 ev, const u8 *thr, const u8 *ta, const u8 *tb, const u8 *tc,
			       const u8 *td, const u8 *te, u8 *out);
	int (*array_cmp)(const u8 *a, const u8 *b, u32 n);
};

#define T32_DFG_TUNE_TABLES	0x8466	/* 5 x 9 B in the bank tuning */
#define T32_DFG_P_TABLES	610	/* 5 x 9 B in the parameter section */
#define T32_DFG_P_OUT		256
#define T32_DFG_P_FROZEN	590
#define T32_DFG_Q_NIBBLES	3178

/* index of the active bank's tuning pointer in dd0b4 (bank byte +125 of the channel state) */
static inline u32 t32_defog_bank_index(const u8 *dc284, u32 ch)
{
	return ch * 6 + dc284[ch * 904 + 125];
}

/*
 * tisp_s_defog_ratio: p, q, ev = the words +12, +16, +24 of X = e2130[ch],
 * tun = tisp_get_tuning(), bank = dd0b4[t32_defog_bank_index()]
 */
static inline void t32_s_defog_ratio(const struct t32_api_dfg_ops *op, u8 *p, u8 *q, u32 ev, const u8 *tun,
				     const u8 *bank, u32 ch)
{
	u32 r = 128, k;
	u8 old[5];

	if (tun) {
		const u8 *c = tun + (u32)(ch << 7) + 1092;

		if (c[0] | c[1] | c[2] | c[3])
			r = c[4];
	}
	for (k = 0; k < 5; k++)
		op->strength_itp(r, bank + T32_DFG_TUNE_TABLES + 9 * k, p + T32_DFG_P_TABLES + 9 * k);
	for (k = 0; k < 5; k++)
		old[k] = p[T32_DFG_P_OUT + k];
	if (p[T32_DFG_P_FROZEN])
		return;
	p[4] = (u8)ev;
	p[5] = (u8)(ev >> 8);
	p[6] = (u8)(ev >> 16);
	p[7] = (u8)(ev >> 24);
	op->interp_trans_k(ch & 0xff, ev, p + 8, p + 610, p + 619, p + 628, p + 637, p + 646, p + T32_DFG_P_OUT);
	if (op->array_cmp(old, p + T32_DFG_P_OUT, 5))
		return;
	for (k = 0; k < 256; k++)
		q[T32_DFG_Q_NIBBLES + k] &= 0xf;
}

#endif
