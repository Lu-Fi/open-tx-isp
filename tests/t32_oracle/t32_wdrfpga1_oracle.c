// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, WDR fpga phase 1: our readable t32_wdr_deghost_fpga
 * (driver/t32/tx_isp_t32_wdrfpga.h) against the unmodified vendor
 * Tiziano_wdr_deghost_fpga (Matt Davis' recovered C, PR #15 @ 2e2e06bc) with
 * its vendor callees (exptime_update, degweight_ev, getVar, interplate).
 *
 * Per case 4 channel records in one canary framed arena (parameter block,
 * extension, hw image, work area + 64 canary bytes each), random contents
 * with the branch inputs forced to their edges (mode 0 / 1 / other, variance
 * knots around the getVar result, exposure around the 4 knots, zero weights);
 * the arena is compared byte for byte, every kmalloc (size, flags, contents)
 * and kfree in order.  ext[0] = 0 with mode 0 (vendor divide by zero) is not
 * fed.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_wdrfpga.h"

#define WEAK __attribute__((weak))
#define FR	64u
#define CAN	0xc5
#define ARN	(1u << 16)
#define NEV	32

WEAK u32 t32_data_e3b40[4];
WEAK u8 t32_data_e3a9a[4];
/* data.c:5956 .. 6036 (t32_data_c2c84, 81 words) */
u32 t32_data_c2c84[81] = {
	0, 15, 31, 47, 63, 79, 95, 111, 127, 143, 159, 175, 191, 207, 223, 239, 255, 287, 319, 351, 383, 415, 447,
	479, 511, 575, 639, 703, 767, 831, 895, 959, 1023, 1087, 1151, 1215, 1279, 1343, 1407, 1471, 1535, 1599,
	1633, 1727, 1791, 1855, 1919, 1983, 2047, 2111, 2175, 2239, 2303, 2367, 2431, 2495, 2559, 2623, 2687, 2751,
	2815, 2879, 2943, 3007, 3071, 3135, 3199, 3263, 3327, 3391, 3455, 3519, 3583, 3647, 3711, 3775, 3839, 3903,
	3967, 4031, 4095,
};

u64 Tiziano_wdr_deghost_fpga(u32, u32, u32, u32);
u64 tisp_wdr_getVar(u32, u32, u32, u32);

static u32 rs = 0x6d2b79f5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rb(u32 n) { return rnd() % n; }

/* vendor side: kmalloc / kfree log */
static u32 VK[NEV][2], VF[NEV], NVK, NVF;
static u8 *VAL[NEV];
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u8 *p = t32o_alloc(a0);

	(void)a2; (void)a3;
	if (p)
		memset(p, 0xe7, a0);
	if (NVK < NEV) { VK[NVK][0] = a0; VK[NVK][1] = a1; VAL[NVK] = p; }
	NVK++;
	return T32O_PTR(p);
}
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (NVF < NEV) VF[NVF] = a0;
	NVF++;
	return 0;
}

/* our side */
static u8 OH[1u << 14] __attribute__((aligned(64)));
static u32 ohu, OK[NEV][2], NOK, NOF;
static u8 *OAL[NEV];
static uintptr_t OF[NEV];
static u8 *o_kmalloc(u32 size, u32 gfp)
{
	u8 *p;

	ohu = (ohu + 63u) & ~63u;
	p = OH + ohu;
	ohu += size;
	memset(p, 0xe7, size);
	if (NOK < NEV) { OK[NOK][0] = size; OK[NOK][1] = gfp; OAL[NOK] = p; }
	NOK++;
	return p;
}
static void o_kfree(void *p)
{
	u32 i;
	uintptr_t v = (uintptr_t)p;

	for (i = 0; i < NOK && i < NEV && i < NVK; i++)
		if (p == OAL[i])
			v = (uintptr_t)VAL[i];
	if (NOF < NEV) OF[NOF] = v;
	NOF++;
}
static const struct t32_wdr_ops OPS = { .kmalloc = o_kmalloc, .kfree = o_kfree };

static u8 IMG[ARN], OA[ARN] __attribute__((aligned(64)));
static u8 *VA;
static u32 *VREC[4];
static u32 PAR[4], EXT[4], HW[4], WRK[4], TOT, cur;
static struct t32_wdr_rec REC[4];
static struct t32_wdr_st ST;

