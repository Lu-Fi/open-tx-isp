// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the IQ-bin walk of our driver (driver/t32/tx_isp_t32_iq.h,
 * t32_iq_section + t32_iq_payload) against the vendor's tisp_bin_data_parse
 * from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc,
 * built for the host with env.c).
 *
 * Ours only locates sections, the vendor copies them.  So the proof is made on
 * the vendor's memory: for every one of the 28 sections, if ours finds it at
 * offset o, the vendor's tuning block at its fixed destination holds exactly
 * the bytes bin[o .. o + copy size] (destination and copy size per section are
 * the constants of the recovered code, listed below); if ours says "absent",
 * the vendor's destination is still zero.  For the five payload sections
 * (7 LSC two tables, 12 CCM, 13 gamma, 19, 20 two tables) the vendor's LUT
 * info (size u16 and buffer pointer) must hold the table at the position ours
 * computes and the sum of the sizes must equal t32_iq_payload.
 *
 * Inputs: generated bins (valid "1.00" tag, project name PRJ007, checksum over
 * the data words as the vendor checks it) with random enables, lengths equal to
 * or a little different from the copy size, random payload-size bytes, and
 * every *.bin of T32_IQ_DIR (default ../external/ingenic-sdk/sensor-iq/t32).
 *
 * Exit 0 = all equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"

/* per section: destination in the tuning block and bytes copied (recovered code, L_26xxx..L_27xxx) */
static const struct { u32 dst, size; } sec_tab[T32_IQ_SECTIONS] = {
	{ 0, 100 }, { 104, 3288 }, { 3392, 6740 }, { 10132, 1840 }, { 11976, 11000 }, { 22976, 2304 },
	{ 25280, 1540 }, { 26820, 120 }, { 26940, 548 }, { 27488, 104 }, { 27592, 20 }, { 27612, 4708 },
	{ 32320, 872 }, { 33192, 92 }, { 33284, 656 }, { 33940, 712 }, { 34652, 18252 }, { 52904, 248 },
	{ 53152, 436 }, { 53588, 1204 }, { 54792, 40 }, { 54832, 3928 }, { 58760, 628 }, { 59388, 1076 },
	{ 60464, 800 }, { 61264, 188 }, { 61452, 100 }, { 61552, 456 } };

/* payload tables: section, size field offset and buffer pointer offset in the LUT info */
static const struct { unsigned int sec, size_off, ptr_off; } pay_tab[] = {
	{ 7, 0, 16 }, { 7, 2, 20 }, { 12, 6, 24 }, { 13, 4, 32 }, { 19, 8, 28 }, { 20, 10, 36 }, { 20, 12, 40 } };

static u32 rs = 0x2c61e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static u32 get32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static const u32 crc_tab[8] = { 0, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3 };

static unsigned int compared, failed;
#define BADF(...) do { if (failed++ < 5) { printf("MISMATCH %s: ", what); printf(__VA_ARGS__); printf("\n"); } return 1; } while (0)

static int run_case(const char *what, const u8 *bin, u32 bytes)
{
	u8 *vb, *tp, *info;
	u32 i, ret, off[T32_IQ_SECTIONS], len[T32_IQ_SECTIONS];

	t32o_reset();
	vb = t32o_alloc(bytes + 16);
	memcpy(vb, bin, bytes);
	ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_bin_data_parse, 0, 0x20000, T32O_PTR(vb), 0, 0);
	compared++;
	if (ret)
		BADF("vendor returned %d", (int)ret);
	tp = T32O_HOST(t32_data_dd0a4[0]);
	info = T32O_HOST(t32_data_dc274[0]);
	if (!tp || !info)
		BADF("no tuning block");

	for (i = 0; i < T32_IQ_SECTIONS; i++) {
		len[i] = 0;
		off[i] = t32_iq_section(bin, bytes, i, &len[i]);
		if (off[i]) {
			if (memcmp(tp + sec_tab[i].dst, bin + off[i], sec_tab[i].size))
				BADF("section %u at %x differs in the vendor's tuning block", i, off[i]);
		} else {
			u32 k;

			for (k = 0; k < sec_tab[i].size; k++)
				if (tp[sec_tab[i].dst + k])
					BADF("section %u absent for ours but the vendor copied it", i);
		}
	}
	/* payloads */
	for (i = 0; i < sizeof(pay_tab) / sizeof(pay_tab[0]); i++) {
		u32 s = pay_tab[i].sec, size, sum = 0, pos, ptr, k;

		if (!off[s])
			continue;
		/* position of this table: behind the section, after the earlier tables of the same section */
		pos = off[s] + len[s];
		for (k = 0; k < i; k++)
			if (pay_tab[k].sec == s)
				pos += info[pay_tab[k].size_off] | info[pay_tab[k].size_off + 1] << 8;
		size = info[pay_tab[i].size_off] | info[pay_tab[i].size_off + 1] << 8;
		ptr = get32(info + pay_tab[i].ptr_off);
		if (pos + size > bytes || !ptr || memcmp(T32O_HOST(ptr), bin + pos, size))
			BADF("payload %u of section %u (size %u) differs: pos %u bytes %u off %u len %u ptr %x sizes %u", i, s, size, pos, bytes, off[s], len[s], ptr, (unsigned)(info[0]|info[1]<<8));
		for (k = 0; k < sizeof(pay_tab) / sizeof(pay_tab[0]); k++)
			if (pay_tab[k].sec == s)
				sum += info[pay_tab[k].size_off] | info[pay_tab[k].size_off + 1] << 8;
		if (t32_iq_payload(bin + off[s], bytes - off[s], s) != sum)
			BADF("payload sum of section %u: ours %u vendor %u", s, t32_iq_payload(bin + off[s], bytes - off[s], s), sum);
	}
	return 0;
}

