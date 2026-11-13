// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB colour temperature 4: our t32_awb_grayworld / _mode
 * (driver/t32/tx_isp_t32_awbct.h) against the vendor Tiziano_Awb_Ct_Detect_GrayWorld
 * (0x20908) and _mode (0x20360), with Tiziano_Awb_Ct_Cal, ISPAWBInterpolation1/2 and the
 * fix_point helpers, from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc).  The vendor takes a table of pointers into par / state (slots 20, 24, 28,
 * 32, 36, 44, 48, 52, 56, 172); the oracle builds it.  Inputs: zone grids to 32 x 32,
 * statistics around and outside the r/b windows, flags 0..3 and random, modes (en bytes
 * 0/1/other, refine on/off), shifts, valid flag 0/1/other, sums that are zero.
 * Compared: the output pair, the valid flag, every byte of par and state.  Also with
 * section 2 of every *.bin in T32_IQ_DIR as par.
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
#define STB	sizeof(((struct t32_awb_state *)0)->raw)
#define SN	4096u

static u32 rs = 0x4a5b6cu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void p16(u8 *p, u32 off, u32 v) { p[off] = (u8)v; p[off + 1] = (u8)(v >> 8); }
static void p32(u8 *p, u32 off, u32 v) { p16(p, off, v); p16(p, off + 2, v >> 16); }
static u32 g32(const u8 *p, u32 off) { return p[off] | (u32)p[off + 1] << 8 | (u32)p[off + 2] << 16 | (u32)p[off + 3] << 24; }


/* (dr, db) pairs with dr^2 + db^2 = t for the class thresholds +-1 */
static unsigned int npair[1024];
static u8 pdr[1024][8], pdb[1024][8];

static void pairs_init(void)
{
	static const u32 thr[] = { 514, 540, 572, 615, 679, 818 };
	unsigned int k, d;
	u32 dr, db;

	(void)thr;
	for (k = 0; k < 1; k++)
		for (d = 480; d < 840; d++)
			for (dr = 0; dr < 30; dr++)
				for (db = 0; db < 30; db++)
					if (dr * dr + db * db == d && npair[d] < 8) {
						pdr[d][npair[d]] = (u8)dr;
						pdb[d][npair[d]++] = (u8)db;
					}
}

/* a grid of equal zones with a few outliers at chosen squared distances */
static void targeted(u8 *par, u32 *stats)
{
	static const u32 thr[] = { 514, 540, 572, 615, 679, 818 };
	u32 os = 8 + rnd() % 3, v = 250 + rnd() % 100, w = 250 + rnd() % 100, i, k;

	for (i = 0; i < 15; i++) {
		p32(par, 60 + 4 * i, 100 + 40 * i);
		p32(par, 120 + 4 * i, 100 + 40 * i);
	}
	p16(par, 3306, 10);
	p16(par, 3308, os);
	p16(par, 3186, 32);
	p16(par, 3190, 32);
	p16(par, 3448, 1);
	p16(par, 3450, rnd() % 2);
	p16(par, 3452, 1);
	for (i = 0; i < 1024; i++)
		par[5714 + i] = 1;
	for (i = 0; i < 4096; i++)
		stats[i] = (i % 2048 >= 1024 ? w : v) << os;
	for (k = 0; k < 3; k++) {
		u32 t = rnd() % 3 ? 480 + rnd() % 360 : thr[rnd() % 6] - 1 + rnd() % 3, n = npair[t], z = rnd() % 1024, sg = rnd() % 4, m = rnd() % 2;

		if (!n)
			continue;
		n = rnd() % n;
		stats[m * 2048 + z] = (sg & 1 ? v - pdr[t][n] : v + pdr[t][n]) << os;
		stats[m * 2048 + 1024 + z] = (sg & 2 ? w - pdb[t][n] : w + pdb[t][n]) << os;
	}
}

static unsigned int cases, failed, nvalid[2], nrefine, nboth;

