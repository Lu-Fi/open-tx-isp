/*
 * T31 IMP_ISP_Tuning_SetSharpness mapping (driver/t31/tx_isp_t31_sharpness.h)
 * against values worked out by hand from the OEM tisp_dmsc_sharpness_set
 * (tx-isp-t31.ko 0x263bc, docs/re/tx-isp-t31.ko_hlil.txt) with the curve
 * values of the bank built into the OEM driver (sp_d_w 90..40, sp_ud_w 80..40).
 */
#include <stdint.h>
#include <stdio.h>

typedef uint32_t u32;
#include "../driver/t31/tx_isp_t31_sharpness.h"

struct row { u32 sharp, src, max, want; };

int main(void)
{
	static const struct row rows[] = {
		{ 128, 90, T31_DMSC_SHARP_SP_MAX, 90 },   /* neutral: the tuned curve */
		{ 0, 90, T31_DMSC_SHARP_SP_MAX, 0 },      /* off */
		{ 64, 90, T31_DMSC_SHARP_SP_MAX, 45 },    /* half */
		{ 129, 90, T31_DMSC_SHARP_SP_MAX, 93 },   /* (510 * 1) >> 7 = 3 */
		{ 200, 90, T31_DMSC_SHARP_SP_MAX, 376 },  /* (510 * 72) >> 7 = 286 */
		{ 255, 90, T31_DMSC_SHARP_SP_MAX, 596 },  /* (510 * 127) >> 7 = 506 */
		{ 255, 40, T31_DMSC_SHARP_SP_MAX, 595 },  /* high-gain point: ~15x */
		{ 255, 100, T31_DMSC_SHARP_UU_MAX, 794 }, /* uu curve, max 0x320 */
		{ 255, 40, T31_DMSC_SHARP_UU_MAX, 794 }, /* (760 * 127) >> 7 = 754 */
		{ 0x1ff, 90, T31_DMSC_SHARP_SP_MAX, 596 }, /* byte, as the ioctl path */
	};
	unsigned int i;
	int bad = 0;
	u32 prev = 0, s;

	for (i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
		u32 got = t31_dmsc_sharpness_scale(rows[i].sharp, rows[i].src, rows[i].max);

		if (got != rows[i].want) {
			fprintf(stderr, "sharp %u src %u max %u: got %u want %u\n",
				rows[i].sharp, rows[i].src, rows[i].max, got, rows[i].want);
			bad = 1;
		}
	}
	/* monotonic over the whole control range, no wrap below the maximum */
	for (s = 0; s < 256; ++s) {
		u32 v = t31_dmsc_sharpness_scale(s, 40, T31_DMSC_SHARP_SP_MAX);

		if (v < prev || v > T31_DMSC_SHARP_SP_MAX) {
			fprintf(stderr, "sharp %u: %u after %u\n", s, v, prev);
			bad = 1;
		}
		prev = v;
	}
	if (bad)
		return 1;
	printf("t31 sharpness mapping: ok (255 = %u for a tuned 40, OEM formula)\n",
	       t31_dmsc_sharpness_scale(255, 40, T31_DMSC_SHARP_SP_MAX));
	return 0;
}
