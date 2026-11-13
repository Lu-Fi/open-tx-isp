// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle for the lens shading (LSC): the vendor tisp_lsc_init chain (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc, built for the host with env.c) against our
 * driver/t32/tx_isp_t32_lsc.h.
 *
 * The vendor's LSC output is a shadow image (tisp_single_lsc_reg_write /
 * _reg_lut_write: index -> value) that the register-list code turns into the stream-on
 * list.  Image indices: 2 = enable, 4..16 = the 13 control registers (0x5040..0x5070),
 * 18 = port select 0x101, 20 + w = mesh word w.  Compared: enable, the 13 registers, the
 * select value, the cell count (vendor: p[65] ? (IR sensor ? 1152 : 576) : rows * cols * 2)
 * and every mesh word of those cells.  IQ section 7 sits at tuning base + 26820 (120 bytes)
 * with its two payload tables behind it (lutinfo + 16 / + 20); ours reads the same bytes
 * contiguously at p.
 *
 * Inputs: random sections (tags, hysteresis incl. exact band edges, radial thresholds and gains,
 * both formats), and section 7 + payload of every *.bin in T32_IQ_DIR at three sensor sizes and
 * for plain and IR Bayer indices.  The mesh words are compared too (T32O_DIAG=1 lists differences).
 *
 * Exit 0 = all equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_lsc.h"

static u32 rs = 0x6a31d9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, refused, failed, word_diffs, cases_with_word_diffs;

/* the shadow image against ours: enable, registers, select, and the mesh words of ocount cells */
static int compare_image(const char *what, const u32 *sh, const u8 *blk, const struct t32_lsc *s,
			 const struct t32_lsc_work *wk, u32 ocount, const char *step)
{
	u32 i, bad = 0;

	if (sh[2] != 1 || sh[18] != 0x101) {
		if (failed++ < 5)
			printf("MISMATCH %s %s: enable %x select %x\n", what, step, sh[2], sh[18]);
		return 1;
	}
	for (i = 0; i < 13; i++)
		if (sh[4 + i] != s->regs[i]) {
			if (failed++ < 5)
				printf("MISMATCH %s %s: ctrl reg %u vendor %08x ours %08x\n", what, step, i, sh[4 + i], s->regs[i]);
			return 1;
		}
	for (i = 0; i < ocount; i++) {
		u32 ow[3];

		t32_lsc_cell(blk, wk, i, ow);
		if (ow[0] != sh[20 + 3 * i] || ow[1] != sh[21 + 3 * i] || ow[2] != sh[22 + 3 * i])
			bad++;
	}
	if (ocount < 1152 && (sh[20 + 3 * ocount] || sh[21 + 3 * ocount] || sh[22 + 3 * ocount])) {
		if (failed++ < 5)
			printf("MISMATCH %s %s: cell count %u, vendor wrote more\n", what, step, ocount);
		return 1;
	}
	if (bad) {
		word_diffs += bad;
		cases_with_word_diffs++;
		if (failed++ < 5)
			printf("MISMATCH %s %s: %u of %u cells differ in the mesh words (p65 %u n %u tables %u/%u none %u w %u)\n", what,
			       step, bad, ocount, blk[65], blk[116], s->table_lo, s->table_hi, s->none, s->fraction);
		return 1;
	}
	return 0;
}

/*
 * blk: section (120 bytes) + payload 1 + payload 2, contiguous; n1/n2: payload sizes.
 * Init at 5000 K, then per-frame updates (tisp_lsc_ct_update, not forced) with the colour
 * temperatures of cts[]; the image is compared after each step.
 */
