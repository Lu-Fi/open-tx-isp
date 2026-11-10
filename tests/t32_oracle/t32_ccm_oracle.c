// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable t32_ccm_init (driver/t32/tx_isp_t32_ccm.h)
 * against the vendor flow tisp_ccm_init -> tisp_ccm_interp_by_ct ->
 * tisp_ccm_interp_by_ev -> tisp_ccm_calc_para_refresh -> tisp_ccm_write_reg,
 * taken from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc, built for the host with env.c).
 *
 * Compared bit-exact: every register write (address, value, order) and the
 * IQ section bytes after the call (the vendor clears +0x360/+0x361).
 * Inputs: random sections and knot tables (boundaries of the 5000 K search
 * and the hysteresis, coefficient ranges up to the clamp and beyond), and
 * section 12 of every *.bin in T32_IQ_DIR (default
 * ../external/ingenic-sdk/sensor-iq/t32).
 *
 * Sections our driver refuses on purpose (t32_ccm_init returns -1: id,
 * +0x35f = 0, no knots) are counted, not compared.
 *
 * Exit 0 = all compared cases equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_ccm.h"

#define SECB	T32_CCM_SEC_BYTES
#define SEC_OFF	0x7e40u		/* section 12 in the vendor's tuning-parameter block */

static u32 rs = 0x7c3a91u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, refused, failed, ev_compared;

/*
 * The exposure knots at other exposures: after the init the vendor's own
 * state (e28f0[0] -> { section, exposure state, parameter block, ..., flags })
 * is live, so call tisp_ccm_interp_by_ev again with a forced update (arg4 = 1)
 * and compare the parameter block with t32_ccm_ev_knots(ev = hi << 22 | lo >> 10).
 * Only for the knot path (+0x38 != 1).
 */
static int ev_cases(const u8 *sec, const u8 *osec, const u8 *lut)
{
	u32 *st = T32O_HOST(t32_data_e28f0[0]);
	u8 *cpv = T32O_HOST(st[2]);
	unsigned int c;

	(void)lut;
	if (t32_front_le32(osec + 0x38) == 1)
		return 0;
	for (c = 0; c < 6; c++) {
		u32 lo = rnd(), hi = rnd() % 4 ? rnd() & 0x3ff : rnd(), ev, ret;
		u8 cp[0x68];

		if (c < 4) {	/* on / around a threshold */
			u32 thr = t32_front_le32(sec + 4 + 4 * (rnd() % 12)) + (rnd() % 3) - 1;

			hi = thr >> 22;
			lo = thr << 10 | (rnd() & 0x3ff);
		}
		ev = hi << 22 | lo >> 10;
		memcpy(cp, cpv, sizeof(cp));
		t32_ccm_ev_knots(osec, cp, ev);
		ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ccm_interp_by_ev, 0, 0, lo, hi, 1);
		ev_compared++;
		if (ret || memcmp(cp, cpv, sizeof(cp))) {
			if (failed < 5)
				printf("MISMATCH ev: ev %08x vendor ret %u\n", ev, ret);
			return 1;
		}
	}
	return 0;
}

/* one case; returns 0 equal, 1 mismatch */
static int run_case_mode(const char *what, const u8 *sec, const u8 *lut, u32 lutbytes, u8 dsm)
{
	struct t32_front_write ow[T32_CCM_MAX_WRITES];
	u8 osec[SECB], *base, *vlut, *info;
	int on;
	unsigned int i;

	memcpy(osec, sec, SECB);
	on = t32_ccm_init(osec, lut, lutbytes, ow);
	if (on < 0) {
		refused += !dsm;
		return 0;
	}

	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB);
	vlut = t32o_alloc(lutbytes ? lutbytes : 4);
	info = t32o_alloc(64);
	memcpy(base + SEC_OFF, sec, SECB);
	memcpy(vlut, lut, lutbytes);
	put32(info + 24, T32O_PTR(vlut));
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32o_decoy_arm();
	t32_data_dc274[0] = T32O_PTR(info);
	t32_data_d03a0[0] = dsm;	/* 1: tisp_reg_write queues into the register shadow (logged tagged, same values) */
	t32o_call((t32o_fn)(void (*)(void))tisp_ccm_init, 0, 0, 0, 0, 0);

	compared += !dsm;
	if (t32o_log.n != (unsigned int)on)
		goto bad;
	for (i = 0; i < t32o_log.n; i++)
		if (!t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) || t32o_log.w[i].val != ow[i].val)
			goto bad;
	if (memcmp(base + SEC_OFF, osec, SECB))
		goto bad;
	if (ev_cases(sec, osec, lut) )
		goto bad;
	if (t32o_decoy_check())	/* the other channels' section bases must stay untouched */
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s: writes vendor %u ours %d, knots %u hyst %u\n", what, t32o_log.n, on,
		       sec[0x364], sec[0x365]);
		for (i = 0; i < t32o_log.n || (int)i < on; i++) {
			int d = i >= t32o_log.n || (int)i >= on || !t32o_reg_eq(t32o_log.w[i].reg, ow[i].reg) ||
				t32o_log.w[i].val != ow[i].val;

			if (d)
				printf("  #%u vendor %04x=%08x ours %04x=%08x\n", i,
				       i < t32o_log.n ? t32o_log.w[i].reg : 0, i < t32o_log.n ? t32o_log.w[i].val : 0,
				       (int)i < on ? ow[i].reg : 0, (int)i < on ? ow[i].val : 0);
		}
		for (i = 0; i < SECB; i++)
			if (base[SEC_OFF + i] != osec[i])
				printf("  sec+0x%x vendor %02x ours %02x\n", i, base[SEC_OFF + i], osec[i]);
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

