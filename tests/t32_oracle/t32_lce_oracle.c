// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable t32_lce_init (driver/t32/tx_isp_t32_lce.h) against the vendor
 * flow tisp_lce_init -> tisp_lce_par_to_used (-> tgain_interp_strength, gauss_lpf / gauss_wgt),
 * tisp_lce_malloc_cfg, tisp_lce_size_para_calc, tisp_lce_curve_init_default -> write_all_reg ->
 * write_ctrl_reg / write_curve, from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15
 * @ 2e2e06bc).  The init chain is scalar C (no MXU2).
 *
 * Compared: return, every register write (address, value, order) except the five DMA address
 * registers 0xc090..0xc09c and 0xc0a4 (the vendor takes them from its own allocations; ours from
 * the caller's two buffers).  Inputs: random sections (id, the strength/gain tables, the gauss
 * parameter byte +0x1b0) at random frame sizes incl. 1920x1080 (the vendor takes its grid from tables there, equal to the formula) and the block-size edges, and
 * section 15 of every *.bin in T32_IQ_DIR at three sizes.  Cases ours refuses on purpose
 * (t32_lce_init returns -1: id, +0x1fb, grid of 0) are counted, not compared.
 *
 * Exit 0 = equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_lce.h"

#define SECB	T32_LCE_SEC_BYTES
#define SEC_OFF	0x8494u		/* section 15 in the vendor's tuning-parameter block */

static u32 rs = 0x6f1d35u;
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

static int is_addr_reg(u32 r)
{
	return (r >= 0xc090 && r <= 0xc09c) || r == 0xc0a4;
}

static int run_case(const char *what, const u8 *sec, u32 width, u32 height)
{
	static struct t32_front_write ow[T32_LCE_MAX_WRITES + 8];
	u8 *base, *size;
	int on;
	unsigned int i, j, nv = 0;
	static u32 vreg[512], vval[512];
	u32 rv;

	on = t32_lce_init(sec, width, height, 0x10000000u, 0x10100000u, ow);
	if (on < 0) {
		refused++;
		return 0;
	}
	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	size = t32o_alloc(16);
	memcpy(base + SEC_OFF, sec, SECB);
	put32(size, width);
	put32(size + 4, height);
	t32_data_dd0a4[0] = T32O_PTR(base);
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_init, 0, T32O_PTR(size), 0, 0, 0);

	compared++;
	for (i = 0; i < t32o_log.n && nv < 512; i++)
		if (!is_addr_reg(t32o_log.w[i].reg)) {
			vreg[nv] = t32o_log.w[i].reg;
			vval[nv++] = t32o_log.w[i].val;
		}
	if (rv)
		goto bad;
	for (i = j = 0; i < (unsigned int)on; i++) {
		if (is_addr_reg(ow[i].reg))
			continue;
		if (j >= nv || vreg[j] != ow[i].reg || vval[j] != ow[i].val)
			goto bad;
		j++;
	}
	if (j != nv)
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s %ux%u: vendor rv %u writes %u ours %d\n", what, width, height, rv, t32o_log.n, on);
		for (i = j = 0; i < (unsigned int)on || j < nv; i++) {
			u32 orr = 0, ov = 0, vr = 0, vv = 0;
			int has_o = 0;

			while (i < (unsigned int)on && is_addr_reg(ow[i].reg))
				i++;
			if (i < (unsigned int)on) {
				has_o = 1;
				orr = ow[i].reg;
				ov = ow[i].val;
			}
			if (j < nv) {
				vr = vreg[j];
				vv = vval[j];
			}
			if (!has_o && j >= nv)
				break;
			if (!has_o || j >= nv || vr != orr || vv != ov) {
				printf("  vendor %05x=%08x ours %05x=%08x\n", vr, vv, orr, ov);
				break;
			}
			j++;
		}
	}
	return 1;
}

static void random_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (rnd() % 3) ? (u8)rnd() : (u8)(rnd() & 7);
	put32(sec, T32_LCE_ID);
	sec[0x1fb] = (u8)(rnd() % 8 ? 0 : rnd());
	if (rnd() % 8)
		sec[0x1b0] = (u8)(rnd() % 40);
}

static const u32 sizes[][2] = { { 3840, 2160 }, { 2560, 1440 }, { 1920, 1080 }, { 1280, 720 }, { 640, 480 }, { 2688, 1520 },
				{ 1600, 1200 }, { 800, 600 }, { 320, 240 }, { 704, 576 }, { 128, 64 }, { 64, 36 } };

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
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_LCE, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		for (k = 0; k < 3; k++)
			run_case(e->d_name, bin + off, sizes[k][0], sizes[k][1]);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	int nbins;

	for (c = 0; c < n; c++) {
		static u8 sec[SECB];
		u32 w, h;

		random_section(sec);
		if (c % 3 == 0) {
			w = sizes[rnd() % 12][0];
			h = sizes[rnd() % 12][1];
		} else {
			w = 64 + (rnd() % 4000);
			h = 36 + (rnd() % 2300);
		}
		run_case("random", sec, w, h);
	}
	/* grid sizes right at the band thresholds of the 9th grid product (0x100 .. 0x4000) */
	{
		u32 t, ww, hh;
		int dd;

		for (t = 0x100; t <= 0x4000; t <<= 1)
			for (dd = -4; dd <= 4; dd += 4) {
				int found = 0;

				for (ww = 64; ww < 4400 && found < 2; ww += 32)
					for (hh = 36; hh < 2400 && found < 2; hh += 36) {
						u32 c2 = (ww >> 5) & 0xfe, c5 = (((hh >> 1) / 18) & 0x7f) * 2;

						if (c2 * c5 == t + dd) {
							static u8 sec2[SECB];

							random_section(sec2);
							run_case("edge", sec2, ww, hh);
							found++;
						}
					}
			}
	}
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_lce_oracle: random %u compared (%u refused by ours), IQ bins %d (%u compared), %u mismatches\n",
	       rc_r, refused, nbins, compared - rc_r, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_lce_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
