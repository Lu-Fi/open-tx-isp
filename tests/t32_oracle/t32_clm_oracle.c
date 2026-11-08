// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable t32_clm_init (driver/t32/tx_isp_t32_clm.h)
 * against the vendor flow tisp_clm_init -> tisp_csc_api_get, clm_write_csc_para,
 * clm_ct_interp (clm_itp), clm_write_lut (clm_lut_write_lut, tisp_reg_lut_write),
 * clm_calc_para_refresh, clm_write_reg, from Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 *
 * The vendor reads the colour space from the CSC block, so the oracle first
 * runs the vendor's tisp_csc_init (section 9 with the mode byte 0x5b) and then
 * clears the write log.  Compared bit-exact: every register write of the CLM
 * init (address, value, order).  IQ section 20 (40 bytes) sits at tuning base
 * + 0xd608, its two payload tables at lutinfo + 36 (zones x 2102 bytes) and
 * + 40 (zones x 1050 bytes).  Inputs: random sections (zone count 0..5,
 * hysteresis, colour temperatures around 5000 K, coefficients incl. extremes)
 * for the CSC modes 0..5, and section 20 of every *.bin of T32_IQ_DIR (default
 * ../external/ingenic-sdk/sensor-iq/t32, with its own section 9 mode).
 * Sections ours refuses (-1) are counted, not compared.
 *
 * Known vendor quirk, outside the generated domain: when 5000 K is within the
 * hysteresis band of a zone's colour temperature, the vendor's tisp_clm_ct_interp
 * can return -1 without filling the tables (they stay zero); ours takes the zone
 * table as documented.  The 67 real bins never come near that band.
 *
 * Exit 0 = all compared cases equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_front.h"
#include "tx_isp_t32_clm.h"

#define SECB	T32_CLM_SEC_BYTES
#ifndef MARGIN
#define MARGIN 2
#endif
#define SEC_OFF	0xd608u
#define CSC_OFF	27488u

static u32 rs = 0x2b8f13u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put16(u8 *p, int v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8);
}

static unsigned int compared, refused, failed;

/* csc: the 104-byte section 9 (mode at +0x5b) */
static int run_case(const char *what, const u8 *sec, const u8 *lut, u32 lutbytes, const u8 *csc)
{
	static struct t32_front_write ow[T32_CLM_MAX_WRITES];
	u8 *base, *info, *vlut;
	unsigned int mode = csc[0x5b], zones = sec[0x23];
	int on;
	unsigned int i;

	if (mode > 5)
		return 0;
	on = t32_clm_init(sec, lut, lutbytes, mode, t32_csc_modes[mode].m, ow);
	if (on < 0) {
		refused++;
		return 0;
	}
	t32o_reset();
	base = t32o_alloc(0x10000);
	info = t32o_alloc(64);
	vlut = t32o_alloc(lutbytes + 8);
	memcpy(base + SEC_OFF, sec, SECB);
	memcpy(base + CSC_OFF, csc, 104);
	memcpy(vlut, lut, lutbytes);
	{
		u32 p1 = T32O_PTR(vlut), p2 = T32O_PTR(vlut + zones * 2102u);

		memcpy(info + 36, &p1, 4);
		memcpy(info + 40, &p2, 4);
	}
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_dc274[0] = T32O_PTR(info);
	t32o_call((t32o_fn)(void (*)(void))tisp_csc_init, 0, 0, 0, 0, 0);
	t32o_log.n = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_clm_init, 0, 0, 0, 0, 0);

	compared++;
	if (t32o_log.n != (unsigned int)on)
		goto bad;
	for (i = 0; i < t32o_log.n; i++)
		if (t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val)
			goto bad;
	return 0;
