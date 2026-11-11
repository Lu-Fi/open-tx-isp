/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 CLM colour temperature update on the kept block state (openimp #234):
 * tisp_clm_ct_update (0x8a7f8) and tisp_clm_ct_interp (0x8a474) per frame.
 *
 * tisp_clm_ct_interp(ch, ct, force) on the block state behind e28d0[ch]
 * (section +0, colour temperature +96, tables +100 / +2202, threshold u16
 * +2200 = 1 from tisp_clm_init, zone code +3260 = 0xff from the init):
 *   code = the zone code of ct (t32_clm_zone_code), diff = |ct - state ct|;
 *   not forced: same code and even (a zone itself or its hysteresis band:
 *   constant tables) -> 1; else diff not above the threshold (signed
 *   compare) -> 1;
 *   then state ct = ct, zone = code and the tables (t32_clm_ct_tables);
 *   -1 when ct lies at or above the upper end of a blend range (the tables
 *   stay), else 0.
 * tisp_clm_ct_update: interp not forced; on 0 the LUT goes out
 * (tisp_clm_write_lut = t32_clm_lut in direct mode, d03a0 = 0; the shadow
 * variant tisp_clm_lut_write_lut is not ported).  Checked bit-exact against
 * Matt Davis' recovered C (PR #15 @ 2e2e06bc) by tests/t32_oracle
 * (t32_clmct_oracle).
 */
#ifndef TX_ISP_T32_CLMCT_H
#define TX_ISP_T32_CLMCT_H

#include "tx_isp_t32_clm.h"

#define T32_CLMCT_THR		1	/* state +2200 after tisp_clm_init */
#define T32_CLMCT_ZONE_INIT	0xff	/* state +3260 after tisp_clm_init */

/* the return code of the table part: -1 when ct lies at or above the upper end of its blend range */
static inline int t32_clm_ct_rc(const u8 *sec, const u8 *za, const u8 *zb, u32 ct)
{
	unsigned int count = sec[0x23], hyst = sec[0x24], v;

	if (!((u32)(int)t32_clm_s16(za) < ct))	/* one zone: the next test returns */
		return 0;
	if (!(ct < (u32)(int)t32_clm_s16(za + (count - 1) * T32_CLM_ZONE_BYTES)))
		return 0;
	for (v = 1; v < count && (u32)(int)t32_clm_s16(za + v * T32_CLM_ZONE_BYTES) < ct; v++)
		;
	if (v >= count)
		return 0;
	if (!((u32)(int)t32_clm_s16(za + (v - 1) * T32_CLM_ZONE_BYTES) + hyst < ct))
		return 0;
	if (!(ct < (u32)((int)(signed char)zb[v * T32_CLM_TABLE] - (int)hyst)))
		return 0;
	if (!(ct < (u32)(int)t32_clm_s16(za + v * T32_CLM_ZONE_BYTES) - hyst))
		return -1;
	return 0;
}

/* tisp_clm_ct_interp on the kept state; thr = state +2200 */
static inline int t32_clm_ct_interp(struct t32_clm_ctx *c, u32 thr, u32 ct, u32 force)
{
	const u8 *za = c->lut, *zb = c->lut + c->sec[0x23] * T32_CLM_ZONE_BYTES;
	u8 code = t32_clm_zone_code(c->sec, za, ct);
	u32 diff = c->ct < ct ? ct - c->ct : c->ct - ct;

	if (!(force & 0xff)) {
		if (c->zone == code && !(code & 1))
			return 1;
		if (!((int)(thr & 0xffff) < (int)diff))
			return 1;
	}
	c->ct = ct;
	c->zone = code;
	t32_clm_ct_tables(c->sec, za, zb, (int)ct, c->tab, c->tb);
	return t32_clm_ct_rc(c->sec, za, zb, ct);
}

/* tisp_clm_ct_update (direct mode): w gets the LUT writes (2 x 422 + 2), returns their number */
static inline int t32_clm_ct_update_st(struct t32_clm_ctx *c, u32 thr, u32 ct, struct t32_front_write *w)
{
	unsigned int n = 0;

	if (t32_clm_ct_interp(c, thr, ct, 0))
		return 0;
	t32_clm_lut(c, w, &n);
	return (int)n;
}

#endif /* TX_ISP_T32_CLMCT_H */