static int run_case(const char *what, const u8 *sec)
{
	static u8 par[PARB], opar[PARB], stt[STB], ost[STB];
	static u32 stats[SN];
	u32 ovalid, vvalid_init, out_o[2] = { 0x11111111, 0x22222222 }, *vout, *vvalid, *vstats;
	u8 *vlp, *vpar, *vst;
	u32 i, s_os, rows, cols;
	const char *bad = 0;

	if (sec)
		memcpy(par, sec, PARB);
	else
		for (i = 0; i < PARB; i++)
			par[i] = (u8)rnd();
	if (!sec || rnd() % 2) {
		u32 x = rnd() % 100, y = rnd() % 100;

		for (i = 0; i < 15; i++) {
			x += 1 + rnd() % 200;
			y += 1 + rnd() % 200;
			p32(par, 60 + 4 * i, x);
			p32(par, 120 + 4 * i, y);
		}
		for (i = 0; i < 225; i++)
			p32(par, 2164 + 4 * i, 200 + rnd() % 6000);
		p16(par, 3306, 8 + rnd() % 6);
		p16(par, 3308, rnd() % 4 ? 6 + rnd() % 6 : rnd() % 40);
		p16(par, 3186, rnd() % 8 ? 1 + rnd() % 32 : rnd() % 33);
		p16(par, 3190, rnd() % 8 ? 1 + rnd() % 32 : rnd() % 33);
	} else {
		p16(par, 3186, rnd() % 33);
		p16(par, 3190, rnd() % 33);
	}
	p16(par, 3448, rnd() % 3 ? 1 : rnd() % 3);
	p16(par, 3450, rnd() % 3 ? 1 : rnd() % 3);
	p16(par, 3452, rnd() % 3 ? 1 : rnd() % 3);
	for (i = 0; i < 1024; i++)
		par[5714 + i] = rnd() % 6 == 0 ? (u8)rnd() : (u8)(rnd() % 4);
	rows = par[3186] | par[3187] << 8;
	cols = par[3190] | par[3191] << 8;
	s_os = (par[3308] | par[3309] << 8) & 31;
	{
		u32 lr = g32(par, 60) << s_os, hr = g32(par, 60 + 56) << s_os, lb = g32(par, 120) << s_os, hb = g32(par, 120 + 56) << s_os;

		for (i = 0; i < SN; i++) {
			int win = rnd() % 4;

			if (i % 2048 >= 1024) {
				stats[i] = win ? (lb + rnd() % (hb - lb + 2)) : rnd() % 3 ? rnd() % (hb + 50) : rnd();
				if (rnd() % 16 == 0)
					stats[i] = hb + rnd() % 3 - 1;
			} else {
				stats[i] = win ? (lr + rnd() % (hr - lr + 2)) : rnd() % 3 ? rnd() % (hr + 50) : rnd();
				if (rnd() % 16 == 0)
					stats[i] = lr + rnd() % 3 - 1;
			}
		}
	}
	if (rnd() % 20 == 0)
		memset(stats, 0, sizeof(stats));
	if (!sec && rnd() % 5 == 0) {
		targeted(par, stats);
		rows = 32;
		cols = 32;
	}
	memset(stt, 0, STB);
	for (i = 0; i < 514; i++)
		p16(stt, 28764 + 2 * i, rnd() % 40);
	for (i = 0; i < 4096; i++)
		stt[87108 + i] = (u8)rnd();
	ovalid = rnd() % 4 ? 1 : rnd() % 3;
	vvalid_init = ovalid;
	memcpy(opar, par, PARB);
	memcpy(ost, stt, STB);

	t32o_reset();
	vlp = t32o_alloc(256);
	vpar = t32o_alloc(PARB);
	vst = t32o_alloc(STB);
	vstats = t32o_alloc(SN * 4);
	vout = t32o_alloc(16);
	vvalid = t32o_alloc(8);
	memcpy(vpar, par, PARB);
	memcpy(vst, stt, STB);
	memcpy(vstats, stats, SN * 4);
	vout[0] = out_o[0]; vout[1] = out_o[1];
	vvalid[0] = vvalid_init;
	p32(vlp, 20, T32O_PTR(vpar) + 3184);
	p32(vlp, 24, T32O_PTR(vpar) + 60);
	p32(vlp, 28, T32O_PTR(vpar) + 120);
	p32(vlp, 32, T32O_PTR(vpar) + 2164);
	p32(vlp, 36, T32O_PTR(vpar) + 5714);
	p32(vlp, 44, T32O_PTR(vpar) + 44);
	p32(vlp, 48, T32O_PTR(vst) + 28764);
	p32(vlp, 52, T32O_PTR(vpar) + 3306);
	p32(vlp, 56, T32O_PTR(vpar) + 3448);
	p32(vlp, 172, T32O_PTR(vst) + 87108);
	t32o_call((t32o_fn)(void (*)(void))Tiziano_Awb_Ct_Detect_GrayWorld, T32O_PTR(vstats), 0, T32O_PTR(vlp), T32O_PTR(vout),
		  T32O_PTR(vvalid));
	t32_awb_grayworld((struct t32_awb_par *)opar, (struct t32_awb_state *)ost, stats, &ovalid, out_o);

	cases++;
	nvalid[ovalid == 1]++;
	nrefine += g32(par, 3448) == 0x10001 && par[3452] == 1;
	(void)rows; (void)cols; (void)nboth;
	if (vout[0] != out_o[0] || vout[1] != out_o[1])
		bad = "output";
	else if (vvalid[0] != ovalid)
		bad = "valid";
	else if (memcmp(vpar, opar, PARB))
		bad = "par";
	else if (memcmp(vst, ost, STB))
		bad = "state";
	if (!bad)
		return 0;
	if (failed++ < 5) {
		printf("MISMATCH %s: %s (grid %ux%u en %u %u %u valid-in %u; vendor out %u %u valid %u, ours %u %u %u)\n", what, bad, rows,
		       cols, par[3448], par[3450], par[3452], vvalid_init, vout[0], vout[1], vvalid[0], out_o[0], out_o[1], ovalid);
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
	unsigned int c, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 8000;
	int nbins;

	pairs_init();
	for (c = 0; c < n; c++)
		run_case("random", 0);
	rc_r = cases;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_awbw_oracle: random %u, IQ bins %d (%u cases), valid out 0/1 %u/%u, refine %u, %u mismatches\n", rc_r, nbins,
	       cases - rc_r, nvalid[0], nvalid[1], nrefine, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbw_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