static int coef(int mode)
{
	switch (mode) {
	case 0: return (int)(rnd() % 0x4000) - 0x2000;		/* inside the clamp */
	case 1: return (int)(rnd() % 0x10000) - 0x8000;		/* beyond the clamp */
	case 2: return (rnd() & 1) ? 0x1fff + (int)(rnd() % 3) - 1 : -0x2000 + (int)(rnd() % 3) - 1;
	default: return (int)(rnd() % 0x200000) - 0x100000;	/* 21 bit */
	}
}

static void random_cases(unsigned int n)
{
	static u8 sec[SECB], lut[255 * 40];
	unsigned int c, i, k;

	for (c = 0; c < n; c++) {
		int mode = (int)(c % 4), knots, hyst, ct;

		for (i = 0; i < SECB; i++)
			sec[i] = (u8)rnd();
		put32(sec, T32_CCM_ID);
		if (rnd() % 16)
			sec[0x35f] = (u8)(1 + rnd() % 255);
		else
			sec[0x35f] = 0;
		put32(sec + 0x38, rnd() % 8 ? rnd() % 4 + 2 : rnd() % 2);	/* sometimes 0/1 */
		/*
		 * exposure knots: the vendor walks the 12 thresholds (+4) for the first
		 * one above exposure 0.  Force leading zeros (0..12 of them), then
		 * small / huge / sign-bit values for the first non-zero one.
		 */
		if (rnd() % 3 == 0) {
			static const u32 first[] = { 1, 2, 3, 0x7fffffff, 0x80000000u, 0xffffffffu, 4096, 0x1000 };
			int zeros = (int)(rnd() % 13);

			for (i = 0; i < 12; i++)
				put32(sec + 4 + 4 * i, (int)i < zeros ? 0 :
				      (int)i == zeros && rnd() % 2 ? first[rnd() % 8] : rnd());
		}
		knots = c % 50 == 7 ? (int)(rnd() % 255) + 1 : (int)(rnd() % 10) + 1;
		hyst = rnd() % 4 ? (int)(rnd() % 400) & 0xff : (int)(rnd() % 8);
		sec[0x364] = (u8)knots;
		sec[0x365] = (u8)hyst;
		/* knot temperatures: ascending, one of them often on a 5000 K boundary */
		ct = 1500 + (int)(rnd() % 3000);
		for (k = 0; k < (unsigned int)knots; k++) {
			int t = ct;

			if (rnd() % 3 == 0) {
				static const int edge[] = { 0, 1, -1 };
				int e = edge[rnd() % 3], h = rnd() & 1 ? hyst : -hyst;

				t = 5000 + h + e;
				if (t < ct)
					t = ct;
			}
			put32(lut + 40 * k, (u32)t);
			for (i = 0; i < 9; i++)
				put32(lut + 40 * k + 4 + 4 * i, (u32)coef(mode));
			ct = t + (int)(rnd() % 2500);
		}
		run_case("random", sec, lut, (u32)knots * 40U);
	}
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
		size_t bytes;
		u32 off, len = 0;
		size_t l = strlen(e->d_name);

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_CCM, &len);
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
	unsigned int rc_r, rf_r;
	int nbins;

	random_cases(getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000);
	rc_r = compared;
	rf_r = refused;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_ccm_oracle: random %u compared (%u refused by ours), IQ bins %d (%u compared), %u exposure-knot calls, %u mismatches\n",
	       rc_r, rf_r, nbins, compared - rc_r, ev_compared, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_ccm_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
