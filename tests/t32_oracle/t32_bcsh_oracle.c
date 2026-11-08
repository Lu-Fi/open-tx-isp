// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable t32_bcsh_init (driver/t32/tx_isp_t32_bcsh.h)
 * against the vendor flow tisp_bcsh_init -> tisp_csc_api_get, get_new_para,
 * interp_by_ev (exposure 0, forced), interp_by_ct (5000 K, forced),
 * calc_para_refresh (adjust_brightness / _contrast / _hue, matrix_multi), write_reg,
 * from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc,
 * built for the host with env.c).
 *
 * The vendor reads the colour space from the CSC block, so the oracle first runs
 * the vendor's tisp_csc_init (section 9) and clears the write log.  Compared
 * bit-exact: every register write of the BCSH init (address, value, order) and
 * the IQ section bytes after the call.  IQ section 19 (1204 bytes) sits at tuning
 * base + 53588, its colour temperature knots (+0x4af knots x 40 bytes) at
 * lutinfo + 28.  Inputs: random sections and knots, and section 19 / 9 of every
 * *.bin of T32_IQ_DIR (default ../external/ingenic-sdk/sensor-iq/t32).  Sections
 * ours refuses (-1: block id, exposure mode byte +0x4b1, matrix mode above 2,
 * other colour space path) are counted, not compared.
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
#include "tx_isp_t32_bcsh.h"

#define SECB	T32_BCSH_SEC_BYTES
#define SEC_OFF	53588u
#define CSC_OFF	27488u

static u32 rs = 0x4d29b1u;
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
#define NBASE 80
static u8 base_sec[NBASE][T32_BCSH_SEC_BYTES], base_lut[NBASE][10 * 40], base_csc[NBASE][104];
static u32 base_lutlen[NBASE];
static int nbase, quiet;

