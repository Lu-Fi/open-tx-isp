// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, WDR fpga phase 2: our readable t32_wdr_fusion_fpga
 * (driver/t32/tx_isp_t32_wdrfpga.h) against the unmodified vendor
 * Tiziano_wdr_fusion_fpga (Matt Davis' recovered C, PR #15 @ 2e2e06bc) with
 * its vendor callees (get_lum_s, interplate, paixu, log2).
 * tisp_tmo_timeratio_curve_adjust (TMO block) is a recording hook on both sides.
 *
 * Per case 4 channel records in one canary framed arena, random contents with
 * every branch input forced around its knots (flags 1 / 0 / other, exposures
 * around the 3 exposure knot pairs, ascending knot tables, sort length up to
 * 255).  Compared: the arena byte for byte, the fusion globals, every kmalloc
 * (size, flags, 512 bytes of contents: the vendor clears 510 bytes of the
 * first 450 byte buffer and the sums read past 450 for n > 225), every
 * kfree and every hook call in order.  Not fed (vendor divide by zero):
 * ext[0] = 0, a zero block size, sort length 0, par + 8 == par + 4, a tone
 * knot step below 1/128 with different neighbours.  Exit 0 = all equal.
 * One intended difference (#279, beyond vendor): the port allocates the two
 * statistics buffers with 510 instead of 450 bytes and clears both fully.  Both
 * sides fill the requested bytes with 0xe7 and the slab slack up to 512 with 0
 * (the vendor's stale tail of the second buffer is modelled as the zeros the
 * port now guarantees); kmalloc sizes compare with vendor 450 == ours 510.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_wdrfpga.h"

#define WEAK __attribute__((weak))
#define FR	64u
#define CAN	0xc5
#define ARN	(1u << 16)
#define NEV	32
#define KSZ	512u

WEAK u32 t32_data_e3b40[4];
WEAK u8 t32_data_e3a9a[4];
WEAK u16 t32_data_e3b30[5];
WEAK u8 t32_data_e3b10[4];
WEAK u16 t32_data_e3b14[5];
WEAK u16 t32_data_e3b20[5];
WEAK u16 t32_data_e3b2a[1];
WEAK u16 t32_data_e3b2c[1];
WEAK u16 t32_data_e3b3a[1];
WEAK u8 t32_data_e3b3c[4];
/* data.c:5863 .. 5942 (t32_data_c2b10, 80 words) */
u32 t32_data_c2b10[80] = {
	0x007f003fU,0x00ff00bfU,0x017f013fU,0x01ff01bfU,0x027f023fU,0x02ff02bfU,0x037f033fU,0x03ff03bfU,
	0x047f043fU,0x04ff04bfU,0x057f053fU,0x05ff05bfU,0x067f063fU,0x06ff06bfU,0x077f073fU,0x07ff07bfU,
	0x081f080fU,0x083f082fU,0x085f084fU,0x087f086fU,0x089f088fU,0x08bf08afU,0x08df08cfU,0x08ff08efU,
	0x091f090fU,0x093f092fU,0x095f094fU,0x097f096fU,0x099f098fU,0x09bf09afU,0x09df09cfU,0x09ff09efU,
	0x0a1f0a0fU,0x0a3f0a2fU,0x0a5f0a4fU,0x0a7f0a6fU,0x0a9f0a8fU,0x0abf0aafU,0x0adf0acfU,0x0aff0aefU,
	0x0b1f0b0fU,0x0b3f0b2fU,0x0b5f0b4fU,0x0b7f0b6fU,0x0b9f0b8fU,0x0bbf0bafU,0x0bdf0bcfU,0x0bff0befU,
	0x0c1f0c0fU,0x0c3f0c2fU,0x0c5f0c4fU,0x0c7f0c6fU,0x0c9f0c8fU,0x0cbf0cafU,0x0cdf0ccfU,0x0cff0cefU,
	0x0d1f0d0fU,0x0d3f0d2fU,0x0d5f0d4fU,0x0d7f0d6fU,0x0d9f0d8fU,0x0dbf0dafU,0x0ddf0dcfU,0x0dff0defU,
	0x0e1f0e0fU,0x0e3f0e2fU,0x0e5f0e4fU,0x0e7f0e6fU,0x0e9f0e8fU,0x0ebf0eafU,0x0edf0ecfU,0x0eff0eefU,
	0x0f1f0f0fU,0x0f3f0f2fU,0x0f5f0f4fU,0x0f7f0f6fU,0x0f9f0f8fU,0x0fbf0fafU,0x0fdf0fcfU,0x0fff0fefU
};
/* data.c:5943 .. 5955 (t32_data_c2c50, 13 words) */
u32 t32_data_c2c50[13] = {
	0x00ff0000U,0x03ff01ffU,0x0bff07ffU,0x0fff0fffU,0x0fff0fffU,0x0fff0fffU,0x0fff0fffU,0x0fff0fffU,
	0x0fff0fffU,0x0fff0fffU,0x0fff0fffU,0x0fff0fffU,0x00000fffU
};

u64 Tiziano_wdr_fusion_fpga(u32, u32, u32, u32);

static u32 rs = 0x2545f491u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rb(u32 n) { return rnd() % n; }

/* hook log: tisp_tmo_timeratio_curve_adjust */
static u32 VT[8], OT[8], NVT, NOT;
u64 tisp_tmo_timeratio_curve_adjust(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7)
{
	u32 v[8] = { a0, a1, a2, a3, a4, a5, a6, a7 };

	if (!NVT)
		memcpy(VT, v, sizeof(v));
	NVT++;
	return 0;
}
static void o_tmo(u32 ch, u32 ratio, u32 mode, u32 a, u32 b, u32 c, u32 d, u32 e)
{
	u32 v[8] = { ch, ratio, mode, a, b, c, d, e };

	if (!NOT)
		memcpy(OT, v, sizeof(v));
	NOT++;
}

/* vendor side: kmalloc / kfree log */
static u32 VK[NEV][2], VF[NEV], NVK, NVF;
static u8 *VAL[NEV];
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u8 *p = t32o_alloc(a0 > KSZ ? a0 : KSZ);

	(void)a2; (void)a3;
	if (p) {
		memset(p, 0, a0 > KSZ ? a0 : KSZ);
		memset(p, 0xe7, a0);
	}
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
	ohu += size > KSZ ? size : KSZ;
	memset(p, 0, size > KSZ ? size : KSZ);
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
static struct t32_wdrf_st FS;

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

/* a flag word: mostly 1, else 0 or other */
static u32 flag(void) { u32 m = rb(8); return m < 5 ? 1 : m < 7 ? 0 : (rb(2) ? 2 : rnd() & 0xffff); }
static u32 scale(void) { return 1u << (2 + rb(15)); }
/* n ascending u16 knots (sometimes equal neighbours) */
static void knots(u32 o, u32 n)
{
	u32 s = scale(), x = rb(s), i;

	for (i = 0; i < n; i++) {
		w16(o + 2 * i, x > 0xffff ? 0xffff : x);
		x += rb(10) ? 1 + rb(s) : 0;
	}
}
static void ys(u32 o, u32 n, u32 m)
{
	u32 i;

	for (i = 0; i < n; i++)
		w16(o + 2 * i, rb(4) ? rb(m) : rnd());
}
/* exposure knot pair around exp */
static void epair(u32 o, u32 exp)
{
	u32 e0, e1;

	switch (rb(6)) {
	case 0: e0 = exp + 1 + rb(100); e1 = e0 + 1 + rb(100); break;
	case 1: e0 = exp > 100 ? exp - rb(100) : 0; e1 = exp + 1 + rb(100); break;
	case 2: e1 = exp > 50 ? exp - rb(50) : 0; e0 = e1 > 50 ? e1 - 1 - rb(50) : 0; break;
	case 3: e0 = exp; e1 = exp + 1 + rb(100); break;
	case 4: e1 = exp; e0 = exp > 60 ? exp - 1 - rb(60) : 0; break;
	default: e0 = rnd() >> rb(32); e1 = e0 + 1 + (rnd() >> rb(32)); break;
	}
	if (e1 <= e0)
		e1 = e0 + 1;
	w32(o, e0);
	w32(o + 4, e1);
}

static void fill(u32 i)
{
	u32 p = PAR[i], e = EXT[i], w = WRK[i], k, exp, x;

	exp = rb(3) ? rb(20000) : rnd() >> rb(32);
	w32(e + 4, exp);
	w32(e + 0, rb(4) ? 1 + rb(4 * (exp + 1)) : 1 + (rnd() >> rb(32)));
	IMG[p + 0x729] = (u8)(rb(4) ? 1 : rnd());
	epair(p + 0x90, exp);
	epair(p + 0x98, exp);
	epair(p + 0xa0, exp);
	ys(p + 0x6f6, 18, 0x2000);
	w16(p + 0x2ee, rb(3) ? rb(0x200) : rnd());
	w16(p + 0x300, flag());
	knots(p + 0x2f6, 5);
	ys(p + 0x302, 5, 0x400);
	w16(p + 0x316, flag());
	knots(p + 0x30c, 5);
	ys(p + 0x318, 5, 0x400);
	w16(p + 0x684, flag());
	knots(p + 0x686, 4);
	ys(p + 0x690, 4, 0x400);
	w16(p + 0x328, flag());
	knots(p + 0x32a, 11);
	ys(p + 0x342, 11, 0x1000);
	w16(p + 0x358, flag());
	knots(p + 0x35a, 11);
	ys(p + 0x372, 11, 0x1000);
	w16(p + 0x388, flag());
	knots(p + 0x38a, 11);
	ys(p + 0x3a2, 11, 0x1000);
	w16(p + 0x698, flag());
	knots(p + 0x69a, 4);
	ys(p + 0x6a4, 4, 0x1000);
	w16(p + 0x6ac, flag());
	knots(p + 0x6ae, 9);
	ys(p + 0x6c0, 10, 0x1000);
	w16(p + 0x3b8, flag());
	{	/* descending exposure scaled knots */
		u32 s = scale(), v = rb(s) + 5 * s;

		for (k = 0; k < 5; k++, v = v > s ? v - rb(s) : 0)
			w16(p + 0x3ba + 2 * k, v > 0xffff ? 0xffff : v);
	}
	ys(p + 0x3c4, 6, 0x1000);
	ys(p + 0x6de, 10, 32);
	w16(p + 0x100, rb(4) ? rb(0x1000) : rnd());
	w16(p + 0x102, rnd()); w16(p + 0x104, rnd());
	w16(p + 0x2f2, flag());
	w16(p + 0x2f4, (rnd() & 0xff00) | (rb(4) ? 1 + rb(225) : 1 + rb(255)));
	w16(p + 0x5ea, flag());
	knots(p + 0x600, 2); knots(p + 0x604, 2); knots(p + 0x608, 2);
	ys(p + 0x60c, 10, 0x1000); ys(p + 0x634, 10, 0x1000); ys(p + 0x65c, 10, 0x1000);
	ys(p + 0x626, 3, 16);
	ys(p + 0x5ec, 5, 0x1000);
	IMG[p + 0x68] = (u8)(rb(4) ? 1 : rnd());
	w32(p + 0x70, rb(3) ? rb(200) : rnd());
	w32(p + 0x74, rb(2) ? exp + rb(3) - 1 : rnd() >> rb(32));
	w32(p + 0x6c, rb(3) ? rb(8) : rnd());
	knots(p + 0x5d0, 4);
	ys(p + 0x5d8, 4, 0x1000);
	knots(p + 0x3d0, 6);
	ys(p + 0x3dc, 250, 0x1000);
	w16(p + 0x6d4, flag());
	knots(p + 0x6d6, 2);
	ys(p + 0x6da, 2, 0x81);
	/* tone knots: equal or >= 128 apart (vendor divides by the 1/128 step), the second one >= 64 */
	for (k = 0, x = rb(0x100); k < 5; k++) {
		w16(p + 0x5e0 + 2 * k, x);
		x += rb(5) ? 128 + rb(0x800) : 0;
		if (k == 0 && x < 64)
			x = 64 + rb(64);
	}
	w32(p + 4, rnd() >> rb(32));
	do
		w32(p + 8, rb(2) ? rnd() : (IMG[p + 4] | IMG[p + 5] << 8) + rb(2000) - 1000);
	while (!memcmp(IMG + p + 4, IMG + p + 8, 4));
	ys(p + 0x322, 3, 0x400);
	for (k = 0; k < 160; k++)
		w32(w + 0x1f58 + 4 * k, rb(3) ? rb(64) : rnd() >> rb(32));
	for (k = 0; k < 450; k++)
		w32(w + 0x1850 + 4 * k, rb(8) ? rb(0x4000) : rnd() >> rb(32));
	for (k = 0; k < 4; k++)
		w32(w + 0x1838 + 4 * k, rb(3) ? rb(0x10000) : rnd());
	w32(w + 0x184c, rb(3) ? rb(0x20000) : rnd());
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
	t32_data_e3a9a[0] = ST.ev_hold = rb(4) == 0 ? (u8)rnd() : (u8)rb(2);
	t32_data_e3b10[0] = ST.nbuf = (u8)(rb(2) ? rb(4) : rnd());
	for (i = 0; i < 5; i++) {
		t32_data_e3b30[i] = ST.grid[i] = (u16)(rb(3) ? 1 + rb(64) : 1 + rb(0xffff));
		t32_data_e3b14[i] = FS.diff_fix_y[i] = (u16)rnd();
		t32_data_e3b20[i] = FS.fix_y_tmp[i] = (u16)(rb(2) ? rb(0x1000) : rnd());
	}
	t32_data_e3b2a[0] = FS.diff_thr_max = (u16)rnd();
	t32_data_e3b2c[0] = FS.thr_max_tmp = (u16)(rb(2) ? rb(0x1000) : rnd());
	t32_data_e3b3a[0] = FS.ev_last_switch = (u16)rnd();
	t32_data_e3b3c[0] = FS.frame_sum = (u8)(rb(2) ? 0 : rnd());
	FS.tmo_timeratio_curve_adjust = o_tmo;
}

static sigjmp_buf FPE;
static void on_fpe(int sig)
{
	(void)sig;
	siglongjmp(FPE, 1);
}

static int globals_differ(void)
{
	return memcmp(t32_data_e3b14, FS.diff_fix_y, 10) || memcmp(t32_data_e3b20, FS.fix_y_tmp, 10) ||
	       t32_data_e3b2a[0] != FS.diff_thr_max || t32_data_e3b2c[0] != FS.thr_max_tmp ||
	       t32_data_e3b3a[0] != FS.ev_last_switch || t32_data_e3b3c[0] != FS.frame_sum ||
	       t32_data_e3a9a[0] != ST.ev_hold || t32_data_e3b10[0] != ST.nbuf;
}

/* kept out of main: the SIGFPE siglongjmp must not clobber them */
static u32 n, i, bad, cases, dz, nc;

int main(int argc, char **argv)
{
	nc = argc > 1 ? (u32)atoi(argv[1]) : 20000;

	signal(SIGFPE, on_fpe);
	for (n = 0; n < nc; n++) {
		static u32 ch;
		static int d;

		ch = rb(4);
		d = 0;

		cur = 0;
		memset(IMG, CAN, sizeof(IMG));
		for (i = 0; i < 4; i++) {
			PAR[i] = lay(0x730);
			EXT[i] = lay(112);
			HW[i] = lay(1036);
			WRK[i] = lay(10200);
			fill(i);
		}
		t32o_reset();
		VA = t32o_alloc(ARN);
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		NVK = NVF = NOK = NOF = ohu = NVT = NOT = 0;
		setup();
		t32o_call((t32o_fn)(void (*)(void))Tiziano_wdr_fusion_fpga, ch, 0, 0, 0, 0);
		if (sigsetjmp(FPE, 1)) {	/* vendor divide by zero (sc = 0 or a wrapped mean), not compared */
			signal(SIGFPE, on_fpe);
			dz++;
			continue;
		}
		t32_wdr_fusion_fpga(&ST, &FS, ch);
		cases++;
		if (memcmp(VA, OA, TOT))
			d = 2;
		else if (NVK != NOK || NVF != NOF || NVK > NEV)
			d = 3;
		else if (globals_differ())
			d = 6;
		else if (NVT != 1 || NOT != 1 || memcmp(VT, OT, sizeof(VT)))
			d = 7;
		for (i = 0; !d && i < NVK; i++)
			if ((VK[i][0] != OK[i][0] && !(i < 2 && VK[i][0] == 450 && OK[i][0] == 510)) || VK[i][1] != OK[i][1] || memcmp(VAL[i], OAL[i], KSZ))
				d = 4;
		for (i = 0; !d && i < NVF; i++)
			if (VF[i] != (u32)OF[i])
				d = 5;
		if (d && bad++ < 8) {
			u32 j, k = 0;

			for (j = 0; d == 2 && j < TOT; j++)
				if (VA[j] != OA[j])
					break;
			for (k = 0; k < 4 && d == 2; k++)
				if (j >= PAR[k] && j < WRK[k] + 10200)
					break;
			fprintf(stderr, "t32_wdrfpga2_oracle: case %u ch %u differs (%d at %u: ch %u par+0x%x ext+0x%x hw+0x%x, v %02x o %02x)\n",
				n, ch, d, d == 2 ? j : 0, k, j - PAR[k & 3], j - EXT[k & 3], j - HW[k & 3],
				d == 2 ? VA[j] : 0, d == 2 ? OA[j] : 0);
		}
	}
	printf("t32_wdrfpga2_oracle: %u cases, %u vendor divide by zero (not compared), %u mismatches\n", cases, dz, bad);
	if (dz * 50 > nc)
		bad++;	/* too many cases lost to the vendor divide by zero */
	return bad ? 1 : 0;
}