static u32 lay(u32 n)
{
	u32 i, o;

	cur = (cur + FR + 63u) & ~63u;
	o = cur;
	for (i = 0; i < n; i++)
		IMG[o + i] = (u8)(rnd() >> 9);
	cur += n;
	TOT = cur + FR;
	return o;
}
static void w16(u32 o, u32 v) { IMG[o] = (u8)v; IMG[o + 1] = (u8)(v >> 8); }
static void w32(u32 o, u32 v) { w16(o, v); w16(o + 2, v >> 16); }
static u32 r16(u32 o) { return IMG[o] | (u32)IMG[o + 1] << 8; }

static void hist(u32 o, u32 mode)
{
	u32 i, c = rb(256), w = 1 + rb(40);

	for (i = 0; i < 256; i++) {
		u32 v;

		switch (mode) {
		case 0: v = rb(1000); break;
		case 1: v = rb(8) ? 0 : rb(5000); break;
		case 2: v = (i > c ? i - c : c - i) < w ? 1000 + rb(20000) : rb(3); break;
		case 3: v = rnd() >> rb(4); break;
		case 4: v = 100; break;
		default: v = rb(3); break;
		}
		w32(o + 4 * i, v);
	}
}

/* the getVar result the vendor will see for channel i (mode 1): run the vendor matching on a copy */
static u32 knot(u32 v)
{
	switch (rb(5)) {
	case 0: return v;
	case 1: return v + 1;
	case 2: return v ? v - 1 : 0;
	case 3: return rb(2000);
	default: return rnd() & 0xffff;
	}
}

static void fill(u32 i)
{
	u32 p = PAR[i], e = EXT[i], k, m, x0;

	/* degweight_ev: configuration, 8 ascending knots, weights */
	w16(p + 0x1fa, rb(2)); w16(p + 0x1fc, rb(2)); w16(p + 0x1fe, rb(4000)); w16(p + 0x200, rb(2000));
	w16(p + 0x202, rb(3) ? rb(64) : rnd());
	for (k = 0, x0 = rb(64); k < 8; k++, x0 += 1 + rb(200)) {
		w32(p + 0x30 + 4 * k, x0);
		w16(p + 0x206 + 2 * k, rb(64));
	}
	m = rb(8);
	w16(p + 0x1e4, m < 4 ? 1 : m < 7 ? 0 : (rb(2) ? 2 : rnd()));
	w16(p + 0x1ee, rb(4) ? 1 : rb(3));
	w16(p + 0x1ea, rb(3) ? rb(17) : rnd());
	w16(p + 0x1ec, rb(3) ? rb(17) : rnd());
	w16(p + 0x1f4, rnd()); w16(p + 0x1f6, rnd());
	/* exposure knots (ascending, rarely equal neighbours above) and the exposures */
	for (k = 0, x0 = rb(3) ? rb(1000) : rnd() >> 4; k < 4; k++) {
		w32(p + 0x80 + 4 * k, x0);
		x0 += rb(8) ? 1 + rb(3000) : 1;
	}
	k = rb(5);
	if (rb(4)) {
		w32(e + 4, k < 4 ? r16(p + 0x80 + 4 * k) + (r16(p + 0x82 + 4 * k) << 16) + rb(3) - 1 : rnd());
	} else {
		w32(e + 4, rnd() >> rb(32));
	}
	w32(e + 0, rb(4) ? 1 + rb(5000) : 1 + (rnd() >> rb(31)));
	w32(e + 8, rb(2) ? rnd() : rb(5000));
	w32(e + 12, rb(3) ? rb(5000) : rnd());
	for (k = 0; k < 4 * 27; k++)
		w16(p + 0x216 + 2 * k, rb(6) ? rb(4096) : rb(3) ? 0 : rnd());
	IMG[p + 0x71f] = (u8)(rb(2) ? rb(8) : rnd());
	for (k = 0; k < 6; k++)
		hist(WRK[i] + 0x38 + 1024 * k, rb(6));
	if (rb(4) == 0)
		memcpy(IMG + WRK[i] + 0x38 + 3 * 1024, IMG + WRK[i] + 0x38 + 2 * 1024, 1024);
	/* variance knots: random, or around the vendor's getVar result (computed below) */
	w16(p + 0x1e6, knot(rb(5000))); w16(p + 0x1e8, knot(rb(5000)));
	w16(p + 0x1f0, knot(rb(5000))); w16(p + 0x1f2, knot(rb(5000)));
}