static int run_case(const char *what, const u8 *sec, const u8 *lut, u32 lutbytes, const u8 *csc)
{
	static struct t32_front_write ow[T32_BCSH_MAX_WRITES + 16];
	static u8 osec[SECB];
	u8 *base, *info, *vlut;
	int on;
	unsigned int i;

	if (csc[0x5b] > 5)
		return 0;
	memcpy(osec, sec, SECB);
	on = t32_bcsh_init(osec, lut, lutbytes, csc, ow);
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
	put32(info + 28, T32O_PTR(vlut));
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_dc274[0] = T32O_PTR(info);
	t32o_call((t32o_fn)(void (*)(void))tisp_csc_init, 0, 0, 0, 0, 0);
	t32o_log.n = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_bcsh_init, 0, 0, 0, 0, 0);

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
	if (quiet)
		return 1;
	if (failed++ < 5) {
		if (getenv("T32O_DBG")) {
			u32 *ctx = T32O_HOST(t32_data_e2450[0]);
			u8 *c2 = T32O_HOST(ctx[2]);
			unsigned int q;

			printf("vendor ctx %08x %08x %08x %08x\n", ctx[0], ctx[1], ctx[2], ctx[3]);
			printf("vendor cp+0xdc:");
			for (q = 0; q < 9; q++)
				printf(" %08x", c2[0xdc + 4 * q] | c2[0xdd + 4 * q] << 8 | c2[0xde + 4 * q] << 16 | (u32)c2[0xdf + 4 * q] << 24);
			printf("\n");
		}
		printf("MISMATCH %s: knots %u writes vendor %u ours %d (4b1=%u 492=%u)\n", what, sec[0x4af], t32o_log.n, on, sec[0x4b1], sec[0x492]);
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
		u32 off, len = 0, coff, clen = 0;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_BCSH, &len);
		coff = t32_iq_section(bin, (u32)bytes, 9, &clen);
		if (!off || len < SECB || !coff || clen < 104)
			continue;
		nbins++;
		if (nbase < NBASE) {
			u32 ll = bin[off + 0x4af] * 40U;

			memcpy(base_sec[nbase], bin + off, SECB);
			memcpy(base_lut[nbase], bin + off + len, ll > 400 ? 400 : ll);
			memcpy(base_csc[nbase], bin + coff, 104);
			base_lutlen[nbase++] = ll > 400 ? 400 : ll;
		}
		run_case(e->d_name, bin + off, bin + off + len, (u32)bytes - off - len, bin + coff);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, i, k, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6000;
	int nbins;

	/* the bins first (fills the bases), then perturbed copies of them, then fully random sections */
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	rc_r = compared;
	for (c = 0; c < n && nbase; c++) {
		static u8 sec[SECB], lut[10 * 40], csc[104];
		unsigned int b = rnd() % nbase, np = 1 + rnd() % 6;

		memcpy(sec, base_sec[b], SECB);
		memcpy(lut, base_lut[b], base_lutlen[b]);
		memcpy(csc, base_csc[b], 104);
		while (np--) {
			unsigned int r = rnd() % (SECB + 104);

			if (r < SECB && r >= 4)
				sec[r] = (u8)(rnd() % 3 ? sec[r] + (rnd() % 5) - 2 : rnd());
			else if (r >= SECB && r - SECB != 0x5b)
				csc[r - SECB] = (u8)(rnd() % 3 ? csc[r - SECB] + (rnd() % 5) - 2 : rnd());
		}
		sec[0x4b1] = (u8)(rnd() % 16 ? 0 : 1 + rnd() % 3);
		csc[0x5b] = base_csc[b][0x5b];
		if (run_case("perturbed", sec, lut, base_lutlen[b], csc) && getenv("T32O_MIN")) {
			unsigned int q;

			quiet = 1;
			for (q = 0; q < SECB + 104; q++) {
				u8 *pb = q < SECB ? &sec[q] : &csc[q - SECB];
				u8 orig = q < SECB ? base_sec[b][q] : base_csc[b][q - SECB], cur = *pb;

				if (cur == orig)
					continue;
				*pb = orig;
				if (!run_case("min", sec, lut, base_lutlen[b], csc))	/* reverting fixes it: this byte matters */
					printf("  culprit %s+0x%x: %02x -> %02x\n", q < SECB ? "sec" : "csc", q < SECB ? q : q - SECB, orig, cur);
				*pb = cur;
			}
			quiet = 0;
		}
	}
	for (c = 0; c < n; c++) {
		static u8 sec[SECB], lut[10 * 40], csc[104];
		unsigned int knots = 1 + rnd() % 10;
		u32 ct = 1500 + rnd() % 2500;

		for (i = 0; i < SECB; i++)
			sec[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 7);
		put32(sec, 0x0134dae2);
		sec[0x4af] = (u8)knots;
		sec[0x4b0] = (u8)(rnd() % 4 ? rnd() % 255 : rnd() % 6);
		sec[0x4b1] = (u8)(rnd() % 16 ? 0 : 1 + rnd() % 3);
		sec[0x492] = (u8)(rnd() % 3);
		for (k = 0; k < knots; k++) {
			u32 t = rnd() % 3 ? ct : 5000 + (rnd() % 2 ? 1 : -1) * (sec[0x4b0] + rnd() % 3);

			if (t < ct)
				t = ct;
			put32(lut + 40 * k, t);
			for (i = 0; i < 9; i++)
				put32(lut + 40 * k + 4 + 4 * i, (c % 4) ? (u32)((int)(rnd() % 4096) - 2048) : rnd());
			ct = t + rnd() % 2500;
		}
		for (i = 0; i < 104; i++)
			csc[i] = (u8)rnd();
		csc[0x5b] = (u8)(rnd() % 6);
		csc[0x62] = (u8)(rnd() % 5 ? (rnd() % 2) : rnd());
		csc[0x63] = (u8)(rnd() % 3);
		run_case("random", sec, lut, knots * 40, csc);
	}
	printf("t32_bcsh_oracle: random %u compared (%u refused by ours), IQ bins %d (%u compared), %u mismatches\n",
	       compared - rc_r, refused, nbins, rc_r, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_bcsh_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