/* payload-size byte: 1..max, or 1..255 in every ninth case */
static u8 nz(unsigned int c, u32 max)
{
	return (u8)(1 + rnd() % (c % 9 == 0 ? 255 : max));
}

static int gen_case(unsigned int c)
{
	static u8 bin[1u << 20];
	u8 *d = bin + T32_IQ_DATA;
	u32 i, crc = 0, w, n;

	memset(bin, 0, sizeof(bin));
	memcpy(bin, "1.00", 4);
	memcpy(bin + 0x98, "PRJ007", 6);
	for (i = 0; i < T32_IQ_SECTIONS; i++) {
		u32 l = sec_tab[i].size, k;
		u8 *sec;
		int en = rnd() % 4 != 0;

		if (c % 11 == 0)
			en = 1;
		if (!en) {
			put32(bin + T32_IQ_TABLE + 4 * i, 0 | (rnd() % 3 ? 0 : 2) | (l << 16));
			continue;
		}
		if (rnd() % 5 == 0)
			l += rnd() % 17;	/* section longer than what the vendor copies */
		put32(bin + T32_IQ_TABLE + 4 * i, 1 | l << 16);
		sec = d;
		for (k = 0; k < l; k++)
			sec[k] = (u8)rnd();
		/* payload-size bytes: small mostly, nonzero (a zero size is an error path of the vendor) */
		switch (i) {
		case 7: sec[0x74] = nz(c, 4); sec[0x75] = nz(c, 4); break;
		case 12: sec[0x364] = nz(c, 12); break;
		case 13: sec[0x5a] = nz(c, 4);
			 if (rnd() & 1)
				 put32(sec, 0x134ff91);
			 break;
		case 19: sec[0x4af] = nz(c, 10); break;
		case 20: sec[0x23] = nz(c, 4); break;
		}
		d += l;
		if (i == 7 || i == 12 || i == 13 || i == 19 || i == 20) {
			u32 p = t32_iq_payload(sec, l, i);	/* sizes the vendor derives from the same bytes */

			for (k = 0; k < p; k++)
				d[k] = (u8)rnd();
			d += p;
		}
	}
	n = (u32)(d - (bin + T32_IQ_DATA));
	put32(bin + 8, n);
	for (i = 0; i < (n >> 2); i++) {
		w = get32(bin + T32_IQ_DATA + 4 * i);
		crc ^= w;
		crc ^= crc_tab[crc & 7];
	}
	put32(bin + 0x10, crc);
	return run_case("generated", bin, T32_IQ_DATA + n + 0x8000);
}

static int bin_cases(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int nbins = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char path[1024];
		static u8 bin[1u << 20];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		memset(bin, 0, sizeof(bin));
		bytes = fread(bin, 1, sizeof(bin) - 0x10000, f);
		fclose(f);
		nbins++;
		run_case(e->d_name, bin, (u32)bytes);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000, rc_r;
	int nbins;

	for (c = 0; c < n; c++)
		gen_case(c);
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_iq_oracle: generated %u compared, IQ bins %d (%u compared), %u mismatches\n", rc_r, nbins,
	       compared - rc_r, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_iq_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
