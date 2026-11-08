// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB per-frame leaf 3: our t32_awb_long_par_update (driver/t32/
 * tx_isp_t32_awb.h) against the vendor tisp_awb_long_par_update (0x3d158) from Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * The vendor fills a 192 byte pointer table (lp) besides the data it computes.
 * Compared: lp slot 16 (parameter table choice, as offset in par) with ours, the
 * constant slots 0/4/8/12 against their field addresses, every other written slot
 * lies inside par / state / hw; and every byte of par, state afterwards.
 * Inputs aim at every branch: ev around ev_lo, ev_lo + span, ev_hi, ev_hi + span (+-1),
 * reversed ranges, ids 0/1/other for the outer and inner id (low 16 bits), weights
 * around 0x10000, shift 0..33, span 0.  Also with section 2 of every *.bin in
 * T32_IQ_DIR as par.
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
#define HWB	sizeof(((struct t32_awb_hw *)0)->raw)

static u32 rs = 0x2f19c7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void p16(u8 *p, u32 off, u32 v) { p[off] = (u8)v; p[off + 1] = (u8)(v >> 8); }
static void p32(u8 *p, u32 off, u32 v) { p16(p, off, v); p16(p, off + 2, v >> 16); }
static u32 g32(const u8 *p, u32 off) { return p[off] | (u32)p[off + 1] << 8 | (u32)p[off + 2] << 16 | (u32)p[off + 3] << 24; }

static unsigned int cases, failed, path[8];

static u32 id_val(void)
{
	u32 hi16 = rnd() % 3 ? 0 : (rnd() & 0xffff) << 16;

	switch (rnd() % 4) {
	case 0: return hi16;
	case 1: return hi16 | 1;
	case 2: return hi16 | (2 + rnd() % 3);
	default: return hi16 | (rnd() & 0xffff);
	}
}

static u32 weight(void)
{
	switch (rnd() % 4) {
	case 0: return 0x10000 + rnd() % 3 - 1;
	case 1: return rnd() & 0xffff;
	case 2: return rnd() % 8;
	default: return rnd();
	}
}

static int run_case(const char *what, const u8 *sec)
{
	static u8 par[PARB], stt[STB], hw[HWB], opar[PARB], ost[STB];
	u8 *vctx, *vpar, *vst, *vhw, *vlp;
	struct t32_awb a = { (struct t32_awb_par *)opar, (struct t32_awb_state *)ost, 0 };
	struct t32_awb_lp olp;
	u32 lo, hi, ev, span, ch = rnd() % 4, i, pick, tbl;
	const char *bad = 0;

	memset(par, 0, PARB);
	if (sec)
		memcpy(par, sec, PARB);
	else
		for (i = 0; i < PARB; i++)
			par[i] = (u8)rnd();
	for (i = 0; i < 1024; i += 4)
		p32(stt, 28672 + i, rnd());
	memset(stt, 0, 28672);
	memset(stt + 29696, 0, STB - 29696);
	for (i = 0; i < 512; i += 4)
		p32(stt, 37888 + i, rnd());
	lo = rnd() % 2 ? rnd() % 3000 : rnd();
	hi = rnd() % 5 ? lo + rnd() % 9000 : rnd() % 3000;
	if (rnd() % 8 == 0)
		hi = lo + 3 * (rnd() % 3);
	if (!sec || rnd() % 2) {
		p32(par, 32, lo);
		p32(par, 36, hi);
		p32(par, 228, id_val());
		p32(par, 232, weight());
		p32(par, 236, id_val());
		p32(par, 240, weight());
		p16(par, 3308, rnd() % 3 ? rnd() % 4 : rnd() % 36);
	}
	lo = g32(par, 32);
	hi = g32(par, 36);
	span = (u32)((int)(hi - lo) / 3);
	switch (rnd() % 10) {
	case 0: ev = lo + rnd() % 3 - 1; break;
	case 1: ev = lo + span + rnd() % 3 - 1; break;
	case 2: ev = hi + rnd() % 3 - 1; break;
	case 3: ev = hi + span + rnd() % 3 - 1; break;
	case 4: ev = lo + (rnd() % (span + 1)); break;
	case 5: ev = hi + (rnd() % (span + 1)); break;
	case 6: ev = rnd(); break;
	default: ev = lo + rnd() % (hi - lo + 2 * span + 8) - 4; break;
	}
	p32(stt, 107588, ev);
	for (i = 0; i < 4096; i += 4)
		p32(stt, 107392 + (i & 0x3ff), rnd() % 2 ? 0 : g32(stt, 107392));
	p32(stt, 107588, ev);
	memcpy(opar, par, PARB);
	memcpy(ost, stt, STB);
	memcpy(hw, stt, 0);

	t32o_reset();
	vctx = t32o_alloc(32);
	vpar = t32o_alloc(PARB);
	vst = t32o_alloc(STB);
	vhw = t32o_alloc(HWB);
	vlp = t32o_alloc(256);
	memcpy(vpar, par, PARB);
	memcpy(vst, stt, STB);
	p32(vctx, 0, T32O_PTR(vpar));
	p32(vctx, 4, T32O_PTR(vst));
	p32(vctx, 12, T32O_PTR(vhw));
	t32_data_e16b4[ch] = T32O_PTR(vctx);

	t32o_call((t32o_fn)(void (*)(void))tisp_awb_long_par_update, ch, T32O_PTR(vlp), 0, 0, 0);
	t32_awb_long_par_update(&a, &olp);

	cases++;
	pick = (ev < lo) + 2 * (ev >= hi) + 4 * (g32(par, 228) & 1);
	path[pick & 7]++;
	tbl = g32(vlp, 16) - T32O_PTR(vpar);
	if (tbl != olp.tbl)
		bad = "table choice";
	else if (g32(vlp, 0) != T32O_PTR(vpar) + 3322 || g32(vlp, 4) != T32O_PTR(vst) + 28688 ||
		 g32(vlp, 8) != T32O_PTR(vst) + 38032 || g32(vlp, 12) != T32O_PTR(vst) + 38048)
		bad = "constant slot";
	else {
		for (i = 20; i <= 188; i += 4) {
			u32 v = g32(vlp, i);

			if (v && !((v >= T32O_PTR(vpar) && v < T32O_PTR(vpar) + PARB) ||
				   (v >= T32O_PTR(vst) && v < T32O_PTR(vst) + STB) ||
				   (v >= T32O_PTR(vhw) && v < T32O_PTR(vhw) + HWB + 4300))) {
				bad = "slot outside";
				break;
			}
		}
	}
	if (!bad && memcmp(vpar, opar, PARB))
		bad = "par";
	if (!bad && memcmp(vst, ost, STB))
		bad = "state";
	if (!bad)
		return 0;
	if (failed++ < 5) {
		printf("MISMATCH %s: %s (ev %u lo %u hi %u span %d id %08x/%08x)\n", what, bad, ev, lo, hi, (int)span,
		       g32(par, 228), g32(par, 236));
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
	printf("t32_awbl_oracle: random %u, IQ bins %d (%u cases), paths", rc_r, nbins, cases - rc_r);
	for (c = 0; c < 8; c++)
		printf(" %u", path[c]);
	printf(", %u mismatches\n", failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbl_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
