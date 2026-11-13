// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable t32_dpc_init (driver/t32/tx_isp_t32_dpc.h)
 * against the vendor flow tisp_dpc_init -> tisp_dpc_gain_interp (long, short)
 * -> tisp_dpc_write_reg -> tisp_dpc_write_reg_long / _short / _other, from
 * Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built
 * for the host with env.c).
 *
 * Compared bit-exact: every register write (address, value, order).  IQ
 * section 5 (2304 bytes) sits at tuning base + 22976.  "wide" is the vendor's
 * test of the format register 0x88 (field & 0x1f >= 5); the oracle drives it
 * through that register and passes the same decision to ours.
 * Inputs: random sections (random bytes, and small values) and section 5 of
 * every *.bin in T32_IQ_DIR (default ../external/ingenic-sdk/sensor-iq/t32).
 *
 * Exit 0 = all equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_dpc.h"

#define SECB	T32_DPC_SEC_BYTES
#define SEC_OFF	22976u

static u32 rs = 0x7a19c3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

static int run_case_mode(const char *what, const u8 *sec, u32 reg88, u8 mode)
{
	struct t32_front_write ow[T32_DPC_MAX_WRITES];
	u8 *base;
	int on;
	unsigned int i;

	on = t32_dpc_init(sec, (reg88 & 0x1f) >= 5, ow);
	t32o_reset();
	t32o_reg88 = reg88;
	base = t32o_alloc(SEC_OFF + SECB + 64);
	memcpy(base + SEC_OFF, sec, SECB);
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32o_decoy_arm();
	t32_data_d03a0[0] = mode;	/* 1: tisp_reg_write queues into the register shadow (logged tagged, same values) */
	t32o_call((t32o_fn)(void (*)(void))tisp_dpc_init, 0, 0, 0, 0, 0);

	compared += !mode;
	if (on < 0 || t32o_log.n != (unsigned int)on)
		goto bad;
	for (i = 0; i < t32o_log.n; i++)
		if (!t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) || t32o_log.w[i].val != ow[i].val)
			goto bad;
	if (memcmp(base + SEC_OFF, sec, SECB))	/* ours takes the section const: the vendor must leave it unchanged */
		goto bad;
	if (t32o_decoy_check())	/* the other channels' section bases must stay untouched */
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s: reg88 %x writes vendor %u ours %d\n", what, reg88, t32o_log.n, on);
		for (i = 0, compared = compared; i < t32o_log.n || (int)i < on; i++)
			if (i >= t32o_log.n || (int)i >= on || !t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) ||
			    t32o_log.w[i].val != ow[i].val) {
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
				       i < t32o_log.n ? t32o_log.w[i].reg : 0, i < t32o_log.n ? t32o_log.w[i].val : 0,
				       (int)i < on ? ow[i].reg : 0, (int)i < on ? ow[i].val : 0);
				if (i > 8)
					break;
			}
	}
	return 1;
}

static int run_case(const char *what, const u8 *sec, u32 reg88)
{
	int bad = run_case_mode(what, sec, reg88, 0);

	bad += run_case_mode(what, sec, reg88, 1);	/* d03a0 = 1: only the write addresses may differ */
	t32_data_d03a0[0] = 0;
	return bad;
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
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_DPC, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		run_case(e->d_name, bin + off, 0);
		run_case(e->d_name, bin + off, 5);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, i, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 10000;
	int nbins;

	for (c = 0; c < n; c++) {
		static u8 sec[SECB];

		for (i = 0; i < SECB; i++)
			sec[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 7);
		run_case("random", sec, rnd() % 40);
	}
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_dpc_oracle: random %u compared, IQ bins %d (%u compared), %u mismatches\n", rc_r, nbins,
	       compared - rc_r, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_dpc_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
