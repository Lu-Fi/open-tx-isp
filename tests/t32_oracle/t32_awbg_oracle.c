// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB per-frame leaf 1: our t32_awb_set_gain (driver/t32/
 * tx_isp_t32_awb.h) against the vendor tisp_awb_set_gain (0x3c514, with
 * tisp_awb_gain_reg, fix_point_mult2_32, tisp_reg_write, system_reg_set_awb_trig,
 * system_reg_write) from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc).
 *
 * The vendor context (e16b4[ch] -> { par, state, mode block, hw shadow }) is built
 * with random bytes in all four blocks, the fields the function uses are then set
 * to the interesting values: shift amounts 0..33 (including 0 and above 31), operands
 * at the edges of u32, the mode 0..12, the fixed flag of the hardware shadow, the ct
 * around the 200 step and the band limits, the freeze / update bytes.  Compared: every
 * (reg, val) write in order, and every byte of par, state and hw afterwards (the
 * function writes ct and the history).  Also with section 2 of every *.bin in
 * T32_IQ_DIR as par (random operands and state).
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

static u32 rs = 0x51d7e3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void p16(u8 *p, u32 off, u32 v) { p[off] = (u8)v; p[off + 1] = (u8)(v >> 8); }
static void p32(u8 *p, u32 off, u32 v) { p16(p, off, v); p16(p, off + 2, v >> 16); }
static u32 g32(const u8 *p, u32 off) { return p[off] | (u32)p[off + 1] << 8 | (u32)p[off + 2] << 16 | (u32)p[off + 3] << 24; }

static unsigned int cases, failed, nwrites, nfrz, nmode[16];

static u32 shift_val(void)
{
	static const u32 s[] = { 0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 16, 20, 24, 30, 31, 32, 33, 40 };

	return rnd() % 4 ? s[rnd() % (sizeof(s) / sizeof(s[0]))] : rnd() & 0xffff;
}

static u32 operand(void)
{
	switch (rnd() % 6) {
	case 0: return rnd() % 8;
	case 1: return rnd() & 0xfff;
	case 2: return rnd() & 0xffff;
	case 3: return 0xffffffffU - rnd() % 4;
	case 4: return 0x80000000U + rnd() % 4 - 2;
	default: return rnd();
	}
}

static u32 near(u32 v)
{
	return v + rnd() % 3 - 1;
}

/* images: par from sec (or random), state and hw random, then the used fields */
static void make_case(u8 *par, u8 *stt, u8 *hw, const u8 *sec)
{
	u32 i, ct, lo, hi;

	for (i = 0; i < PARB; i++)
		par[i] = sec ? sec[i] : (u8)rnd();
	memset(stt, 0, STB);
	memset(hw, 0, HWB);
	for (i = 0; i < 4096; i += 4)	/* random bytes around every field the function can touch */
		p32(stt, 28672 + i, rnd());
	for (i = 0; i < 1024; i += 4) {
		p32(stt, 29600 + i, rnd());
		p32(stt, 107392 + i, rnd());
		p32(stt, 119808 + i, rnd());
		p32(hw, 128 + i, rnd());
		p32(hw, 4096 + (i & 0x7f), rnd());
	}
	if (!sec || rnd() % 2) {
		p16(par, 3306, shift_val());
		p16(par, 3308, shift_val());
		if (rnd() % 2) {	/* the realistic range */
			p16(par, 3306, 10 + rnd() % 8);
			p16(par, 3308, 6 + rnd() % 8);
		}
	}
	p32(par, 24, operand());
	p32(par, 28, operand());
	p32(par, 52, operand());
	p32(par, 56, operand());
	ct = rnd() % 3 ? 1500 + rnd() % 9000 : rnd();
	p32(par, 44, ct);
	lo = rnd() % 2 ? near(ct) : 2000 + rnd() % 3000;
	hi = rnd() % 2 ? near(ct) : lo + rnd() % 4000;
	if (rnd() % 2) { u32 t = lo; lo = hi; hi = t; }
	p32(par, 3088, lo);
	p32(par, 3092, hi);
	for (i = 0; i < 6; i++)
		p32(par, 3064 + 4 * i, rnd() % 2 ? 1024 + rnd() % 400 : operand());
	/* state */
	p32(stt, 28760, rnd() % 2 ? near(ct + 200 * (rnd() % 3) - 200) : rnd() % 12000);
	if (rnd() % 4 == 0)
		p32(stt, 28760, ct + 199 + rnd() % 3);
	stt[107596] = rnd() % 3 ? (u8)(rnd() % 3) : (u8)rnd();
	stt[107597] = rnd() % 3 ? 0 : (u8)(rnd() % 3);
	stt[107598] = rnd() % 4 ? (u8)(rnd() % 13) : (u8)rnd();
	p32(stt, 107600, rnd() % 2 ? 2000 + rnd() % 9000 : rnd());
	p16(stt, 107604, rnd() % 2 ? 200 + rnd() % 600 : rnd());
	p16(stt, 107606, rnd() % 2 ? 200 + rnd() % 600 : rnd());
	p32(stt, 119916, rnd() % 2 ? 2000 + rnd() % 9000 : rnd());
	/* hw shadow */
	p16(hw, 4266, rnd() % 3 ? rnd() % 3 : rnd());
	p16(hw, 4268, rnd() % 2 ? rnd() % 2000 : rnd());
	p16(hw, 4270, rnd() % 2 ? rnd() % 2000 : rnd());
	p16(hw, 4272, rnd() % 2 ? 2000 + rnd() % 9000 : rnd());
	if (rnd() % 3 == 0) {	/* aim the 14 bit limit: fixed gains, no offset reselection, off = T + 1024 - 4 * gain */
		static const u32 t[] = { 16381, 16382, 16383, 16384, 16385, 16386, 0, 1, 0xffffffffU, 0xfffffffeU, 4095, 8192 };
		u32 tr = t[rnd() % (sizeof(t) / sizeof(t[0]))], tb = t[rnd() % (sizeof(t) / sizeof(t[0]))];

		stt[107598] = 1 + rnd() % 8;
		stt[107596] = 0;
		p32(stt, 28760, g32(stt, 107600) + rnd() % 200);
		p16(hw, 4266, rnd() % 2 ? 0 : 2);
		p32(stt, 29792, tr + 1024 - 4 * (g32(stt, 107604) & 0xffff));
		p32(stt, 29796, tb + 1024 - 4 * ((g32(stt, 107604) >> 16)));
	}
}

