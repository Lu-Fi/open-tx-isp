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
	p[116] = 3;		/* three tables, hysteresis p[48] = 0 */
	/* the three mesh tables carry their colour temperature (2850, 3850, 6200 K) as a u16 tag */
	p[T32_LSC_TABLES + 0 * T32_LSC_TABLE_STRIDE] = 2850 & 255; p[T32_LSC_TABLES + 0 * T32_LSC_TABLE_STRIDE + 1] = 2850 >> 8;
	p[T32_LSC_TABLES + 1 * T32_LSC_TABLE_STRIDE] = 3850 & 255; p[T32_LSC_TABLES + 1 * T32_LSC_TABLE_STRIDE + 1] = 3850 >> 8;
	p[T32_LSC_TABLES + 2 * T32_LSC_TABLE_STRIDE] = 6200 & 255; p[T32_LSC_TABLES + 2 * T32_LSC_TABLE_STRIDE + 1] = 6200 >> 8;
	t32_lsc_ct(p, &s, 2000);
	CHECK(s.table_lo == 0 && s.table_hi == 0);
	t32_lsc_ct(p, &s, 3850);
	CHECK(s.table_lo == 1 && s.table_hi == 1 && s.fraction == 0);
	p[48] = 20;		/* hysteresis 20 K (below a quarter of the smallest tag distance): 3850 +- 20 sticks to table 1 */
	t32_lsc_ct(p, &s, 3860);
	CHECK(s.table_lo == 1 && s.table_hi == 1 && !s.none);
	t32_lsc_ct(p, &s, 5000);
	CHECK(s.table_lo == 1 && s.table_hi == 2 && s.fraction == ((5000 - 3870) << 12) / ((6200 - 20) - (3850 + 20)));
	p[48] = 0;
	t32_lsc_ct(p, &s, 5000);
	CHECK(s.table_lo == 1 && s.table_hi == 2 && s.fraction == ((5000 - 3850) << 12) / (6200 - 3850));
	t32_lsc_ct(p, &s, 7000);
	CHECK(s.table_lo == 2 && s.table_hi == 2);
	/* cell packing: 6 x 12 bits into 3 words (a single table: below the first tag) */
	memset(&s, 0, sizeof(s));		/* no grid: no radial correction */
	t32_lsc_ct(p, &s, 2000);
	{
		unsigned char *pl = p + T32_LSC_TABLES + 2;
		u32 v[6] = { 0x123, 0x456, 0x789, 0xabc, 0xdef, 0xfed }, k;

		for (k = 0; k < 6; k++) {
			pl[(k / 2) * T32_LSC_PLANE + (k & 1) * 2] = v[k] & 255;
			pl[(k / 2) * T32_LSC_PLANE + (k & 1) * 2 + 1] = v[k] >> 8;
		}
	}
	{
		static struct t32_lsc_work wk;
		unsigned int pl, e;

		memset(&wk, 0, sizeof(wk));
		for (pl = 0; pl < 3; pl++)
			for (e = 0; e < 2; e++)
				wk.pl[pl][e] = (u16)t32_lsc_raw_elem(p, &s, pl, e);
		t32_lsc_cell(p, &wk, 0, w);
	}
	CHECK(w[0] == (0x123 | 0x456 << 12 | 0x89u << 24) && w[1] == (0x7 | 0xabc << 4 | 0xdef << 16 | 0xdu << 28) &&
	      w[2] == 0xfe);
}

/*
 * openimp #728 (beyond vendor): the sensor orientation sequence 1 (mirror), 2 (flip), 3 (both), 0 through
 * t32_lsc_mf_delta leaves the tables exactly as one exchange of the current orientation from the original (the
 * vendor's in-place toggles would leave 1 -> 2 mirrored and flipped), and the ring centre follows mirror / flip
 * (ox' = -ox - (w - 1), oy' = -oy - (h - 1)) and returns to the bin's value.
 */
static void mf_check(void)
{
	static u8 tab[T32_LSC_TABLE_STRIDE * 3], org[sizeof(tab)], ref[sizeof(tab)], sec[120];
	static const u32 seq[][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 0 }, { 1, 0 }, { 0, 1 } };
	u8 st[12] = { 0 }, mf = 0;
	u32 i, df, dm, rows = 6, cols = 8, x, y;

	for (i = 0; i < sizeof(tab); i++)
		tab[i] = (u8)(i * 7 + (i >> 8));
	memcpy(org, tab, sizeof(tab));
	sec[16] = 0x31; sec[17] = 0xfd; sec[18] = 0xff; sec[19] = 0xff;	/* oy -719 */
	sec[20] = 0x01; sec[21] = 0xfb; sec[22] = 0xff; sec[23] = 0xff;	/* ox -1279 */
	for (i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
		t32_lsc_mf_delta(&mf, seq[i][0], seq[i][1], &df, &dm);
		if (df)
			t32_lsc_mesh_flip(tab, rows, cols, 3, 1, 0, st, &t32_lsc_xops_default);
		if (dm)
			t32_lsc_mesh_flip(tab, rows, cols, 3, 0, 1, st, &t32_lsc_xops_default);
		t32_lsc_ring_centre(sec, 2560, 1440, df, dm);
		memcpy(ref, org, sizeof(ref));
		if (seq[i][0])
			t32_lsc_mesh_flip(ref, rows, cols, 3, 1, 0, st, &t32_lsc_xops_default);
		if (seq[i][1])
			t32_lsc_mesh_flip(ref, rows, cols, 3, 0, 1, st, &t32_lsc_xops_default);
		CHECK(memcmp(tab, ref, sizeof(tab)) == 0);
		CHECK(mf == (seq[i][0] | seq[i][1] << 1));
		y = (u32)-(int)t32_le32(sec + 16);
		x = (u32)-(int)t32_le32(sec + 20);
		CHECK(y == (seq[i][0] ? 1439u - 719u : 719u));
		CHECK(x == (seq[i][1] ? 2559u - 1279u : 1279u));
	}
	/* the same flip twice: nothing to exchange the second time */
	t32_lsc_mf_delta(&mf, 1, 0, &df, &dm);
	t32_lsc_mf_delta(&mf, 1, 0, &df, &dm);
	CHECK(df == 0 && dm == 0);
	printf("  #728 orientation delta / ring centre: %s\n", bad ? "FAIL" : "ok");
}

int main(void)
{
	long blen = 0, rlen = 0;
	unsigned char *bin = load(getenv("T32_IQ_BIN"), &blen), *ref = load(getenv("T32_RCFG_REF"), &rlen);

	synthetic();
	mf_check();
	if (bin && ref) {
		static u32 words[4096], scratch[T32_LSC_WORDS], want[4096];
		const u8 *p = t32_lsc_block(bin, blen);
		struct t32_rcfg_list l;
		struct t32_lsc s;
		static struct t32_lsc_work wk;
		u32 n = 0, i, diff = 0;
		char *tok = strtok((char *)ref, " \n");

		while (tok && n < 4096) {
			want[n++] = strtoul(tok, NULL, 16);
			tok = strtok(NULL, " \n");
		}
		CHECK(p != NULL);
		if (p) {
			CHECK(t32_lsc_setup(p, &s, &wk, 3840, 2160, 3850) == 0);
			t32_rcfg_init(&l, words, 4096);
			CHECK(t32_lsc_fill_list(p, &s, &wk, &l, scratch, 4) == 0);
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