bad:
	if (failed++ < 40 && getenv("T32O_CLS")) {
		unsigned int z;

		printf("F hyst %u n %u:", sec[0x24], zones);
		for (z = 0; z < zones; z++)
			printf(" %d", (short)(lut[2102 * z] | lut[2102 * z + 1] << 8));
		printf("\n");
	}
	if (failed++ < 5) {
		printf("MISMATCH %s: mode %u zones %u writes vendor %u ours %d\n", what, mode, zones, t32o_log.n, on);
		{
			unsigned int z;

			printf("  hyst %u cts:", sec[0x24]);
			for (z = 0; z < zones; z++)
				printf(" %d", (short)(lut[2102 * z] | lut[2102 * z + 1] << 8));
			printf("\n");
		}
		for (i = 0; i < t32o_log.n || (int)i < on; i++)
			if (i >= t32o_log.n || (int)i >= on || t32o_log.w[i].reg != ow[i].reg ||
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
		u32 off, len = 0, coff, clen = 0;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_CLM, &len);
		coff = t32_iq_section(bin, (u32)bytes, 9, &clen);
		if (!off || len < SECB || !coff || clen < 104)
			continue;
		nbins++;
		run_case(e->d_name, bin + off, bin + off + len, (u32)bytes - off - len, bin + coff);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, i, k, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	int nbins;

	for (c = 0; c < n; c++) {
		static u8 sec[SECB], lut[5 * 2102 + 5 * 1050 + 16], csc[104];
		unsigned int zones = rnd() % 6;
		int ct;

		memset(sec, 0, sizeof(sec));
		for (i = 0; i < SECB; i++)
			sec[i] = (u8)rnd();
		memcpy(sec, "\x11\xda\x34\x01", 4);
		sec[0x23] = (u8)zones;
		if (rnd() % 2) {	/* saturation ranges with small differences, around the ratio buckets */
			static const int edge[] = { 0, 1, 2, 3, 4, 5, 8, 9, 16, 17, 32, 33, 64, 65, 128, 129, 256, 257, 512, 513, 1024, 1025 };
			int lo1 = (int)(rnd() % 500), lo2 = (int)(rnd() % 500);

			put16(sec + 0x1a, lo1);
			put16(sec + 0x1c, lo1 + edge[rnd() % 22] + (int)(rnd() % 3 ? 0 : rnd() % 700));
			put16(sec + 0x1e, lo2);
			put16(sec + 0x20, lo2 + edge[rnd() % 22] + (int)(rnd() % 3 ? 0 : rnd() % 700));
		}
		sec[0x24] = (u8)(rnd() % 4 ? rnd() % 600 : rnd() % 8);
		/*
		 * Domain: 5000 K outside all zones by more than the hysteresis (clamped to the
		 * first or the last zone), or a single zone.  Between two zones the vendor's
		 * code (0x8a6d8) tests a signed byte of the byte table where one expects a
		 * colour temperature; this is not reproduced by ours and not generated.
		 */
		{
			int below = rnd() & 1, h = sec[0x24];

			ct = below ? 5000 - h - 2 - (int)(rnd() % 3000) : 5000 + h + 2 + (int)(rnd() % 100);
			if (below)
				ct = ct < 5000 - h - 2 - 4000 ? 1000 : ct;
			for (k = 0; k < zones; k++) {
				int t = below ? ct + (int)(k * (rnd() % 3 ? 1 : 0)) : ct + (int)k * (1 + (int)(rnd() % 400));

				if (below && t > 5000 - h - 2)
					t = 5000 - h - 2;
				put16(lut + 2102 * k, t);
				for (i = 0; i < 1050; i++)
					put16(lut + 2102 * k + 2 + 2 * i, (c % 4 == 0) ? (int)(rnd() % 65536) - 32768 : (int)(rnd() % 4096) - 2048);
			}
		}
		for (i = 0; i < zones * 1050; i++)
			lut[zones * 2102 + i] = (u8)rnd();
		for (i = 0; i < 104; i++)
			csc[i] = (u8)rnd();
		csc[0x5b] = (u8)(rnd() % 6);
		run_case("random", sec, lut, zones * (2102u + 1050u), csc);
	}
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_clm_oracle: random %u compared (%u refused by ours), IQ bins %d (%u compared), %u mismatches\n",
	       rc_r, refused, nbins, compared - rc_r, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_clm_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