static int run_case(const char *what, const u8 *sec)
{
	static u8 par[PARB], stt[STB], hw[HWB], opar[PARB], ost[STB], ohw[HWB];
	struct t32_stats_write ow[T32_AWB_MAX_WRITES];
	struct t32_awb a = { (struct t32_awb_par *)opar, (struct t32_awb_state *)ost, (struct t32_awb_hw *)ohw };
	u8 *vctx, *vpar, *vst, *vhw;
	u32 ch = rnd() % 4;
	unsigned int on, i;
	const char *bad = 0;

	make_case(par, stt, hw, sec);
	memcpy(opar, par, PARB);
	memcpy(ost, stt, STB);
	memcpy(ohw, hw, HWB);

	t32o_reset();
	vctx = t32o_alloc(32);
	vpar = t32o_alloc(PARB);
	vst = t32o_alloc(STB);
	vhw = t32o_alloc(HWB);
	memcpy(vpar, par, PARB);
	memcpy(vst, stt, STB);
	memcpy(vhw, hw, HWB);
	p32(vctx, 0, T32O_PTR(vpar));
	p32(vctx, 4, T32O_PTR(vst));
	p32(vctx, 12, T32O_PTR(vhw));
	t32_data_e16b4[ch] = T32O_PTR(vctx);

	t32o_call((t32o_fn)(void (*)(void))tisp_awb_set_gain, ch, 0, 0, 0, 0);
	on = t32_awb_set_gain(&a, ow);

	cases++;
	nwrites += on;
	nfrz += !ost[107597] ? 0 : 1;
	nmode[ost[107598] < 15 ? ost[107598] : 15]++;
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
	if (!bad && memcmp(vhw, ohw, HWB))
		bad = "hw";
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
		for (i = 0; i < HWB; i++)
			if (vhw[i] != ohw[i]) { printf("  hw+%u vendor %02x ours %02x\n", i, vhw[i], ohw[i]); break; }
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

	for (c = 0; c < n; c++)
		run_case("random", 0);
	rc_r = cases;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_awbg_oracle: random %u, IQ bins %d (%u cases), %u register writes, frozen %u, modes", rc_r, nbins,
	       cases - rc_r, nwrites, nfrz);
	for (c = 0; c < 11; c++)
		printf(" %u", nmode[c]);
	printf(", %u mismatches\n", failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbg_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
