// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable t32_top_init (driver/t32/tx_isp_t32_front.h)
 * against the vendor flow tisp_top_init -> tisp_top_reg_cfg, taken from Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the
 * host with env.c).
 *
 * Compared bit-exact: every register write (address, value, order).  The
 * vendor runs for channel 0 with one pipeline (state +900 = 1), the direct
 * register path (t32_data_d03a0 = 0) and the sensor info { width, height,
 * bayer word, ..., byte 36, byte 39 = 0 (no WDR) }.  The IQ section is
 * section 0 of the bin (tuning base), 100 bytes.
 * Inputs: random sections (random enable/ctl/state bytes), every colour order
 * and depth class of t32_mbus_to_bayer, random sizes, info byte 36 0/1; and
 * section 0 of every *.bin in T32_IQ_DIR (default
 * ../external/ingenic-sdk/sensor-iq/t32).  Not covered (ours does not take
 * them): channel 1 and WDR modes.
 *
 * Exit 0 = all equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_front.h"

#define SECB	T32_TOP_BYTES

static u32 rs = 0x51d3a7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, failed;

static int run_case(const char *what, const u8 *sec, u32 w, u32 h, u32 bayer, u32 b36)
{
	struct t32_front_write ow[16];
	u8 *base, *info;
	unsigned int on, i;

	on = t32_top_init(sec, w, h, bayer, b36, ow);

	t32o_reset();
	base = t32o_alloc(SECB + 16);
	info = t32o_alloc(64);
	memcpy(base, sec, SECB);
	put32(info, w);
	put32(info + 4, h);
	put32(info + 8, bayer);
	info[36] = (u8)(b36 | (rnd() & 0xfe));	/* only bit 0 counts */
	info[39] = 0;
	t32_data_dd0a4[0] = T32O_PTR(base);
	*(u32 *)(t32_data_dc284 + 900) = 1;
	t32o_call((t32o_fn)(void (*)(void))tisp_top_init, 0, T32O_PTR(info), 0, 0, 0);

	compared++;
	if (t32o_log.n != on)
		goto bad;
	for (i = 0; i < on; i++)
		if (t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val)
			goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s: writes vendor %u ours %u, bayer %08x\n", what, t32o_log.n, on, bayer);
		for (i = 0; i < t32o_log.n || i < on; i++)
			if (i >= t32o_log.n || i >= on || t32o_log.w[i].reg != ow[i].reg ||
			    t32o_log.w[i].val != ow[i].val)
				printf("  #%u vendor %04x=%08x ours %04x=%08x\n", i,
				       i < t32o_log.n ? t32o_log.w[i].reg : 0, i < t32o_log.n ? t32o_log.w[i].val : 0,
				       i < on ? ow[i].reg : 0, i < on ? ow[i].val : 0);
	}
	return 1;
}

static u32 bayer_word(void)
{
	/* mostly the Bayer codes 0x5200..0x520b of t32_mbus_to_bayer, else any word */
	if (rnd() % 4)
		return (u32)t32_mbus_to_bayer(T32_MBUS_SBGGR8 + rnd() % 12);
	return rnd() & 0x3ffff;
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
		u32 off, len = 0;
		unsigned int k;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_IQ_SEC_TOP, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		for (k = 0; k < 12; k++)
			run_case(e->d_name, bin + off, 1920 + 64 * k, 1080 + 8 * k, (u32)t32_mbus_to_bayer(T32_MBUS_SBGGR8 + k), k & 1);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, i, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	int nbins;

	for (c = 0; c < n; c++) {
		u8 sec[SECB];

		for (i = 0; i < SECB; i++)
			sec[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 1);
		run_case("random", sec, rnd() & (c % 5 ? 0xffff : 0xffffffff), rnd() & (c % 7 ? 0xffff : 0xffffffff),
			 bayer_word(), rnd() & 1);
	}
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_top_oracle: random %u compared, IQ bins %d (%u compared), %u mismatches\n", rc_r, nbins,
	       compared - rc_r, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_top_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
