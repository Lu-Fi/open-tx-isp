/* Host test: T32 LSC stream-on list (driver/t32/tx_isp_t32_lsc.h).
 * Synthetic checks always; with T32_IQ_BIN (cv8001-PRJ007-day.bin) and
 * T32_RCFG_REF (the vendor's stream-on list dumped from guest RAM, one hex
 * word per line, run r20) the list built from the bin is compared with the
 * vendor's word for word (3840x2160, the 3850 K table). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../driver/t32/tx_isp_t32_lsc.h"

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static unsigned char *load(const char *path, long *len)
{
	FILE *f = path ? fopen(path, "rb") : NULL;
	unsigned char *b;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	rewind(f);
	b = malloc(*len);
	if (fread(b, 1, *len, f) != (size_t)*len) {
		free(b);
		b = NULL;
	}
	fclose(f);
	return b;
}

static void synthetic(void)
{
	static unsigned char p[T32_LSC_BLOCK_BYTES];
	unsigned char t[11] = { 200, 180, 160, 140, 120, 110, 100, 90, 80, 70, 60 };
	struct t32_lsc s;
	u32 w[3];

	/* tisp_simple_intp_uint8 */
	CHECK(t32_intp_u8(0, 0, t) == 200 && t32_intp_u8(0, 0x8000, t) == 190);
	CHECK(t32_intp_u8(3, 0x4000, t) == 135 && t32_intp_u8(12, 0, t) == 60);
	/* geometry of a 3840x2160 sensor with 64x64 cells, centre offsets 0 */
	memset(p, 0, sizeof(p));
	p[66] = p[67] = 64;
	p[114] = 4;
	p[115] = 8;
	CHECK(t32_lsc_geometry(p, &s, 3840, 2160) == 0);
	CHECK(s.rem_y == 40 && s.rows == 18 && s.rem_x == 32 && s.cols == 31 && s.cells == 1116);
	CHECK(s.inv_main == 256 && s.inv_wdr == 1023 && s.zone == 4);
	p[66] = p[67] = 16;	/* 16x16 cells: too many cells */
	CHECK(t32_lsc_geometry(p, &s, 3840, 2160) == -1);
	/* CT zones: breakpoints 2850 3650 4050 6200 */
	p[0x18] = 2850 & 255; p[0x19] = 2850 >> 8; p[0x1a] = 3650 & 255; p[0x1b] = 3650 >> 8;
	p[0x1c] = 4050 & 255; p[0x1d] = 4050 >> 8; p[0x1e] = 6200 & 255; p[0x1f] = 6200 >> 8;
	t32_lsc_ct(p, &s, 2000);
	CHECK(s.table_lo == 0 && s.table_hi == 0);
	t32_lsc_ct(p, &s, 3850);
	CHECK(s.table_lo == 1 && s.table_hi == 1 && s.fraction == 0);
	t32_lsc_ct(p, &s, 5000);
	CHECK(s.table_lo == 1 && s.table_hi == 2 && s.fraction == ((5000 - 4050) << 12) / (6200 - 4050));
	t32_lsc_ct(p, &s, 7000);
	CHECK(s.table_lo == 2 && s.table_hi == 2);
	/* cell packing: 6 x 12 bits into 3 words */
	t32_lsc_ct(p, &s, 3850);
	{
		unsigned char *pl = p + T32_LSC_TABLES + T32_LSC_TABLE_STRIDE + 2;
		u32 v[6] = { 0x123, 0x456, 0x789, 0xabc, 0xdef, 0xfed }, k;

		for (k = 0; k < 6; k++) {
			pl[(k / 2) * T32_LSC_PLANE + (k & 1) * 2] = v[k] & 255;
			pl[(k / 2) * T32_LSC_PLANE + (k & 1) * 2 + 1] = v[k] >> 8;
		}
	}
	t32_lsc_cell(p, &s, 0, w);
	CHECK(w[0] == (0x123 | 0x456 << 12 | 0x89u << 24) && w[1] == (0x7 | 0xabc << 4 | 0xdef << 16 | 0xdu << 28) &&
	      w[2] == 0xfe);
}

int main(void)
{
	long blen = 0, rlen = 0;
	unsigned char *bin = load(getenv("T32_IQ_BIN"), &blen), *ref = load(getenv("T32_RCFG_REF"), &rlen);

	synthetic();
	if (bin && ref) {
		static u32 words[4096], scratch[T32_LSC_WORDS], want[4096];
		const u8 *p = t32_lsc_block(bin, blen);
		struct t32_rcfg_list l;
		struct t32_lsc s;
		u32 n = 0, i, diff = 0;
		char *tok = strtok((char *)ref, " \n");

		while (tok && n < 4096) {
			want[n++] = strtoul(tok, NULL, 16);
			tok = strtok(NULL, " \n");
		}
		CHECK(p != NULL);
		if (p) {
			CHECK(t32_lsc_setup(p, &s, 3840, 2160, 3850) == 0);
			t32_rcfg_init(&l, words, 4096);
			CHECK(t32_lsc_fill_list(p, &s, &l, scratch) == 0);
			/* the vendor list carries two empty commands after its end */
			for (i = 0; i <= words[0] && i < n; i++)
				diff += words[i] != want[i];
			CHECK(words[0] + 1 <= n && diff == 0);
			printf("  list: %u words built, %u words in the vendor's, %u differ\n", words[0], want[0], diff);
		}
	} else {
		printf("  (no T32_IQ_BIN / T32_RCFG_REF: vendor comparison skipped)\n");
	}
	printf("tx_isp_t32_lsc_test: %s\n", bad ? "FAIL" : "PASS");
	return bad != 0;
}
