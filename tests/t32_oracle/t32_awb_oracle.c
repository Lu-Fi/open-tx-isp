// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our t32_awb_stats_init (driver/t32/tx_isp_t32_stats.h)
 * against the vendor tisp_awb_init (-> tisp_awb_malloc_cfg, tisp_awb_set_hardware_param
 * with set_regional_threshold / set_lum_th_freq, params_refresh, set_riscv_param,
 * set_gain, ...), from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc, built for the host with env.c).
 *
 * The vendor init also writes other blocks and the gain registers; ours reproduces
 * the AWB statistics setup.  Compared: the vendor writes to 0x16000..0x16fff (the
 * AWB statistics block), in order, with ours.  IQ section 2 (6740 bytes) sits at
 * tuning base + 0xd40 (ours reads it at bin + 0xddc).  Inputs: random sections and
 * section 2 of every *.bin in T32_IQ_DIR (default
 * ../external/ingenic-sdk/sensor-iq/t32).
 *
 * Exit 0 = all equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_stats.h"

#define SECB	6740u
#define SEC_OFF	0xd40u

static u32 rs = 0x3b7c19u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

static int run_case(const char *what, const u8 *sec, u32 w_, u32 h_)
{
	static u8 bin[T32_AWB_PARAM + SECB + 16];
	struct t32_stats_write ow[T32_STATS_MAX_WRITES + 64];
	struct { u32 reg, val; } vw[512];
	u8 *base, *info;
	unsigned int on, nv = 0, i;
	u32 buf_pa = 0;

	t32o_reset();
	base = t32o_alloc(0x8000);
	info = t32o_alloc(64);
	memcpy(base + SEC_OFF, sec, SECB);
	info[0] = (u8)w_; info[1] = (u8)(w_ >> 8); info[4] = (u8)h_; info[5] = (u8)(h_ >> 8);
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32o_call((t32o_fn)(void (*)(void))tisp_awb_init, 0, T32O_PTR(info), 0, 0, 0);
	for (i = 0; i < t32o_log.n && nv < 512; i++)
		if ((t32o_log.w[i].reg >> 12) == 0x16) {
			vw[nv].reg = t32o_log.w[i].reg;
			vw[nv++].val = t32o_log.w[i].val;
		}
	for (i = 0; i < nv; i++)
		if (vw[i].reg == 0x16078)
			buf_pa = vw[i].val;

	memset(bin, 0, sizeof(bin));
	memcpy(bin + T32_AWB_PARAM, sec, SECB);
	on = t32_awb_stats_init(bin, w_, h_, buf_pa, T32_AWB_INIT_EV, ow);
	compared++;
	if (on != nv)
		goto bad;
	for (i = 0; i < nv; i++)
		if (ow[i].reg != vw[i].reg || ow[i].val != vw[i].val)
			goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		if (getenv("T32O_DBG")) {
			unsigned int q;

			printf("sec+4588:");
			for (q = 0; q < 12; q++)
				printf(" %02x", sec[4588 + q]);
			printf("\nvendor sec+4588:");
			for (q = 0; q < 12; q++)
				printf(" %02x", ((u8 *)T32O_HOST(t32_data_dd0a4[0]))[SEC_OFF + 4588 + q]);
			printf("\nvendor sec+3252:");
			for (q = 0; q < 12; q++)
				printf(" %02x", ((u8 *)T32O_HOST(t32_data_dd0a4[0]))[SEC_OFF + 3252 + q]);
			printf("\nchanged offsets:");
			for (q = 0; q < SECB; q++)
				if (((u8 *)T32O_HOST(t32_data_dd0a4[0]))[SEC_OFF + q] != sec[q])
					printf(" %u", q);
			printf("\n");
		}
		printf("MISMATCH %s: AWB writes vendor %u ours %u\n", what, nv, on);
		for (i = 0; i < nv || i < on; i++)
			if (i >= nv || i >= on || ow[i].reg != vw[i].reg || ow[i].val != vw[i].val) {
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i, i < nv ? vw[i].reg : 0,
				       i < nv ? vw[i].val : 0, i < on ? ow[i].reg : 0, i < on ? ow[i].val : 0);
				if (i > 10)
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
		static u8 b[1u << 20];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);
		u32 off, len = 0;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(b, 1, sizeof(b), f);
		fclose(f);
		off = t32_iq_section(b, (u32)bytes, T32_IQ_SEC_AWB, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		run_case(e->d_name, b + off, 3840, 2160);
		run_case(e->d_name, b + off, 1920, 1080);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, i, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 4000;
	int nbins;

	for (c = 0; c < n; c++) {
		static u8 sec[SECB];

		for (i = 0; i < SECB; i++)
			sec[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 7);
		run_case("random", sec, 64 + rnd() % 4000, 64 + rnd() % 3000);
	}
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_awb_oracle: random %u compared, IQ bins %d (%u compared), %u mismatches\n", rc_r, nbins,
	       compared - rc_r, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awb_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
