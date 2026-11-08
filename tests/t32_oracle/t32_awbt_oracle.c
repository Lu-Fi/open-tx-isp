// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB colour temperature 3: our t32_awb_ct_cal (driver/t32/
 * tx_isp_t32_awbct.h) against the vendor Tiziano_Awb_Ct_Cal (0x20054, with
 * ISPAWBInterpolation1/2 and fix_point_div_32) from Matt Davis' mechanical MIPS-to-C
 * recovery (ref/, PR #15 @ 2e2e06bc).  The vendor reads its section through a pointer
 * table (slots 24, 28, 32, 44, 52 = axes, grid, ct, shifts); the oracle builds that
 * table.  Section: random bytes with realistic axes (ascending, scaled) and grids,
 * and also unordered / random ones; the ratios r, b at every axis point +-1, inside
 * the cells, below and above the axes; out_shift 0..33.  Compared: the ct word and
 * every byte of the section.  Also with section 2 of every *.bin in T32_IQ_DIR as par.
 * Exit 0 = equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_awbct.h"

#define SECB	6740u
#define PARB	sizeof(((struct t32_awb_par *)0)->raw)

static u32 rs = 0x3141b9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void p16(u8 *p, u32 off, u32 v) { p[off] = (u8)v; p[off + 1] = (u8)(v >> 8); }
static void p32(u8 *p, u32 off, u32 v) { p16(p, off, v); p16(p, off + 2, v >> 16); }

static unsigned int cases, failed, zero_v, cls[3];

static int run_case(const char *what, const u8 *sec)
{
	static u8 par[PARB], opar[PARB];
	u8 *vlp, *vpar;
	u32 s, r, b, i, ov, ct0 = 0;
	const char *bad = 0;

	if (sec)
		memcpy(par, sec, PARB);
	else
		for (i = 0; i < PARB; i++)
			par[i] = (u8)rnd();
	if (!sec || rnd() % 2) {
		u32 x = rnd() % 200, y = rnd() % 200;

		for (i = 0; i < 15; i++) {
			x += 1 + rnd() % 300;
			y += 1 + rnd() % 300;
			p32(par, 60 + 4 * i, rnd() % 16 ? x : rnd() % 3000);
			p32(par, 120 + 4 * i, rnd() % 16 ? y : rnd() % 3000);
		}
		for (i = 0; i < 225; i++)
			p32(par, 2164 + 4 * i, rnd() % 20 ? 200 + rnd() % 6000 : (rnd() % 3 ? 0 : rnd()));
		p16(par, 3306, 10 + rnd() % 4);
		p16(par, 3308, rnd() % 3 ? 6 + rnd() % 6 : rnd() % 34);
	}
	s = par[3308] | par[3309] << 8;
	s &= 31;
	{
		u32 ar = 60 + 4 * (rnd() % 15), ab = 120 + 4 * (rnd() % 15);
		u32 vr = (par[ar] | par[ar + 1] << 8 | par[ar + 2] << 16 | (u32)par[ar + 3] << 24) << s;
		u32 vb = (par[ab] | par[ab + 1] << 8 | par[ab + 2] << 16 | (u32)par[ab + 3] << 24) << s;

		switch (rnd() % 5) {
		case 0: r = vr + rnd() % 3 - 1; b = vb + rnd() % 3 - 1; break;
		case 1: r = vr + rnd() % 300; b = vb + rnd() % 300; break;
		case 2: r = rnd() % (vr + 2); b = rnd() % (vb + 2); break;
		case 3: r = rnd(); b = rnd(); break;
		default: r = vr + rnd() % 3 - 1; b = rnd() % 4000 << s; break;
		}
	}
	memcpy(opar, par, PARB);

	t32o_reset();
	vlp = t32o_alloc(64);
	vpar = t32o_alloc(PARB);
	memcpy(vpar, par, PARB);
	p32(vlp, 24, T32O_PTR(vpar) + 60);
	p32(vlp, 28, T32O_PTR(vpar) + 120);
	p32(vlp, 32, T32O_PTR(vpar) + 2164);
	p32(vlp, 44, T32O_PTR(vpar) + 44);
	p32(vlp, 52, T32O_PTR(vpar) + 3306);
	t32o_call((t32o_fn)(void (*)(void))Tiziano_Awb_Ct_Cal, T32O_PTR(vlp), r, b, 0, 0);
	t32_awb_ct_cal((struct t32_awb_par *)opar, r, b);
	(void)ov; (void)ct0;

	cases++;
	zero_v += ((struct t32_awb_par *)opar)->ct == 5000;
	cls[0]++;
	if (memcmp(vpar, opar, PARB))
		bad = "section";
	if (!bad)
		return 0;
	if (failed++ < 5) {
		printf("MISMATCH %s: %s (r %u b %u shift %u)\n", what, bad, r, b, s);
		printf("  ct vendor %u ours %u\n", vpar[44] | vpar[45] << 8 | vpar[46] << 16 | (u32)vpar[47] << 24, ((struct t32_awb_par *)opar)->ct);
		for (i = 0; i < PARB; i++)
			if (vpar[i] != opar[i]) { printf("  par+%u vendor %02x ours %02x\n", i, vpar[i], opar[i]); break; }
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
		char pth[1024];
		static u8 b[1u << 20];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);
		u32 off, len = 0, k;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(pth, sizeof(pth), "%s/%s", dir, e->d_name);
		f = fopen(pth, "rb");
		if (!f)
			continue;
		bytes = fread(b, 1, sizeof(b), f);
		fclose(f);
		off = t32_iq_section(b, (u32)bytes, T32_IQ_SEC_AWB, &len);
		if (!off || len < SECB)
			continue;
		nbins++;
		for (k = 0; k < 400; k++)
			run_case(e->d_name, b + off);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 40000;
	int nbins;

	for (c = 0; c < n; c++)
		run_case("random", 0);
	rc_r = cases;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_awbt_oracle: random %u, IQ bins %d (%u cases), ct 5000 in %u, %u mismatches\n", rc_r, nbins,
	       cases - rc_r, zero_v, failed);
	(void)cls;
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbt_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
