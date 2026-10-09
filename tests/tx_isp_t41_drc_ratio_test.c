#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_drc_ratio.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int get(const unsigned char *p, unsigned int off) { return t41_drc_ratio_s16(p + off); }
static void put(unsigned char *p, unsigned int off, int v) { p[off] = v; p[off+1] = (unsigned)v >> 8; }
int main(void)
{
	unsigned char base[0xa60], cur[0xa60];
	unsigned int k, f, i;
	memset(base, 0x5a, sizeof(base));
	for (k = 0; k < 11; ++k) {
		put(base, 980 + 2*k, 1000 + 500 * k);
		for (f = 0; f < 6; ++f) put(base, 1068 + f*22 + 2*k, 90 + 20 * k + f);
	}
	memcpy(cur, base, sizeof(cur));
	CHECK(!t41_drc_ratio_scale(cur, base, sizeof(cur), 128));
	for (k = 0; k < 11; ++k) {
		CHECK(get(cur, 980 + 2*k) == get(base, 980 + 2*k));
		/* neutral keeps values inside 101..249 and maps outside values to 100 */
		int x = get(base, 1068 + 2*k);
		CHECK(get(cur, 1068 + 2*k) == (x > 100 && x < 250 ? x : 100));
	}
	/* hand computed */
	CHECK(t41_drc_ratio_strength(200, 64) == 150);
	CHECK(t41_drc_ratio_strength(200, 0) == 100);
	CHECK(t41_drc_ratio_strength(200, 255) == 200 + ((127 * 50) >> 7));
	CHECK(t41_drc_ratio_strength(100, 255) == 100);		/* 100 is outside 101..249 */
	CHECK(t41_drc_ratio_strength(250, 0) == 100);
	CHECK(t41_drc_ratio_strength(250, 200) == 250);
	CHECK(t41_drc_ratio_strength(-5, 200) == -5);
	CHECK(t41_drc_ratio_limit(4000, 64) == 2000);
	CHECK(t41_drc_ratio_limit(7000, 64) == 0);
	CHECK(t41_drc_ratio_limit(0, 64) == 0);
	CHECK(t41_drc_ratio_limit(4000, 192) == 4000 + ((64 * 3000) >> 7));
	CHECK(t41_drc_ratio_limit(7000, 255) == 7000);
	memcpy(cur, base, sizeof(cur));
	CHECK(!t41_drc_ratio_scale(cur, base, sizeof(cur), 0));
	for (k = 0; k < 11; ++k) {
		CHECK(get(cur, 980 + 2*k) == 0);
		for (f = 0; f < 6; ++f) CHECK(get(cur, 1068 + f*22 + 2*k) == 100);
	}
	/* nothing outside the seven fields moves */
	for (i = 0; i < sizeof(cur); ++i) {
		int in = (i >= 980 && i < 1002);
		for (f = 0; f < 6; ++f) if (i >= 1068 + f*22 && i < 1068 + f*22 + 22) in = 1;
		if (!in) CHECK(cur[i] == base[i]);
	}
	CHECK(t41_drc_ratio_scale(cur, base, 100, 128) == -1);
	CHECK(t41_drc_ratio_scale(cur, base, sizeof(cur), 256) == -1);
	printf(fails ? "drc_ratio FAILED\n" : "drc_ratio ok\n");
	return fails != 0;
}
