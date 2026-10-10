/* SPDX-License-Identifier: MIT */
#ifndef TX_ISP_T41_DRC_RATIO_H
#define TX_ISP_T41_DRC_RATIO_H
/*
 * IMP_ISP_Tuning_SetModule_Ratio, DRC unit (stock tisp_s_drc_ratio,
 * H20250310a 0x537d4, linear mode).  The DRC engine is the ADR block.  Ratio
 * 128 is neutral.  Seven fields of eleven s16 knots are rebuilt from the
 * pristine ADR calibration (block offsets below):
 *   strength fields (6, 22 bytes apart from +1068), values in percent
 *   (100 = no effect, 250 = maximum), only 101..249 take part:
 *     ratio <= 128: 100 + ((x - 100) * ratio >> 7)   (x outside: 100)
 *     ratio  > 128: x + ((ratio - 128) * (250 - x) >> 7)
 *   gain-limit field (+980), only 1..6999 take part:
 *     ratio <= 128: x * ratio >> 7                    (x outside: 0)
 *     ratio  > 128: x + ((ratio - 128) * (7000 - x) >> 7)
 * The ADR runtime then re-evaluates the exposure curves.
 */
#define T41_DRC_RATIO_BLOCK_BYTES 0xa60U
#define T41_DRC_RATIO_LIMIT 980U
#define T41_DRC_RATIO_STRENGTH 1068U
#define T41_DRC_RATIO_FIELDS 6U
#define T41_DRC_RATIO_KNOTS 11U

static inline int t41_drc_ratio_s16(const unsigned char *p)
{
	return (short)(p[0] | (p[1] << 8));
}

static inline int t41_drc_ratio_strength(int x, int ratio)
{
	unsigned int ux = (unsigned int)x & 0xffff;
	int in = ux - 101U < 149U;

	if (ratio <= 128)
		return 100 + (((in ? x - 100 : 0) * ratio) >> 7);
	return x + (((ratio - 128) * (in ? 250 - x : 0)) >> 7);
}

static inline int t41_drc_ratio_limit(int x, int ratio)
{
	unsigned int ux = (unsigned int)x & 0xffff;
	int in = ux - 1U < 6999U;

	if (ratio <= 128)
		return ((in ? x : 0) * ratio) >> 7;
	return x + (((ratio - 128) * (in ? 7000 - x : 0)) >> 7);
}

static inline int t41_drc_ratio_scale(unsigned char *cur,
		const unsigned char *base, unsigned int bytes, unsigned int ratio)
{
	unsigned int f, k;

	if (!cur || !base || bytes < T41_DRC_RATIO_BLOCK_BYTES || ratio > 255)
		return -1;
	for (k = 0; k < T41_DRC_RATIO_KNOTS; ++k) {
		unsigned int off = T41_DRC_RATIO_LIMIT + k * 2;
		int v = t41_drc_ratio_limit(t41_drc_ratio_s16(base + off), (int)ratio);
		cur[off] = (unsigned char)v;
		cur[off + 1] = (unsigned char)((unsigned int)v >> 8);
	}
	for (f = 0; f < T41_DRC_RATIO_FIELDS; ++f)
		for (k = 0; k < T41_DRC_RATIO_KNOTS; ++k) {
			unsigned int off = T41_DRC_RATIO_STRENGTH + f * 22 + k * 2;
			int v = t41_drc_ratio_strength(t41_drc_ratio_s16(base + off), (int)ratio);
			cur[off] = (unsigned char)v;
			cur[off + 1] = (unsigned char)((unsigned int)v >> 8);
		}
	return 0;
}
#endif
