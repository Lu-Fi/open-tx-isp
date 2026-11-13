// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the LCE state the driver builds (t32_lce_state_init,
 * driver/t32/tx_isp_t32_lcetg.h) against the state the vendor's tisp_lce_init leaves
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc: init -> par_to_used -> interp_strength /
 * gauss_wgt / gauss_lpf, malloc_cfg, size_para_calc, curve_init_default -> write_all_reg).
 *
 * Compared: every byte of the 0x22850-byte state except the first word (the vendor's
 * section pointer; ours passes the section separately), and every byte of the section
 * plus 64 guard bytes (the vendor edits its tuning block in place).  Inputs: random sections
 * (strength tables, weight sources incl. sigma 0, low pass byte) at the sizes of
 * t32_lce_oracle incl. 1920 x 1080 (vendor constant tables) and random sizes, plus section 15
 * of every *.bin in T32_IQ_DIR at those sizes.  Sizes below 64 x 36 (vendor divide by zero
 * in size_para_calc) are counted, not compared.
 * Exit 0 = equal; 1 = mismatch.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_lcetg.h"

#define SECB	T32_LCE_SEC_BYTES
#define SEC_OFF	0x8494u
#define STB	T32_LCETG_STATE_BYTES

static u32 rs = 0x2b9d17u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, refused, nbin_cases;
static u8 ost[STB], osec[SECB + 64];

static void run_case(const char *what, const u8 *sec, u32 width, u32 height)
{
	u8 *base, *size, *vst;
	unsigned int i;
	u32 rv;

	if (width < 64 || height < 36) {
		refused++;
		return;
	}
	memcpy(osec, sec, SECB);
	memset(osec + SECB, 0x5a, 64);
	t32_lce_state_init(ost, osec, width, height);
	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	size = t32o_alloc(16);
	memcpy(base + SEC_OFF, sec, SECB);
	memset(base + SEC_OFF + SECB, 0x5a, 64);
	put32(size, width);
	put32(size + 4, height);
	t32_data_dd0a4[0] = T32O_PTR(base);
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_init, 0, T32O_PTR(size), 0, 0, 0);
	vst = T32O_HOST(t32_data_e1da8[0]);
	compared++;
	if (rv || !vst) {
		printf("MISMATCH %s %ux%u: vendor init rv %d state %p\n", what, width, height, (int)rv, (void *)vst);
		exit(1);
	}
	for (i = 4; i < STB; i++)
		if (vst[i] != ost[i]) {
			unsigned int j;

			printf("MISMATCH %s %ux%u: state byte %#x vendor %02x ours %02x\n", what, width, height, i, vst[i], ost[i]);
			for (j = i & ~15u; j < (i & ~15u) + 48 && j < STB; j++)
				printf("%s%02x/%02x", j % 16 ? " " : "\n", vst[j], ost[j]);
			printf("\n");
			exit(1);
		}
	for (i = 0; i < SECB + 64; i++)
		if (base[SEC_OFF + i] != osec[i]) {
			printf("MISMATCH %s %ux%u: section byte %u vendor %02x ours %02x\n", what, width, height, i,
			       base[SEC_OFF + i], osec[i]);
			exit(1);
		}
}

static void random_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (rnd() % 3) ? (u8)rnd() : (u8)(rnd() & 7);
	put32(sec, T32_LCE_ID);
	sec[507] = 0;	/* tisp_lce_init interpolates at the zeroed state's gain */
	if (rnd() % 4 == 0)
		sec[507] = (u8)rnd();
	if (rnd() % 8)
		sec[0x1b0] = (u8)(rnd() % 40);
	if (rnd() % 10 == 0)
		sec[436] = sec[439] = 0;
}

extern u64 tisp_lce_init(u32 a0, u32 a1, u32 a2, u32 a3);

static const u32 sizes[][2] = { { 3840, 2160 }, { 2560, 1440 }, { 1920, 1080 }, { 1280, 720 }, { 640, 480 }, { 2688, 1520 },
				{ 1600, 1200 }, { 800, 600 }, { 320, 240 }, { 704, 576 }, { 128, 64 }, { 64, 36 }, { 1920, 1088 },
				{ 2304, 1296 }, { 32, 16 }, { 4096, 2304 } };
#define NSZ (sizeof(sizes) / sizeof(sizes[0]))

static void bin_cases(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
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
		for (k = 0; k < 4; k++) {
			run_case(e->d_name, bin + off, sizes[k * 3 % NSZ][0], sizes[k * 3 % NSZ][1]);
			nbin_cases++;
		}
	}
	closedir(d);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 1500;
	static u8 sec[SECB];
	unsigned int c;

	for (c = 0; c < n; c++) {
		u32 w, h;

		random_section(sec);
		if (rnd() % 3) {
			w = sizes[rnd() % NSZ][0];
			h = sizes[rnd() % NSZ][1];
		} else {
			w = 64 + rnd() % 4000;
			h = 36 + rnd() % 3000;
		}
		run_case("random", sec, w, h);
	}
	if (getenv("T32_IQ_DIR"))
		bin_cases(getenv("T32_IQ_DIR"));
	printf("t32_lceinit_oracle: %u compared (%u refused, %u from IQ bins), 0 mismatches\n", compared, refused, nbin_cases);
	return 0;
}
