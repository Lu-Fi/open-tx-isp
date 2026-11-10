// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable t32_gamma_init (driver/t32/tx_isp_t32_gamma.h)
 * against the vendor flow tisp_gamma_init -> tisp_gamma_interp_by_ev ->
 * tisp_gamma_update_curve -> tisp_gamma_strength_transform ->
 * tisp_gamma_addr_write_lut_rgb / tisp_gamma_write_lut_rgb (tisp_reg_lut_write),
 * from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc,
 * built for the host with env.c).
 *
 * Compared bit-exact: every register write (address, value, order).  IQ
 * section 13 (92 bytes) sits at tuning base + 0x81a8, its look-up table payload
 * (one 258-byte curve) at lutinfo + 32.  Inputs: random sections (strength
 * mode 0/1/other, EV knots incl. zero first knot and equal knots, random
 * curves) and every *.bin of T32_IQ_DIR (default
 * ../external/ingenic-sdk/sensor-iq/t32).  The multi-curve form (block id from
 * 0x134ff91 on) and unusable strengths are refused by ours (counted, not
 * compared).
 *
 * Exit 0 = all compared cases equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_gamma.h"
#include "t32o_shadow.h"

#define SECB	T32_GAMMA_SEC_BYTES
#define SEC_OFF	0x81a8u

static u32 rs = 0x1f3a77u;
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

static int run_case_mode(const char *what, const u8 *sec, const u8 *lut, u32 lutbytes, u8 mode)
{
	static struct t32_front_write ow[T32_GAMMA_MAX_WRITES];
	u8 *base, *info, *vlut;
	u32 *shimg = 0;
	int on;
	unsigned int i;

	on = t32_gamma_init(sec, lut, lutbytes, ow);
	if (on < 0) {
		refused += !mode;
		/* ours refuses only a payload too short for its form or a curve count of 0 */
		if (t32_front_le32(sec) < 0x134ff91U && sec[0x5a] >= 1 && lutbytes >= T32_GAMMA_CURVE_BYTES) {
			if (failed++ < 5)
				printf("MISMATCH %s: ours refused a single-curve section\n", what);
			return 1;
		}
		return 0;
	}
	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	info = t32o_alloc(64);
	vlut = t32o_alloc(lutbytes + 8);
	memcpy(base + SEC_OFF, sec, SECB);
	memcpy(vlut, lut, lutbytes);
	put32(info + 32, T32O_PTR(vlut));
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_dc274[0] = T32O_PTR(info);
	t32_data_e1820[0] = 258;	/* payload bytes of the single curve form */
	t32_data_d03a0[0] = mode;	/* 1: tisp_reg_write queues into the register shadow (logged tagged, same values) */
	if (mode) {	/* the LUT goes into the shadow image of channel 0: da844[0] -> block, block + 200 -> image */
		shimg = t32o_shadow_arm(T32_GAMMA_IMG_WORDS);
	}
	t32o_call((t32o_fn)(void (*)(void))tisp_gamma_init, 0, 0, 0, 0, 0);

	compared += !mode;
	if (mode) {
		/* d03a0 = 1: the 128 LUT pairs of each port become image words (ctl 2038 / 2171 / 2304, pair i at ctl + 2 + i,
		 * end word slot ctl + 131); everything else stays a tagged write (see t32o_shadow.h) */
		static const u32 ports[3][2] = { { 0x32040, 0x32044 }, { 0x32080, 0x32084 }, { 0x32060, 0x32064 } };
		static const u32 ctl[3] = { 2038, 2171, 2304 };

		if (t32o_lut_shadow_check(ow, on, 3, ports, ctl, 131, shimg, T32_GAMMA_IMG_WORDS, NULL))
			goto bad;
		return 0;
	}
	if (t32o_log.n != (unsigned int)on)
		goto bad;
	for (i = 0; i < t32o_log.n; i++)
		if (!t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) || t32o_log.w[i].val != ow[i].val)
			goto bad;
	{
		u8 e[SECB];

		memcpy(e, sec, SECB);
		e[86] = e[87] = 0;	/* tisp_gamma_init clears the API switches +0x56/+0x57 */
		if (memcmp(base + SEC_OFF, e, SECB))
			goto bad;
	}
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s: writes vendor %u ours %d\n", what, t32o_log.n, on);
		for (i = 0; i < t32o_log.n || (int)i < on; i++)
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

static int run_case(const char *what, const u8 *sec, const u8 *lut, u32 lutbytes)
{
	int bad = run_case_mode(what, sec, lut, lutbytes, 0);

	bad += run_case_mode(what, sec, lut, lutbytes, 1);	/* d03a0 = 1: only the write addresses may differ */
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
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_GAMMA, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		run_case(e->d_name, bin + off, bin + off + len, (u32)bytes - off - len);
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
		u8 sec[SECB], lut[T32_GAMMA_CURVE_BYTES + 8];
		u32 x = 0;

		for (i = 0; i < SECB; i++)
			sec[i] = (u8)rnd();
		put32(sec, rnd() % 8 ? 0x134da11 : rnd() % 2 ? 0x134ff90 : 0x134ff91);
		sec[0x5a] = 1;
		sec[0x48] = (u8)(rnd() % 3);
		/* EV knots: ascending with an optional zero start / equal neighbours */
		for (i = 0; i < 12; i++) {
			if (!(rnd() % 4) && i < 6)
				x = 0;
			else if (rnd() % 5)
				x += rnd() % 5000;
			put32(sec + 0x18 + 4 * i, x);
		}
		if (!(rnd() % 16))
			memset(sec + 0x18, 0, 48);	/* no knot above 0: last strength byte */
		for (i = 0; i < sizeof(lut); i++)
			lut[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 0xf);
		run_case("random", sec, lut, T32_GAMMA_CURVE_BYTES);
	}
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_gamma_oracle: random %u compared (%u refused by ours), IQ bins %d (%u compared), %u mismatches\n",
	       rc_r, refused, nbins, compared - rc_r, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_gamma_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
