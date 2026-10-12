/* SPDX-License-Identifier: MIT */
/*
 * T23/T31 GetAwbZone block (driver/include/tx_isp/tx_isp_awb_zone.h):
 * layout of the 675-byte IMPISPAWBZone, the stock scaling (truncated byte
 * of sum / pixels, 0 for an empty zone) with synthetic statistics, and the
 * freshness rule.
 */
#include <stdio.h>
#include <string.h>

#include <tx_isp/tx_isp_awb_zone.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; \
	fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
	static uint32_t r[TX_ISP_AWB_ZONES], g[TX_ISP_AWB_ZONES];
	static uint32_t b[TX_ISP_AWB_ZONES], pix[TX_ISP_AWB_ZONES];
	static uint8_t out[TX_ISP_AWB_ZONE_BYTES + 4];
	unsigned int i;

	CHECK(TX_ISP_AWB_ZONES == 225 && TX_ISP_AWB_ZONE_BYTES == 675);

	/* means: a 2 MP sensor zone (85 x 48 Bayer cells = 4080 pixels) of
	 * 8-bit samples; the sums stay within the 21-bit statistics field */
	for (i = 0; i < TX_ISP_AWB_ZONES; i++) {
		pix[i] = 4080;
		r[i] = (i % 256) * 4080;
		g[i] = ((i * 3) % 256) * 4080;
		b[i] = ((255 - i) % 256) * 4080;
		CHECK(r[i] <= 0x1fffff && g[i] <= 0x1fffff && b[i] <= 0x1fffff);
	}
	memset(out, 0xaa, sizeof(out));
	tx_isp_awb_zone_pack(r, g, b, pix, out);
	for (i = 0; i < TX_ISP_AWB_ZONES; i++) {
		CHECK(out[i] == i % 256);
		CHECK(out[225 + i] == (i * 3) % 256);
		CHECK(out[450 + i] == (255 - i) % 256);
	}
	CHECK(out[675] == 0xaa && out[676] == 0xaa);	/* nothing beyond */

	/* truncation, not rounding (stock .b of an integer division) */
	pix[0] = 100; r[0] = 199; g[0] = 150; b[0] = 101;
	tx_isp_awb_zone_pack(r, g, b, pix, out);
	CHECK(out[0] == 1 && out[225] == 1 && out[450] == 1);
	CHECK(tx_isp_awb_zone_mean(199, 100) == 1);
	CHECK(tx_isp_awb_zone_mean(200, 100) == 2);
	/* a saturated zone is 255, the byte wraps only at the impossible 256 */
	CHECK(tx_isp_awb_zone_mean(255 * 4080, 4080) == 255);
	CHECK(tx_isp_awb_zone_mean(0x1fffff, 1) == (uint8_t)0x1fffff);
	/* an empty zone reads 0 in all three planes */
	pix[7] = 0; r[7] = 12345; g[7] = 1; b[7] = 0x1fffff;
	tx_isp_awb_zone_pack(r, g, b, pix, out);
	CHECK(out[7] == 0 && out[225 + 7] == 0 && out[450 + 7] == 0);
	/* the maximum of the statistics fields does not trap */
	pix[8] = 0x1fff; r[8] = g[8] = b[8] = 0x1fffff;
	tx_isp_awb_zone_pack(r, g, b, pix, out);
	CHECK(out[8] == (uint8_t)(0x1fffff / 0x1fff));

	/* freshness: never computed, within the window, too old */
	CHECK(tx_isp_awb_zone_fresh(0, 5000, TX_ISP_AWB_ZONE_MAX_AGE_MS) == 0);
	CHECK(tx_isp_awb_zone_fresh(5000, 5000, TX_ISP_AWB_ZONE_MAX_AGE_MS) == 1);
	CHECK(tx_isp_awb_zone_fresh(5000, 5999, TX_ISP_AWB_ZONE_MAX_AGE_MS) == 1);
	CHECK(tx_isp_awb_zone_fresh(5000, 6000, TX_ISP_AWB_ZONE_MAX_AGE_MS) == 1);
	CHECK(tx_isp_awb_zone_fresh(5000, 6001, TX_ISP_AWB_ZONE_MAX_AGE_MS) == 0);
	CHECK(tx_isp_awb_zone_fresh(1, 1, TX_ISP_AWB_ZONE_MAX_AGE_MS) == 1);

	if (failures)
		fprintf(stderr, "%d failure(s)\n", failures);
	else
		puts("tx_isp_awb_zone_test: ok");
	return failures ? 1 : 0;
}
