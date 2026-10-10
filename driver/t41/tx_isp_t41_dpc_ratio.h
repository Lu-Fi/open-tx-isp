/* SPDX-License-Identifier: MIT */
#ifndef TX_ISP_T41_DPC_RATIO_H
#define TX_ISP_T41_DPC_RATIO_H
/*
 * IMP_ISP_Tuning_SetModule_Ratio, DPC unit (stock tisp_s_dpc_ratio,
 * H20250310a 0x3ed58).  Ratio 128 is neutral.  The long-frame bank of the
 * DPC calibration (block offset 0x31e, eleven gain knots per field, 22
 * bytes per field) has four fields scaled from the pristine table:
 *   fields 0 and 12 (block +0x31e / +0x426), "detection thresholds":
 *     ratio <= 128: max(5, base * ratio >> 7)
 *     ratio  > 128: base + (1200 - base) * (ratio - 128) / 128  (C division)
 *   fields 1 and 13 (+0x334 / +0x43c):
 *     ratio <= 128: (base * ratio + 128000 - 1000 * ratio) >> 7
 *     ratio  > 128: (base * (256 - ratio) + 5 * ratio - 640) >> 7
 * The caller re-runs the gain interpolation afterwards.
 */
#define T41_DPC_RATIO_NEUTRAL 128U
#define T41_DPC_RATIO_BANK 0x31eU
#define T41_DPC_RATIO_KNOTS 11U

static inline unsigned int t41_dpc_ratio_le16(const unsigned char *p)
{ return p[0] | (p[1] << 8); }

static inline int t41_dpc_ratio_threshold(unsigned int base, unsigned int ratio)
{
	if (ratio <= T41_DPC_RATIO_NEUTRAL) {
		int v = (int)((base * ratio) >> 7);
		return v < 5 ? 5 : v;
	}
	return (int)base + (int)(((long long)(1200 - (int)base) * (int)(ratio - 128)) / 128);
}

static inline int t41_dpc_ratio_slope(unsigned int base, unsigned int ratio)
{
	if (ratio <= T41_DPC_RATIO_NEUTRAL)
		return ((int)(base * ratio) + 128000 - 1000 * (int)ratio) >> 7;
	return ((int)base * (int)(256 - ratio) + 5 * (int)ratio - 640) >> 7;
}

/* cur and base: DPC calibration blocks (0x5a2 bytes).  ratio 0..255. */
static inline int t41_dpc_ratio_scale(unsigned char *cur,
		const unsigned char *base, unsigned int bytes, unsigned int ratio)
{
	static const unsigned short field[4] = { 0, 12, 1, 13 };
	unsigned int f, k;

	if (!cur || !base || bytes < 0x5a2 || ratio > 255)
		return -1;
	for (f = 0; f < 4; ++f)
		for (k = 0; k < T41_DPC_RATIO_KNOTS; ++k) {
			unsigned int off = T41_DPC_RATIO_BANK + field[f] * 22 + k * 2;
			unsigned int b = t41_dpc_ratio_le16(base + off);
			int v = f < 2 ? t41_dpc_ratio_threshold(b, ratio)
				      : t41_dpc_ratio_slope(b, ratio);
			cur[off] = (unsigned char)v;
			cur[off + 1] = (unsigned char)((unsigned int)v >> 8);
		}
	return 0;
}
#endif