static int run_case(const char *what, const u8 *blk, u32 n1, u32 n2, u32 w_, u32 h_, u32 bayer, const u32 *cts, unsigned int nct)
{
	static struct t32_lsc_work wk;
	struct t32_lsc s;
	u8 *base, *info, *linfo, *v1, *v2, *regsh;
	u32 *sh, ocount;
	unsigned int c;

	if (t32_lsc_geometry(blk, &s, w_, h_)) {
		refused++;
		return 0;
	}
	t32o_reset();
	base = t32o_alloc(0x10000);
	info = t32o_alloc(64);
	linfo = t32o_alloc(64);
	v1 = t32o_alloc(n1 + 8);
	v2 = t32o_alloc(n2 + 8);
	regsh = t32o_alloc(0x10000);
	{
		u8 *dbase = t32o_alloc(0x400);

		*(u32 *)(dbase + 204) = T32O_PTR(regsh);
		t32_data_da844[0] = T32O_PTR(dbase);
	}
	memcpy(base + 26820, blk, 120);
	memcpy(v1, blk + 120, n1);
	memcpy(v2, blk + 120 + n1, n2);
	put32(linfo + 16, T32O_PTR(v1));
	put32(linfo + 20, T32O_PTR(v2));
	put32(info, w_);
	put32(info + 4, h_);
	put32(info + 8, bayer);
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_dc274[0] = T32O_PTR(linfo);
	t32o_call((t32o_fn)(void (*)(void))tisp_lsc_init, 0, T32O_PTR(info), 0, 0, 0);
	sh = (u32 *)regsh;

	t32_lsc_gain(blk, &s, 0);
	memset(&wk, 0, sizeof(wk));
	t32_lsc_update(blk, &s, &wk, 5000, 1);
	t32_lsc_registers(blk, &s, 0);
	ocount = t32_lsc_cell_count(blk, &s, bayer);
	compared++;
	if (compare_image(what, sh, blk, &s, &wk, ocount, "init"))
		return 1;
	for (c = 0; c < nct; c++) {
		char step[32];

		/* the vendor reads the section copy it has in the tuning block (its hysteresis field may have been lowered) */
		t32o_call((t32o_fn)(void (*)(void))tisp_lsc_ct_update, 0, 0, cts[c], 0, 0);
		t32_lsc_update(blk, &s, &wk, cts[c], 0);
		snprintf(step, sizeof(step), "update %u ct %u", c, cts[c]);
		compared++;
		if (compare_image(what, sh, blk, &s, &wk, ocount, step))
			return 1;
	}
	/* gain updates (tisp_lsc_tgain_update): the strength registers follow the total gain */
	{
		static const u32 gains[] = { 0, 0x8000, 0x10000, 0x2c000, 0x50000, 0xa0000, 0xb0001, 0x120000, 0xffffffff };

		for (c = 0; c < sizeof(gains) / sizeof(gains[0]); c++) {
			char step[32];

			t32o_call((t32o_fn)(void (*)(void))tisp_lsc_tgain_update, 0, 0, gains[c], 0, 0);
			t32_lsc_gain(blk, &s, gains[c]);
			t32_lsc_registers(blk, &s, 0);
			snprintf(step, sizeof(step), "gain %#x", gains[c]);
			compared++;
			if (compare_image(what, sh, blk, &s, &wk, ocount, step))
				return 1;
		}
	}
	return 0;
}

static const u32 bin_cts[12] = { 2000, 2860, 3400, 3849, 3851, 4400, 5000, 5005, 5600, 6190, 6300, 9000 };

static int bin_cases(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int nbins = 0;
	static const u32 sz[3][2] = { { 3840, 2160 }, { 1920, 1080 }, { 2560, 1440 } };

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char path[1024];
		static u8 b[1u << 20], blk[1u << 17];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);
		u32 off, len = 0, n1, n2, k, bay;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(b, 1, sizeof(b), f);
		fclose(f);
		off = t32_iq_section(b, (u32)bytes, T32_IQ_SEC_LSC, &len);
		if (!off || len < 120)
			continue;
		n1 = (b[off + 0x74] * 0xd88U) & 0xffff;
		n2 = (b[off + 0x75] * 0x3602U) & 0xffff;
		if (120 + n1 + n2 > sizeof(blk) || n2 < 3 * T32_LSC_TABLE_STRIDE || n1 != T32_LSC_TABLES - 120)
			continue;
		memcpy(blk, b + off, 120 + n1 + n2);
		nbins++;
		for (k = 0; k < 3; k++)
			for (bay = 0; bay < 2; bay++)
				run_case(e->d_name, blk, n1, n2, sz[k][0], sz[k][1], bay ? 4 : 0, bin_cts, 12);
	}
	closedir(d);
	return nbins;
}

