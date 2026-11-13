// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB per-frame leaf 4: our t32_awb_spec_calculate (driver/t32/
 * tx_isp_t32_awb.h) against the vendor tisp_awb_spec_calculate (0x3e5e4) from Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * The statistics registers 0x16190/94/98 are scripted (b = 0 often, big values).
 * Par and state get random bytes; the sample enable bytes (par 5704, 5709..5713) take
 * 0/1/2/other, the run/count bytes of the state 0/1/2/3, the ring counter small
 * positive and negative values and the extremes.  Compared: every (reg, val) write
 * in order, every byte of par and state afterwards.  Also with section 2 of every
 * *.bin in T32_IQ_DIR as par.
 *
 * Exit 0 = equal; 1 = mismatch; 2 = strict and no bins.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_awb.h"

extern u32 t32_data_e16b4[4];

#define SECB	6740u
#define PARB	sizeof(((struct t32_awb_par *)0)->raw)
#define STB	sizeof(((struct t32_awb_state *)0)->raw)

static u32 rs = 0x6e21a9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void p32(u8 *p, u32 off, u32 v) { p[off] = (u8)v; p[off + 1] = (u8)(v >> 8); p[off + 2] = (u8)(v >> 16); p[off + 3] = (u8)(v >> 24); }

static u32 regv[3];
static unsigned int nreads;

static u32 hook(u32 reg)
{
	nreads++;
	return reg == 0x16190 ? regv[0] : reg == 0x16194 ? regv[1] : reg == 0x16198 ? regv[2] : 0xdead0000u | reg;
}

static u32 ours_rd(void *p, u32 reg)
{
	(void)p;
	return hook(reg);
}

static unsigned int cases, failed, nwrites, pick[4];

static u32 flag(void)
{
	return rnd() % 4 == 0 ? rnd() & 0xff : rnd() % 3;
}

static u32 regval(void)
{
	switch (rnd() % 5) {
	case 0: return 0;
	case 1: return rnd() & 0xff;
	case 2: return rnd() & 0xffff;
	case 3: return 0x00ffffffu + rnd() % 3;
	default: return rnd();
	}
}

static int run_case(const char *what, const u8 *sec)
{
	static u8 par[PARB], stt[STB], opar[PARB], ost[STB];
	struct t32_stats_write ow[T32_AWB_MAX_WRITES];
	struct t32_awb a = { (struct t32_awb_par *)opar, (struct t32_awb_state *)ost, 0 };
	u8 *vctx, *vpar, *vst;
	u32 ch = rnd() % 4, i;
	unsigned int on;
	const char *bad = 0;

	if (sec)
		memcpy(par, sec, PARB);
	else
		for (i = 0; i < PARB; i++)
			par[i] = (u8)rnd();
	for (i = 5704; i < 5714; i++)
		par[i] = (u8)flag();
	if (rnd() % 3 == 0)
		for (i = 5709; i < 5714; i++)
			par[i] = 0;
	if (rnd() % 3 == 0)
		for (i = 5709; i < 5714; i++)
			par[i] = 1;
	if (rnd() % 2) {
		p32(par, 52, rnd() % 2 ? 0x400 + rnd() % 0x400 : rnd());
		p32(par, 56, rnd() % 2 ? 0x400 + rnd() % 0x400 : rnd());
	}
	memset(stt, 0, STB);
	for (i = 28672; i < 28800; i++)
		stt[i] = (u8)rnd();
	stt[28706] = (u8)(rnd() % 4 ? 1 : rnd() % 3);
	stt[28707] = (u8)(rnd() % 4);
	switch (rnd() % 6) {
	case 0: p32(stt, 119944, rnd() % 9); break;
	case 1: p32(stt, 119944, 0 - rnd() % 9); break;
	case 2: p32(stt, 119944, 0x80000000u + rnd() % 3); break;
	case 3: p32(stt, 119944, 5 + rnd() % 5); break;
	case 4: p32(stt, 119944, 0x7ffffffeu + rnd() % 2); break;
	default: p32(stt, 119944, rnd()); break;
	}
	for (i = 0; i < 3; i++)
		regv[i] = regval();
	if (rnd() % 4 == 0)
		regv[1] = 0;
	memcpy(opar, par, PARB);
	memcpy(ost, stt, STB);

	t32o_reset();
	vctx = t32o_alloc(32);
	vpar = t32o_alloc(PARB);
	vst = t32o_alloc(STB);
	memcpy(vpar, par, PARB);
	memcpy(vst, stt, STB);
	p32(vctx, 0, T32O_PTR(vpar));
	p32(vctx, 4, T32O_PTR(vst));
	t32_data_e16b4[ch] = T32O_PTR(vctx);

	t32o_rd_hook = hook;
	t32o_call((t32o_fn)(void (*)(void))tisp_awb_spec_calculate, ch, 0, 0, 0, 0);
	t32o_rd_hook = 0;
	on = t32_awb_spec_calculate(&a, ours_rd, 0, ow);

	cases++;
	nwrites += on;
	pick[(par[5704] == 1) + 2 * (par[5709] != 0)]++;
	if (t32o_log.n != on)
		bad = "write count";
	else
		for (i = 0; i < on; i++)
			if (t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val) {
				bad = "write";
				break;
			}
	if (!bad && memcmp(vpar, opar, PARB))
		bad = "par";
	if (!bad && memcmp(vst, ost, STB))
		bad = "state";
	if (!bad)
		return 0;
	if (failed++ < 5) {
		printf("MISMATCH %s: %s (vendor writes %u, ours %u)\n", what, bad, t32o_log.n, on);
		for (i = 0; i < t32o_log.n || i < on; i++)
			if (i >= t32o_log.n || i >= on || t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val) {
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i, i < t32o_log.n ? t32o_log.w[i].reg : 0,
				       i < t32o_log.n ? t32o_log.w[i].val : 0, i < on ? ow[i].reg : 0, i < on ? ow[i].val : 0);
				break;
			}
		for (i = 0; i < PARB; i++)
			if (vpar[i] != opar[i]) { printf("  par+%u vendor %02x ours %02x\n", i, vpar[i], opar[i]); break; }
		for (i = 0; i < STB; i++)
			if (vst[i] != ost[i]) { printf("  st+%u vendor %02x ours %02x\n", i, vst[i], ost[i]); break; }
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
		for (k = 0; k < 40; k++)
			run_case(e->d_name, b + off);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	int nbins;

	for (c = 0; c < n; c++)
		run_case("random", 0);
	rc_r = cases;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_awbp_oracle: random %u, IQ bins %d (%u cases), %u register writes, classes %u %u %u %u, %u mismatches\n",
	       rc_r, nbins, cases - rc_r, nwrites, pick[0], pick[1], pick[2], pick[3], failed);
	(void)nreads;
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbp_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
