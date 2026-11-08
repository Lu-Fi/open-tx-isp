// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable t32_mdns_init (driver/t32/tx_isp_t32_mdns.h)
 * against the vendor flow tisp_mdns_init -> cmp_par_calc / cmp_reg_write, tisp_mdns_refresh -> par_refresh ->
 * func_en, all_reg_refresh -> tisp_mdns_intp, reg_cfg (equation_dif / _smp_15), start, reg_trig, from Matt Davis' mechanical MIPS-to-C recovery (ref/,
 * PR #15 @ 2e2e06bc, built for the host with env.c).
 *
 * Compared bit-exact: every register write (address, value, order) and the IQ
 * section bytes after the call.  IQ section 16 (18252 bytes) sits at tuning
 * base + 34652.  Inputs: random sections and section
 * 11 of every *.bin in T32_IQ_DIR (default
 * ../external/ingenic-sdk/sensor-iq/t32).  Sections without the block id are
 * refused by ours (counted, not compared).
 *
 * Exit 0 = all compared cases equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_mdns.h"

#define SECB	T32_MDNS_SEC_BYTES
#define SEC_OFF	34652u

static u32 rs = 0x35c7e1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, refused, failed;
static u32 badreg[8192], badbits[8192];
static unsigned int nbad_total;

static int run_case(const char *what, const u8 *sec, u32 w_, u32 h_, unsigned int cmpb)
{
	struct t32_front_write ow[T32_MDNS_MAX_WRITES];
	static u8 osec[SECB], oval[T32_MDNS_VALUES + 16];
	u8 *base, *info;
	int on;
	unsigned int i;

	memcpy(osec, sec, SECB);
	on = t32_mdns_init(osec, w_, h_, cmpb * 100, oval, ow);
	if (on < 0) {
		refused++;
		return 0;
	}
	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	info = t32o_alloc(64);
	memset(info, 0, 64);
	info[0] = (u8)w_; info[1] = (u8)(w_ >> 8); info[4] = (u8)h_; info[5] = (u8)(h_ >> 8); info[42] = (u8)cmpb;
	memcpy(base + SEC_OFF, sec, SECB);
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32o_call((t32o_fn)(void (*)(void))tisp_mdns_init, 0, T32O_PTR(info), 0, 0, 0);

	compared++;
	if (t32o_log.n != (unsigned int)on)
		goto bad;
	for (i = 0; i < t32o_log.n; i++)
		if (t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val)
			goto bad;
	if (memcmp(base + SEC_OFF, osec, SECB))
		goto bad;
	return 0;
bad:
	if (getenv("T32O_REGS")) {
		for (i = 0; i < t32o_log.n && (int)i < on; i++)
			if (t32o_log.w[i].reg == ow[i].reg && t32o_log.w[i].val != ow[i].val) {
				badreg[(t32o_log.w[i].reg >> 2) & 8191] = t32o_log.w[i].reg;
				badbits[(t32o_log.w[i].reg >> 2) & 8191] |= t32o_log.w[i].val ^ ow[i].val;
				nbad_total++;
			}
	}
	if (failed++ < 5) {
		printf("MISMATCH %s: writes vendor %u ours %d\n", what, t32o_log.n, on);
		for (i = 0; i < t32o_log.n || (int)i < on; i++)
			if (i >= t32o_log.n || (int)i >= on || t32o_log.w[i].reg != ow[i].reg ||
			    t32o_log.w[i].val != ow[i].val) {
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
				       i < t32o_log.n ? t32o_log.w[i].reg : 0, i < t32o_log.n ? t32o_log.w[i].val : 0,
				       (int)i < on ? ow[i].reg : 0, (int)i < on ? ow[i].val : 0);
				if (i > 8)
					break;
			}
		for (i = 0; i < SECB; i++)
			if (base[SEC_OFF + i] != osec[i]) {
				printf("  sec+0x%x vendor %02x ours %02x\n", i, base[SEC_OFF + i], osec[i]);
				break;
			}
	}
	return 1;
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

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_MDNS, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		{ run_case(e->d_name, bin + off, 1920, 1080, 10); run_case(e->d_name, bin + off, 2560, 1440, 12); run_case(e->d_name, bin + off, 3840, 2160, 16); }
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, i, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6000;
	int nbins;

	for (c = 0; c < n; c++) {
		static u8 sec[SECB];

		for (i = 0; i < SECB; i++)
			sec[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 7);
		run_case("random", sec, 64 + rnd() % 4000, 64 + rnd() % 3000, rnd() % 4 ? 10 : 11 + rnd() % 10);
	}
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_mdns_oracle: random %u compared (%u refused by ours), IQ bins %d (%u compared), %u mismatches\n",
	       rc_r, refused, nbins, compared - rc_r, failed);
	if (getenv("T32O_REGS")) {
		unsigned int q;

		for (q = 0; q < 8192; q++)
			if (badreg[q])
				printf("badreg %05x diffbits %08x\n", badreg[q], badbits[q]);
	}
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_mdns_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
