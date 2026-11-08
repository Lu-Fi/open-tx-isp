// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB per-frame leaf 2: our t32_awb_sat_weight (driver/t32/
 * tx_isp_t32_awb.h) against the vendor tisp_awb_sat_weight (0x3d6f4) from Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * The vendor reads its parameters through e16b4[ch] -> par; both sides get the same
 * section image, the same zone tables (stat a / b, limit, ref value) and gains, and
 * an output table pre-filled with a marker.  Inputs: grid sizes 0..40 x 0..40 and
 * rare big ones (so that the 16 bit zone number wraps), shift 0..33, mode 0/1/2,
 * zones with limit 0, ref value at thr -1/0/+1, products around zero and the u32
 * edges, divisors that wrap to 0.  Compared: the whole output table (so stray
 * writes show too).  Also with section 2 of every *.bin in T32_IQ_DIR as par
 * (shift, grid, mode from the bin).
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
#define ZN	65536u

static u32 rs = 0x7a3c15u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void p16(u8 *p, u32 off, u32 v) { p[off] = (u8)v; p[off + 1] = (u8)(v >> 8); }
static void p32(u8 *p, u32 off, u32 v) { p16(p, off, v); p16(p, off + 2, v >> 16); }

static unsigned int cases, failed, hit_div0, hit_one, hit_w, hit_mode[3];

static u32 val(void)
{
	switch (rnd() % 6) {
	case 0: return rnd() % 4;
	case 1: return rnd() & 0xffff;
	case 2: return rnd() & 0xfffff;
	case 3: return 0xffffffffU - rnd() % 3;
	case 4: return rnd() & 0xff;
	default: return rnd();
	}
}

static int run_case(const char *what, const u8 *sec)
{
	static u8 par[PARB], opar[PARB];
	static u32 sa[ZN], sb[ZN], lim[ZN], ref[ZN], ow[ZN];
	u8 *vctx, *vpar;
	u32 *va, *vl, *vb, *vr, *vo, *vg, gain[2], g2[2], thr, h, w, sh, n, i, ch = rnd() % 4;
	const char *bad = 0;

	memset(par, 0, PARB);
	if (sec)
		memcpy(par, sec, PARB);
	h = rnd() % 8 ? rnd() % 41 : 1 + rnd() % 300;
	w = rnd() % 8 ? rnd() % 41 : 1 + rnd() % 600;
	sh = rnd() % 3 ? 8 + rnd() % 10 : rnd() % 36;
	thr = rnd() % 2 ? rnd() % 300 : val();
	if (!sec || rnd() % 2) {
		p16(par, 3186, h);
		p16(par, 3190, w);
		p16(par, 3306, sh);
		p16(par, 3438, rnd() % 4 == 0 ? rnd() % 3 : 1 - (rnd() & 1) + (rnd() % 8 == 0 ? 1 : 0));
		p32(par, 4, thr);
	}
	thr = par[4] | par[5] << 8 | par[6] << 16 | (u32)par[7] << 24;
	n = ZN;
	for (i = 0; i < n; i++) {
		int small = rnd() % 4;

		sa[i] = small ? (rnd() & 0xffff) : val();
		sb[i] = small ? (rnd() & 0xffff) : val();
		lim[i] = rnd() % 6 == 0 ? 0 : (small ? (rnd() & 0xfffff) : val());
		ref[i] = thr + rnd() % 3 - 1 + (rnd() % 3 ? 0 : val());
		ow[i] = 0xa5a5a5a5U;
	}
	gain[0] = rnd() % 2 ? 128 + rnd() % 4096 : val();
	gain[1] = rnd() % 2 ? 128 + rnd() % 4096 : val();
	memcpy(opar, par, PARB);

	t32o_reset();
	vctx = t32o_alloc(32);
	vpar = t32o_alloc(PARB);
	va = t32o_alloc(ZN * 4); vl = t32o_alloc(ZN * 4); vb = t32o_alloc(ZN * 4);
	vr = t32o_alloc(ZN * 4); vo = t32o_alloc(ZN * 4); vg = t32o_alloc(8);
	if (!vpar || !va || !vl || !vb || !vr || !vo) {
		printf("oracle pool too small\n");
		exit(1);
	}
	memcpy(vpar, par, PARB);
	memcpy(va, sa, ZN * 4); memcpy(vl, lim, ZN * 4); memcpy(vb, sb, ZN * 4);
	memcpy(vr, ref, ZN * 4); memcpy(vo, ow, ZN * 4);
	vg[0] = gain[0]; vg[1] = gain[1];
	p32(vctx, 0, T32O_PTR(vpar));
	t32_data_e16b4[ch] = T32O_PTR(vctx);

	t32o_arg5 = T32O_PTR(vo);
	t32o_arg6 = T32O_PTR(vg);
	t32o_call((t32o_fn)(void (*)(void))tisp_awb_sat_weight, ch, T32O_PTR(va), T32O_PTR(vl), T32O_PTR(vb), T32O_PTR(vr));
	g2[0] = gain[0]; g2[1] = gain[1];
	t32_awb_sat_weight((struct t32_awb_par *)opar, sa, lim, sb, ref, ow, g2);

	cases++;
	hit_mode[((struct t32_awb_par *)opar)->sat_mode < 3 ? ((struct t32_awb_par *)opar)->sat_mode : 2]++;
	hit_w += (opar[3186] | opar[3187] << 8) * (opar[3190] | opar[3191] << 8) > 0;
	for (i = 0; i < ZN; i++) {
		hit_one += ow[i] == 1;
		hit_div0 += ow[i] == 0xffffffffU;
	}
	if (memcmp(vo, ow, ZN * 4))
		bad = "weights";
	else if (memcmp(vpar, opar, PARB))
		bad = "par";
	if (!bad)
		return 0;
	if (failed++ < 5) {
		printf("MISMATCH %s: %s (grid %ux%u, shift %u)\n", what, bad, h, w, sh);
		for (i = 0; i < ZN; i++)
			if (vo[i] != ow[i]) {
				printf("  g %08x %08x mode %u thr %08x zone %u vendor %08x ours %08x (a %08x b %08x lim %08x ref %08x)\n", gain[0], gain[1], opar[3438], thr, i, vo[i], ow[i], sa[i],
				       sb[i], lim[i], ref[i]);
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
		u32 off, len = 0, k;

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
		for (k = 0; k < 4; k++)
			run_case(e->d_name, b + off);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 1500;
	int nbins;

	for (c = 0; c < n; c++)
		run_case("random", 0);
	rc_r = cases;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_awbs_oracle: random %u, IQ bins %d (%u cases), weight 1 in %u, 0xffffffff in %u, modes %u/%u/%u, %u mismatches\n",
	       rc_r, nbins, cases - rc_r, hit_one, hit_div0, hit_mode[0], hit_mode[1], hit_mode[2], failed);
	(void)hit_w;
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbs_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
