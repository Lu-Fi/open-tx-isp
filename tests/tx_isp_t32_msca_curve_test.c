/* Host test: T32 MSCA polyphase curves (driver/t32/tx_isp_t32_msca_curve.h)
 * against the coefficients the vendor tisp loaded in our QEMU traces
 * (3840x2160 input; 1920x1080, 1280x720 and 640x360 outputs). */
#include <stdio.h>
#include <stdlib.h>
#include "../driver/t32/tx_isp_t32_msca_curve.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static const struct { unsigned int in, out; int c[T32_CURVE_TAPS]; } vendor[] = {
	{ 3840, 1920, { 983, 980, 961, 925, 874, 809, 734, 651, 564, 479, 396, 314, 234,
			163, 99, 44, 0, -33, -60, -77, -84, -86, -82, -73, -63 } },
	{ 3840, 1280, { 628, 621, 611, 598, 583, 565, 544, 521, 496, 461, 425, 390, 354,
			319, 283, 248, 215, 179, 146, 114, 87, 62, 39, 18, -1 } },
	{ 3840, 640, { 406, 404, 401, 399, 396, 393, 390, 387, 384, 376, 368, 360, 352,
		       345, 337, 329, 321, 309, 298, 286, 276, 265, 254, 243, 232 } },
};

/* every phase sums to 2048; phase 0 shares its taps with the others and may
 * end 1 off, as the vendor's own check allows (2047 / 2049) */
static int phases_ok(const int *c)
{
	int p, j;

	for (p = 0; p < T32_CURVE_PHASES; p++) {
		int s = 0;

		for (j = 0; j < 6; j++)
			s += c[abs(T32_CURVE_PHASES * (j - 2) - p)];
		if (p ? s != T32_CURVE_SUM : abs(s - T32_CURVE_SUM) > 1)
			return 0;
	}
	return 1;
}

int main(void)
{
	int c[T32_CURVE_TAPS], i, j, diff = 0, maxd = 0;
	unsigned int w[T32_CURVE_WORDS];

	for (i = 0; i < 3; i++) {
		t32_msca_curve(vendor[i].in, vendor[i].out, c);
		for (j = 0; j < T32_CURVE_TAPS; j++) {
			int d = abs(c[j] - vendor[i].c[j]);

			diff += d != 0;
			maxd = d > maxd ? d : maxd;
		}
		CHECK(phases_ok(c));
	}
	CHECK(maxd <= 1);
	CHECK(diff <= 6);
	/* 1:6 is identical */
	t32_msca_curve(3840, 640, c);
	for (j = 0; j < T32_CURVE_TAPS; j++)
		CHECK(c[j] == vendor[2].c[j]);

	/* ratio: Q14, vendor limit, no division by zero */
	CHECK(t32_msca_curve_ratio(1920, 1920) == 16384);
	CHECK(t32_msca_curve_ratio(1000, 2000) == T32_CURVE_RATIO_MAX);
	CHECK(t32_msca_curve_ratio(0, 640) == T32_CURVE_RATIO_MAX);
	/* 1:1 passes the centre phase through */
	t32_msca_curve(1920, 1920, c);
	CHECK(c[0] == 2048 && c[8] == 0 && c[16] == 0 && c[24] == 0 && phases_ok(c));

	/* packing as the vendor's words: 3840 -> 1920 starts 0x03d403d7 ... */
	for (j = 0; j < T32_CURVE_TAPS; j++)
		c[j] = vendor[0].c[j];
	t32_msca_curve_pack(c, w);
	CHECK(w[0] == 0x03d403d7 && w[8] == 0x1fdf0000 && w[11] == 0x1fb71fae && w[12] == 0x00001fc1);
	t32_msca_curve_box(c);
	t32_msca_curve_pack(c, w);
	CHECK(w[0] == 0x01550155 && w[12] == 0x00000155);

	printf("tx_isp_t32_msca_curve_test: %s (%d of 75 vendor coefficients differ, at most by %d)\n",
	       bad ? "FAIL" : "PASS", diff, maxd);
	return bad != 0;
}
