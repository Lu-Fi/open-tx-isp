// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable t32_gib_init (driver/t32/tx_isp_t32_gib.h)
 * against the vendor flow tisp_gib_init -> tisp_gib_interp_by_again ->
 * tisp_gib_slope_update (tisp_gib_calc_slope) -> tisp_gib_write_reg ->
 * tisp_gib_interp_by_again_s -> tisp_gib_write_blc_s_reg, from Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host
 * with env.c).
 *
 * Compared bit-exact: every register write (address, value, order) and the IQ
 * section bytes after the call (the vendor stores the interpolated black
 * levels back).  IQ section 8 (548 bytes) sits at tuning base + 26940.  The
 * Bayer word of the AddSensor init block is at +8.
 * Inputs: random sections (plus forced knot orders and flags) for every Bayer
 * index 0..31, and section 8 of every *.bin in T32_IQ_DIR (default
 * ../external/ingenic-sdk/sensor-iq/t32).  Patterns ours refuses (-1) are
 * counted, not compared.
 *
 * Exit 0 = all compared cases equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_gib.h"

#define SECB	T32_GIB_SEC_BYTES
#define SEC_OFF	26940u

static u32 rs = 0x6d2b59u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, refused, failed;

static int run_case_mode(const char *what, const u8 *sec, u32 bayer, u8 mode)
{
	struct t32_front_write ow[T32_GIB_MAX_WRITES];
	u8 osec[SECB], *base, *info;
	int on;
	unsigned int i;

	memcpy(osec, sec, SECB);
	on = t32_gib_init(osec, bayer, ow);
	if (on < 0) {
		refused += !mode;
		return 0;
	}
	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	info = t32o_alloc(64);
	memcpy(base + SEC_OFF, sec, SECB);
	put32(info + 8, bayer);
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32o_decoy_arm();
	t32_data_d03a0[0] = mode;	/* 1: tisp_reg_write queues into the register shadow (logged tagged, same values) */
	t32o_call((t32o_fn)(void (*)(void))tisp_gib_init, 0, T32O_PTR(info), 0, 0, 0);

	compared += !mode;
	if (t32o_log.n != (unsigned int)on)
		goto bad;
	for (i = 0; i < t32o_log.n; i++)
		if (!t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) || t32o_log.w[i].val != ow[i].val)
			goto bad;
	if (memcmp(base + SEC_OFF, osec, SECB))
		goto bad;
	if (t32o_decoy_check())	/* the other channels' section bases must stay untouched */
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s: bayer %u writes vendor %u ours %d\n", what, bayer, t32o_log.n, on);
		for (i = 0; i < t32o_log.n || (int)i < on; i++)
			if (i >= t32o_log.n || (int)i >= on || !t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) ||
			    t32o_log.w[i].val != ow[i].val) {
				printf("  #%u vendor %04x=%08x ours %04x=%08x\n", i,
				       i < t32o_log.n ? t32o_log.w[i].reg : 0, i < t32o_log.n ? t32o_log.w[i].val : 0,
				       (int)i < on ? ow[i].reg : 0, (int)i < on ? ow[i].val : 0);
				if (i > 6)
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

static int run_case(const char *what, const u8 *sec, u32 bayer)
{
	int bad = run_case_mode(what, sec, bayer, 0);

	bad += run_case_mode(what, sec, bayer, 1);	/* d03a0 = 1: only the write addresses may differ */
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
		u32 off, len = 0, b;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_GIB, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		for (b = 0; b < 32; b++)
			run_case(e->d_name, bin + off, b);
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
		static const u16 slx[5] = { 0x114, 0x128, 0x13c, 0x150, 0x164 };

		for (i = 0; i < SECB; i++)
			sec[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 0x3f);
		if (rnd() % 3 == 0)
			put32(sec + 4, 0);			/* gain interpolation path */
		if (rnd() % 3 == 0)
			sec[0x223] = 0;				/* store the levels back */
		if (rnd() % 2) {					/* ordered slope knots */
			unsigned int k, x = rnd() % 8;

			for (k = 0; k < 5; k++) {
				u8 *p = sec + slx[k];
				unsigned int j;

				x = rnd() % 3 ? x + rnd() % 40 : x;
				{
					unsigned int y = rnd() % 8;

					for (j = 0; j < 5; j++) {
						y += rnd() % 3 ? rnd() % 200 : 0;
						p[10 + 2 * j] = (u8)y;
						p[11 + 2 * j] = 0;
					}
					if (rnd() % 4 == 0)
						p[18] = (u8)(p[16] - 1);	/* y4 just below y3 */
				}
				for (j = 0; j < 5; j++)
					p[2 * j] = (u8)(x + j * (rnd() % 4 ? 1 + rnd() % 30 : 0)), p[2 * j + 1] = 0;
			}
		}
		run_case("random", sec, rnd() % 32);
	}
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_gib_oracle: random %u compared (%u refused by ours), IQ bins %d (%u compared), %u mismatches\n",
	       rc_r, refused, nbins, compared - rc_r, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_gib_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
