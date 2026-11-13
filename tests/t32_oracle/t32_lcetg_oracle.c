// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: LCE gain / EV strength.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc: tisp_lce_tgain_interp_strength, tisp_itp_bv,
 * tisp_lce_gauss_wgt, calculate_exponential, tisp_lce_write_curve) and our readable ports
 * from driver/t32/tx_isp_t32_lcetg.h on the same input and compares every byte.
 *
 * interp_strength: the whole LCE state (0x22850 bytes) and the whole section (712 bytes plus
 * 64 guard bytes) after the call, both sides from the same random bytes; gain mode (+507 = 0)
 * and EV mode (other values, descending / random breakpoints, EV below / between / above
 * them), day and WDR weights (+0x22737), the stored weight parameters equal or not (each
 * branch), u16 WDR parameters above 255, sigma 0 (divide by zero).  Channel 0..3 with the
 * other channels' states as decoys (unchanged).
 * itp_bv, calculate_exponential, gauss_wgt (n = 0..5, b = 0): return values / every byte of
 * dst (20 + 4 guard) and the section.  write_curve: the whole curve buffer (23040 + guard).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lcetg.h"

#define SECB	712u
#define SECG	(SECB + 64u)
#define STB	T32_LCETG_STATE_BYTES
#define CVB	(2u * T32_LCETG_CURVE_BYTES)