static void random_cases(unsigned int n)
{
	static u8 blk[120 + 3 * 3464 + 3 * 13826 + 16];
	unsigned int c, i;
	static const u32 sz[4][2] = { { 1920, 1080 }, { 1280, 720 }, { 2560, 1440 }, { 640, 480 } };

	for (c = 0; c < n; c++) {
		for (i = 0; i < 120; i++)
			blk[i] = (u8)(c % 3 ? rnd() : rnd() & 7);
		put32(blk, T32_LSC_ID);
		blk[0x74] = 3;
		blk[0x75] = 3;
		blk[65] = (u8)(rnd() & 1);
		blk[66] = (u8)(32 + rnd() % 96);
		blk[67] = (u8)(32 + rnd() % 96);
		/* offsets of the optical centre: small signed values */
		put32(blk + 16, (u32)((int)(rnd() % 4000) - 2000));
		put32(blk + 20, (u32)((int)(rnd() % 4000) - 2000));
		blk[4] = blk[5] = blk[6] = blk[7] = (u8)(rnd() % 5 ? 0 : rnd());
		for (i = 120; i < 120 + 3 * 3464 + 3 * 13826; i++)
			blk[i] = (u8)rnd();
		blk[116] = (u8)(1 + rnd() % 3);		/* number of tables */
		put32(blk + 12, rnd() % 3 ? rnd() % 300 : rnd() % 3000);	/* update threshold in K */
		put32(blk + 48, rnd() % 3 ? rnd() % 400 : 0);	/* hysteresis (u16, next bytes cleared below) */
		blk[50] = blk[51] = 0;
		{
			u32 tg = 1500 + rnd() % 3000;
			unsigned int k;

			for (k = 0; k < 3; k++) {
				blk[T32_LSC_TABLES + k * T32_LSC_TABLE_STRIDE] = (u8)tg;
				blk[T32_LSC_TABLES + k * T32_LSC_TABLE_STRIDE + 1] = (u8)(tg >> 8);
				tg += rnd() % 4 ? 300 + rnd() % 2500 : rnd() % 50;
			}
			if (rnd() % 5 == 0) {		/* unordered tags: no pair applies */
				u32 tr = 1000 + rnd() % 7000;
				unsigned int k3 = rnd() % 3;

				blk[T32_LSC_TABLES + k3 * T32_LSC_TABLE_STRIDE] = (u8)tr;
				blk[T32_LSC_TABLES + k3 * T32_LSC_TABLE_STRIDE + 1] = (u8)(tr >> 8);
			}
			/* put a tag exactly on a band edge of ct = 5000: tag +- hysteresis, +- 1 */
			if (rnd() % 3 == 0) {
				unsigned int k2 = rnd() % blk[116];
				int hy = (int)(blk[48] | blk[49] << 8), e = (int)(rnd() % 3) - 1, sgn = rnd() & 1 ? 1 : -1;
				u32 t = (u32)(5000 + sgn * hy + e);

				blk[T32_LSC_TABLES + k2 * T32_LSC_TABLE_STRIDE] = (u8)t;
				blk[T32_LSC_TABLES + k2 * T32_LSC_TABLE_STRIDE + 1] = (u8)(t >> 8);
			}
		}
		{
			u32 th = 0;
			unsigned int k;

			for (k = 0; k < 8; k++) {		/* radial thresholds, ascending, up to 4096 (r^2 <= 2^24) */
				th += rnd() % 4 ? 200 + rnd() % 600 : rnd() % 20;
				blk[32 + 2 * k] = (u8)th;
				blk[33 + 2 * k] = (u8)(th >> 8);
			}
		}
		for (i = 0; i < 24; i++)
			blk[77 + i] = (u8)(rnd() % 5 ? rnd() % 256 : 255);
		{
			u32 zi = rnd() % 4, cts[8], k;

			for (k = 0; k < 8; k++)
				cts[k] = rnd() % 4 ? 1000 + rnd() % 7000 : 5000 + rnd() % 600 - 300;
			run_case("random", blk, 3 * 3464, 3 * 13826, sz[zi][0], sz[zi][1], rnd() % 8, cts, 8);
		}
	}
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	int nbins;

	random_cases(getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 1500);
	{
		unsigned int rc_r = compared;

		nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
		printf("t32_lsc_oracle: random %u compared; ", rc_r);
	}
	printf("IQ bins %d, %u compared in total (%u refused by ours), %u mismatches; mesh words differ in %u cases (%u cells)\n",
	       nbins, compared, refused, failed, cases_with_word_diffs, word_diffs);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_lsc_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
