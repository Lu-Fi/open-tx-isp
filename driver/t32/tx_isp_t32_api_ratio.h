/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 module ratio setters: readable C of the vendor's tisp_s_sdns_ratio,
 * tisp_s_mdns_ratio and tisp_s_dpc_ratio.  The ratio (128 = tuning as is)
 * comes from the module ratio cache that tisp_s_module_ratio_attr keeps in
 * the attribute block (tisp_get_tuning() = *t32_data_f0904): slot k at
 * ch * 128 + 1060 + k * 8 = { u32 mode, u8 ratio }, mode 0 = 128.
 *
 *  - SDNS (slot 0): the ratio goes to word +16 of the SDNS context
 *    (e2410[ch]), then tisp_sdns_refresh(ch, ctx +8).
 *  - MDNS (slot 1): five 11-entry u8 tables per channel (e22c8 block +0x10,
 *    +0x3c, +0x68, +0x94, +0xc0, 11 B per channel; the block's first words
 *    are the per-channel MDNS contexts) are scaled into the tuning
 *    (dd0a4[ch]) and tisp_mdns_refresh(ch, ctx +8) follows.  Up to 128 the
 *    value scales down, above it moves towards a ceiling (128, 180, 255);
 *    values outside the table's valid range (int8 <= 0, or 0 / above the
 *    ceiling) count as 0.
 *  - DPC (slot 3): four 11-entry u16 strength rows of the tuning are scaled
 *    into the DPC context (e35f0[ch]); above 128 the first two rows are
 *    added to the context's current values.  Then tisp_dpc_gain_interp for
 *    the long and the short frame and tisp_dpc_write_reg.
 * The channel is used unmasked (vendor), so it must be a valid channel.
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_apiratio_oracle).
 */
#ifndef TX_ISP_T32_API_RATIO_H
#define TX_ISP_T32_API_RATIO_H

#include "tx_isp_t32_tp.h"

struct t32_api_ratio_ops {
	int (*sdns_refresh)(u32 ch, u32 arg);
	int (*mdns_refresh)(u32 ch, u32 arg);
	int (*dpc_gain_interp)(u32 ch, u32 is_short);
	int (*dpc_write_reg)(u32 ch);
};

static inline u32 t32_ar_rd32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline u32 t32_ar_rd16(const u8 *p)
{
	return p[0] | (u32)p[1] << 8;
}

static inline void t32_ar_wr16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/* the cached ratio of module slot k */
static inline u32 t32_module_ratio(const u8 *attr, u32 ch, unsigned int k)
{
	const u8 *c = attr + (u32)(ch << 7) + 1060 + k * 8;

	return t32_ar_rd32(c) ? c[4] : 128;
}

/* tisp_s_sdns_ratio */
static inline int t32_s_sdns_ratio(const struct t32_api_ratio_ops *op, const u8 *attr, u8 *ctx, u32 ch)
{
	u32 r = t32_module_ratio(attr, ch, 0);

	ctx[16] = (u8)r;
	ctx[17] = 0;
	ctx[18] = 0;
	ctx[19] = 0;
	return op->sdns_refresh(ch, t32_ar_rd32(ctx + 8));
}

/* one MDNS table entry: v valid when lo <= v <= hi (else 0), moved towards ceil above 128 */
static inline u8 t32_mdns_ratio_one(u32 v, u32 r, int valid, u32 ceil)
{
	if (r <= 128)
		return (u8)(((valid ? v : 0) * r) >> 7);
	return (u8)(v + ((int)((valid ? ceil - v : 0) * (r - 128)) >> 7));
}

#define T32_MDNS_RATIO_DST	0x8bb9	/* tuning offset of the last of the five outputs */

/* tisp_s_mdns_ratio: blk = the e22c8 block (contexts, then the tables) */
static inline int t32_s_mdns_ratio(const struct t32_api_ratio_ops *op, const u8 *attr, u8 *tun,
				   const u8 *blk, const u8 *ctx, u32 ch)
{
	u32 r = t32_module_ratio(attr, ch, 1);
	u8 *d = tun + T32_MDNS_RATIO_DST;
	unsigned int i;

	for (i = 0; i < 11; i++, d++) {
		u32 a = blk[0xc0 + ch * 11 + i], b = blk[0x94 + ch * 11 + i], c = blk[0x68 + ch * 11 + i];
		u32 e = blk[0x3c + ch * 11 + i], f = blk[0x10 + ch * 11 + i];

		d[-800] = t32_mdns_ratio_one(a, r, (s8)a > 0, 128);
		d[-701] = t32_mdns_ratio_one(b, r, (s8)b > 0, 128);
		d[-635] = t32_mdns_ratio_one(c, r, (s8)c > 0, 128);
		d[55] = t32_mdns_ratio_one(e, r, ((e - 1) & 0xff) < 179, 180);
		d[0] = t32_mdns_ratio_one(f, r, ((f - 1) & 0xff) < 254, 255);
	}
	return op->mdns_refresh(ch, t32_ar_rd32(ctx + 8));
}

#define T32_DPC_RATIO_SRC	23004	/* tuning: rows at -22, +242, 0, +264 (11 x u16 each) */

/* tisp_s_dpc_ratio: dctx = e35f0[ch], u16 rows at +24, +46, +68, +90 */
static inline int t32_s_dpc_ratio(const struct t32_api_ratio_ops *op, const u8 *attr, const u8 *tun,
				  u8 *dctx, u32 ch)
{
	u32 r = t32_module_ratio(attr, ch, 3);
	const u8 *s = tun + T32_DPC_RATIO_SRC;
	unsigned int i;

	for (i = 0; i < 11; i++, s += 2) {
		u8 *o = dctx + 2 * i;

		if (r <= 128) {
			int a = (int)(t32_ar_rd16(s - 22) * r) >> 7;
			int b = (int)(t32_ar_rd16(s + 242) * r) >> 7;

			t32_ar_wr16(o + 24, a < 5 ? 5 : a);
			t32_ar_wr16(o + 46, b < 5 ? 5 : b);
			t32_ar_wr16(o + 68, (int)(t32_ar_rd16(s) * r + 128000 - 1000 * r) >> 7);
			t32_ar_wr16(o + 90, (int)(t32_ar_rd16(s + 264) * r + 128000 - 1000 * r) >> 7);
		} else {
			int k = (int)r - 128;

			t32_ar_wr16(o + 24, ((int)(1200 - t32_ar_rd16(s - 22)) * k) / 128 + t32_ar_rd16(o + 24));
			t32_ar_wr16(o + 46, ((int)(1200 - t32_ar_rd16(s + 242)) * k) / 128 + t32_ar_rd16(o + 46));
			t32_ar_wr16(o + 68, (int)(t32_ar_rd16(s) * (256 - r) + 5 * r - 640) >> 7);
			t32_ar_wr16(o + 90, (int)(t32_ar_rd16(s + 264) * (256 - r) + 5 * r - 640) >> 7);
		}
	}
	op->dpc_gain_interp(ch, 0);
	op->dpc_gain_interp(ch, 1);
	return op->dpc_write_reg(ch);
}

#endif