extern u64 tisp_lce_tgain_interp_strength(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_itp_bv(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
extern u64 tisp_lce_gauss_wgt(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 calculate_exponential(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_lce_write_curve(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x41c3e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static unsigned int compared, n_ev, n_ev_above, n_wdr, n_day_wgt1, n_day_wgt2, n_lpf, n_wgt_zero;

#define FAIL(...) do { printf("MISMATCH " __VA_ARGS__); printf("\n"); \
		       printf("t32_lcetg_oracle: %u compared, 1 mismatch\n", compared); exit(1); } while (0)

static u8 osec[SECG], ost[STB];

/* ---- interp_strength ---- */
static void gen(u8 *sec, u8 *st)
{
	u32 i, mode = rnd() % 4;

	for (i = 0; i < SECG; i++)
		sec[i] = (u8)rnd();
	for (i = 0; i < STB; i++)
		st[i] = (u8)(rnd() >> 7);
	sec[507] = mode == 0 ? 0 : mode == 1 ? 1 : (u8)rnd();
	if (rnd() % 3) {	/* descending EV breakpoints (as the vendor's tables are) */
		u32 b = 0x2000000u + rnd() % 0x8000000u;

		for (i = 0; i < 11; i++) {
			put32(sec + 4 + 4 * i, b);
			b -= rnd() % 3 ? rnd() % (b / 8 + 1) : 0;
		}
	}
	/* gain: index 0..12, EV near a breakpoint or random */
	put32(st + T32_LCETG_LAST_GAIN, rnd() % 5 ? (rnd() % 13) << 16 | (rnd() & 0xffff) : rnd());
	{
		u32 j = rnd() % 11, bp = sec[4 + 4 * j] | sec[5 + 4 * j] << 8 | sec[6 + 4 * j] << 16 | (u32)sec[7 + 4 * j] << 24;
		u32 ev = rnd() % 4 == 0 ? rnd() : rnd() % 4 == 0 ? 0 : rnd() % 4 == 0 ? 0xffffffffu : bp + (rnd() % 64) - 32;

		put32(st + T32_LCETG_LAST_EV, ev);
	}
	st[T32_LCETG_SEL] = rnd() % 2 ? 0 : (u8)(1 + rnd() % 255);
	/* day: stored strengths equal to the section's (no refresh) or not */
	if (rnd() % 2) {
		memcpy(st + T32_LCETG_GPAR + 155, sec + 435, 6);
		if (rnd() % 2)
			st[T32_LCETG_GPAR + 155 + rnd() % 6] ^= 1;
	}
	if (rnd() % 2)
		st[T32_LCETG_GPAR + 154] = sec[432];
	if (rnd() % 8 == 0)
		sec[432] = (u8)(rnd() % 10);	/* small / zero sigma */
	/* WDR: u16 triples, stored bytes equal or not, values > 255 */
	for (i = 0; i < 12; i++) {
		u32 v = rnd() % 6 == 0 ? rnd() & 0xffff : rnd() & 0xff;

		sec[48 + 2 * i] = (u8)v;
		sec[49 + 2 * i] = (u8)(v >> 8);
		st[T32_LCETG_GPAR + 342 + i] = rnd() % 3 ? (u8)v : (u8)rnd();
	}
	/* Gauss sources: b = 0 sometimes */
	if (rnd() % 10 == 0)
		sec[436] = 0;
}

static void case_strength(void)
{
	u8 *vsec, *vst[4];
	u32 ch = rnd() % 4, i, k;

	t32o_reset();
	vsec = t32o_alloc(SECG);
	for (k = 0; k < 4; k++) {
		vst[k] = t32o_alloc(STB);
		if (!vst[k] || !vsec) {
			fprintf(stderr, "t32_lcetg_oracle: pool exhausted\n");
			exit(2);
		}
	}
	gen(osec, ost);
	memcpy(vsec, osec, SECG);
	for (k = 0; k < 4; k++) {
		if (k == ch) {
			memcpy(vst[k], ost, STB);
		} else {
			for (i = 0; i < STB; i++)
				vst[k][i] = (u8)(i * 7 + k);
			put32(vst[k], T32O_PTR(vsec));
		}
		t32_data_e1da8[k] = T32O_PTR(vst[k]);
	}
	put32(vst[ch], T32O_PTR(vsec));
	put32(ost, T32O_PTR(vsec));	/* the pointer word compares equal; ours does not use it */
	if (osec[507]) {
		n_ev++;
		for (i = 0; i <= 10; i++)
			if (t32_lcetg_rd32(osec + 4 + 4 * i) < t32_lcetg_rd32(ost + T32_LCETG_LAST_EV))
				break;
		n_ev_above += i > 10;
	}
	if (ost[T32_LCETG_SEL])
		n_wdr++;
	t32o_call((t32o_fn)(void (*)(void))tisp_lce_tgain_interp_strength, ch, 0, 0, 0, 0);
	if (!ost[T32_LCETG_SEL]) {
		u8 g[6];

		memcpy(g, ost + T32_LCETG_GPAR + 155, 6);
		t32_lce_tgain_interp_strength(ost, osec);
		n_day_wgt1 += memcmp(g, osec + 435, 3) != 0;
		n_day_wgt2 += memcmp(g + 3, osec + 438, 3) != 0;
		n_lpf++;
	} else {
		t32_lce_tgain_interp_strength(ost, osec);
	}
	compared++;
	for (i = 0; i < SECG; i++)
		if (vsec[i] != osec[i])
			FAIL("strength section byte %u vendor %02x ours %02x (mode %u sel %u)", i, vsec[i], osec[i],
			     osec[507], ost[T32_LCETG_SEL]);
	for (i = 0; i < STB; i++)
		if (vst[ch][i] != ost[i])
			FAIL("strength state byte %#x vendor %02x ours %02x (mode %u sel %u)", i, vst[ch][i], ost[i],
			     osec[507], ost[T32_LCETG_SEL]);
	for (k = 0; k < 4; k++)
		if (k != ch)
			for (i = 4; i < STB; i++)
				if (vst[k][i] != (u8)(i * 7 + k))
					FAIL("strength decoy channel %u byte %#x changed", k, i);
}

/* ---- itp_bv, calculate_exponential ---- */
static void case_small(void)
{
	u32 x = rnd(), hi = rnd(), lo = rnd(), yh = rnd() & 0xffff, yl = rnd() & 0xffff, r, o;

	switch (rnd() % 4) {
	case 0:
		lo = hi - rnd() % 1000;
		x = lo + rnd() % 1200;
		break;
	case 1:
		lo = hi;
		break;
	case 2:
		x = rnd() % 2 ? hi : lo;
		break;
	default:
		break;
	}
	t32o_reset();
	r = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_itp_bv, x, hi, lo, yh, yl);
	o = t32_itp_bv(x, hi, lo, yh, yl);
	compared++;
	if (r != o)
		FAIL("itp_bv(%u, %u, %u, %u, %u) vendor %u ours %u", x, hi, lo, yh, yl, r, o);
	{
		u32 n = rnd() % 4 ? 10 : rnd() % 20, a = rnd() % 4 ? 12 : rnd() % 16, q = rnd() % 4 ? 12 : rnd() % 16;
		u32 v = rnd() % 3 ? rnd() % 0x20000 : rnd();

		if (a < q)
			a = q;
		r = (u32)t32o_call((t32o_fn)(void (*)(void))calculate_exponential, v, n, a, q, 0);
		o = t32_calc_exp(v, n, a, q);
		compared++;
		if (r != o)
			FAIL("calculate_exponential(%u, %u, %u, %u) vendor %08x ours %08x", v, n, a, q, r, o);
	}
}

/* ---- gauss_wgt (ch 0, section via the state's first word) ---- */
static void case_wgt(void)
{
	u8 *vsec, *vst, *vsrc, *vdst, src[3], dst[24];
	u32 n = rnd() % 6, i;

	t32o_reset();
	vsec = t32o_alloc(SECG);
	vst = t32o_alloc(64);
	vsrc = t32o_alloc(64);
	vdst = t32o_alloc(64);
	for (i = 0; i < SECG; i++)
		osec[i] = vsec[i] = (u8)rnd();
	for (i = 0; i < 3; i++)
		src[i] = vsrc[i] = (u8)rnd();
	if (rnd() % 8 == 0) {
		src[1] = vsrc[1] = 0;
		n_wgt_zero++;
	}
	for (i = 0; i < 24; i++)
		dst[i] = vdst[i] = 0xa5;
	put32(vst, T32O_PTR(vsec));
	t32_data_e1da8[0] = T32O_PTR(vst);
	t32o_call((t32o_fn)(void (*)(void))tisp_lce_gauss_wgt, 0, T32O_PTR(vsrc), T32O_PTR(vdst), n | (rnd() & 0x300), 0);
	t32_lce_gauss_wgt(osec, src, dst, n);
	compared++;
	for (i = 0; i < 24; i++)
		if (vdst[i] != dst[i])
			FAIL("gauss_wgt(%u %u %u, n %u) dst[%u] vendor %02x ours %02x", src[0], src[1], src[2], n, i, vdst[i], dst[i]);
	for (i = 0; i < SECG; i++)
		if (vsec[i] != osec[i])
			FAIL("gauss_wgt n %u section byte %u vendor %02x ours %02x", n, i, vsec[i], osec[i]);
}

/* ---- write_curve ---- */
static u8 ocv[CVB + 64];
static void case_curve(void)
{
	u8 *vst, *vcv;
	u32 ch = rnd() % 4, i;

	t32o_reset();
	vst = t32o_alloc(STB);
	vcv = t32o_alloc(CVB + 64);
	for (i = 0; i < STB; i++)
		ost[i] = vst[i] = (u8)rnd();
	for (i = 0; i < CVB + 64; i++)
		ocv[i] = vcv[i] = (u8)(i * 13);
	t32_data_e1da8[ch] = T32O_PTR(vst);
	put32(t32_data_e1db8 + 36 * ch + 12, T32O_PTR(vcv));
	t32o_call((t32o_fn)(void (*)(void))tisp_lce_write_curve, ch, 0, 0, 0, 0);
	t32_lce_write_curve(ost, ocv);
	compared++;
	for (i = 0; i < CVB + 64; i++)
		if (vcv[i] != ocv[i])
			FAIL("write_curve buffer byte %u vendor %02x ours %02x", i, vcv[i], ocv[i]);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		case_strength();
		case_small();
		case_wgt();
		if (c % 16 == 0)
			case_curve();
	}
	printf("t32_lcetg_oracle: %u compared (EV %u, above all %u, WDR %u, day wgt1 %u wgt2 %u, lpf %u, wgt b=0 %u), 0 mismatches\n",
	       compared, n_ev, n_ev_above, n_wdr, n_day_wgt1, n_day_wgt2, n_lpf, n_wgt_zero);
	return 0;
}
