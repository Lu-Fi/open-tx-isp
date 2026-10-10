#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_dpc_ratio.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static unsigned int get(const unsigned char *p, unsigned int off) { return p[off] | (p[off+1] << 8); }
int main(void)
{
	unsigned char base[0x5a2], cur[0x5a2];
	unsigned int k, i;
	memset(base, 0xcc, sizeof(base));
	for (i = 0; i < 4; ++i)
		for (k = 0; k < 11; ++k) {
			unsigned int off = 0x31e + (unsigned short[]){0,12,1,13}[i]*22 + k*2, v = 100 + k * 10 + i;
			base[off] = v; base[off+1] = v >> 8;
		}
	/* neutral ratio reproduces the base table */
	memcpy(cur, base, sizeof(cur));
	CHECK(!t41_dpc_ratio_scale(cur, base, sizeof(cur), 128));
	CHECK(!memcmp(cur, base, sizeof(cur)));
	/* hand computed from the stock arithmetic, base = 100 (field 0, knot 0) */
	CHECK(t41_dpc_ratio_threshold(100, 64) == 50);
	CHECK(t41_dpc_ratio_threshold(100, 0) == 5);		/* floor 5 */
	CHECK(t41_dpc_ratio_threshold(100, 200) == 100 + (1100 * 72) / 128);
	CHECK(t41_dpc_ratio_threshold(100, 256 - 1) == 100 + (1100 * 127) / 128);
	CHECK(t41_dpc_ratio_threshold(1300, 200) == 1300 + (-100 * 72) / 128);	/* C division truncs to zero */
	CHECK(t41_dpc_ratio_slope(100, 64) == (6400 + 128000 - 64000) >> 7);
	CHECK(t41_dpc_ratio_slope(100, 128) == 100);
	CHECK(t41_dpc_ratio_slope(100, 200) == (100 * 56 + 1000 - 640) >> 7);
	CHECK(t41_dpc_ratio_slope(0, 255) == (255 * 5 - 640 + 0) >> 7);	/* arithmetic shift of a positive */
	CHECK(t41_dpc_ratio_slope(0, 200) == (1000 - 640) >> 7);
	CHECK(t41_dpc_ratio_slope(0, 129) == 0);	/* (0 + 645 - 640) >> 7 */
	/* applied table: only the four fields change, everything else untouched */
	memcpy(cur, base, sizeof(cur));
	CHECK(!t41_dpc_ratio_scale(cur, base, sizeof(cur), 64));
	CHECK(get(cur, 0x31e) == 50 && get(cur, 0x31e + 12*22) == (unsigned int)t41_dpc_ratio_threshold(get(base, 0x31e + 12*22), 64));
	CHECK(get(cur, 0x31e + 22) == (unsigned int)t41_dpc_ratio_slope(get(base, 0x31e + 22), 64));
	CHECK(get(cur, 0x31e + 2) == 55);
	for (i = 0; i < sizeof(cur); ++i) {
		int in = 0;
		unsigned int f;
		for (f = 0; f < 4; ++f) {
			unsigned int lo = 0x31e + (unsigned short[]){0,12,1,13}[f]*22;
			if (i >= lo && i < lo + 22) in = 1;
		}
		if (!in) CHECK(cur[i] == base[i]);
	}
	/* the table goes back to the base when neutral is requested again */
	CHECK(!t41_dpc_ratio_scale(cur, base, sizeof(cur), 128));
	CHECK(!memcmp(cur, base, sizeof(cur)));
	CHECK(t41_dpc_ratio_scale(cur, base, 100, 128) == -1);
	CHECK(t41_dpc_ratio_scale(cur, base, sizeof(cur), 256) == -1);
	printf(fails ? "dpc_ratio FAILED\n" : "dpc_ratio ok\n");
	return fails != 0;
}