/* place the variance knots around the getVar result of channel ch: run the vendor once on a scratch copy */
static void steer(u32 ch)
{
	u32 p = PAR[ch], v, k[4], j;

	if (r16(p + 0x1e4) != 1 || rb(3) == 0)
		return;
	t32o_reset();
	VA = t32o_alloc(ARN);
	memcpy(VA, IMG, TOT);
	for (j = 0; j < 4; j++) {
		VREC[j] = t32o_alloc(32);
		VREC[j][3] = T32O_PTR(VA + PAR[j]); VREC[j][4] = T32O_PTR(VA + EXT[j]);
		VREC[j][5] = T32O_PTR(VA + HW[j]); VREC[j][6] = T32O_PTR(VA + WRK[j]);
		t32_data_e3b40[j] = T32O_PTR(VREC[j]);
	}
	t32o_call((t32o_fn)(void (*)(void))Tiziano_wdr_deghost_fpga, ch, 0, 0, 0, 0);
	v = (u32)tisp_wdr_getVar(T32O_PTR(VA + WRK[ch] + 0x25d8), 0, 0, 0);
	for (j = 0; j < 4; j++)
		k[j] = rb(3) == 0 ? v + 1 + rb(50) : rb(2) ? (v > rb(50) ? v - rb(50) : 0) : v + rb(3) - 1;
	if (rb(2)) k[0] = v, k[1] = v + 1;
	if (rb(3) == 0) k[2] = v, k[3] = v + 1 + rb(9);
	w16(p + 0x1e6, k[0]); w16(p + 0x1e8, k[1] > k[0] ? k[1] : k[0] + 1 + rb(40));
	w16(p + 0x1f0, k[2]); w16(p + 0x1f2, k[3] > k[2] ? k[3] : k[2] + 1 + rb(40));
}

static void setup(void)
{
	u32 i;

	memset(&ST, 0, sizeof(ST));
	ST.ops = &OPS;
	for (i = 0; i < 4; i++) {
		VREC[i] = t32o_alloc(32);
		VREC[i][0] = rnd(); VREC[i][1] = rnd(); VREC[i][2] = rnd();
		VREC[i][3] = T32O_PTR(VA + PAR[i]);
		VREC[i][4] = T32O_PTR(VA + EXT[i]);
		VREC[i][5] = T32O_PTR(VA + HW[i]);
		VREC[i][6] = T32O_PTR(VA + WRK[i]);
		t32_data_e3b40[i] = T32O_PTR(VREC[i]);
		REC[i].par = OA + PAR[i];
		REC[i].ext = OA + EXT[i];
		REC[i].hw = OA + HW[i];
		REC[i].work = OA + WRK[i];
		ST.rec[i] = &REC[i];
	}
	t32_data_e3a9a[0] = ST.ev_hold = rb(4) == 0 ? 0xff : (u8)rb(2) | (rb(3) ? 1 : 0);
}

int main(void)
{
	u32 n, i, bad = 0, cases = 0, modes[3] = { 0, 0, 0 };

	for (n = 0; n < 6000; n++) {
		u32 ch = rb(4), m;
		int d = 0;

		cur = 0;
		memset(IMG, CAN, sizeof(IMG));
		for (i = 0; i < 4; i++) {
			PAR[i] = lay(0x720);
			EXT[i] = lay(112);
			HW[i] = lay(1036);
			WRK[i] = lay(10200);
			fill(i);
		}
		m = r16(PAR[ch] + 0x1e4);
		modes[m == 1 ? 1 : m == 0 ? 0 : 2]++;
		steer(ch);
		t32o_reset();
		VA = t32o_alloc(ARN);
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		NVK = NVF = NOK = NOF = ohu = 0;
		setup();
		t32o_call((t32o_fn)(void (*)(void))Tiziano_wdr_deghost_fpga, ch, 0, 0, 0, 0);
		t32_wdr_deghost_fpga(&ST, ch);
		cases++;
		if (memcmp(VA, OA, TOT))
			d = 2;
		else if (NVK != NOK || NVF != NOF || NVK > NEV)
			d = 3;
		else if (t32_data_e3a9a[0] != ST.ev_hold)
			d = 6;
		for (i = 0; !d && i < NVK; i++)
			if (VK[i][0] != OK[i][0] || VK[i][1] != OK[i][1] || memcmp(VAL[i], OAL[i], OK[i][0]))
				d = 4;
		for (i = 0; !d && i < NVF; i++)
			if (VF[i] != (u32)OF[i])
				d = 5;
		if (d && bad++ < 5) {
			u32 j;

			for (j = 0; d == 2 && j < TOT; j++)
				if (VA[j] != OA[j])
					break;
			fprintf(stderr, "t32_wdrfpga1_oracle: case %u ch %u mode %u differs (%d at %u)\n", n, ch, m, d,
				d == 2 ? j : 0);
		}
	}
	printf("t32_wdrfpga1_oracle: %u cases (mode 0 %u, 1 %u, other %u), %u mismatches\n", cases, modes[0],
	       modes[1], modes[2], bad);
	return bad ? 1 : 0;
}
