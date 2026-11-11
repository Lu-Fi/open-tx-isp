// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, WDR phase 8: our readable t32_wdr_get_map
 * (driver/t32/tx_isp_t32_wdr.h) against the unmodified vendor tisp_wdr_get_map
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 *
 * The vendor function takes 12 arguments; a small wrapper run on the low
 * stack (t32o_call) passes them.  Six random 256 bin histograms (flat,
 * sparse, peaked, huge counts with signed-compare wrap, equal pairs) and six
 * outputs in one canary framed arena, compared byte for byte with the
 * return value; every kmalloc (size, flags) and kfree is logged on both sides
 * in order, our temporaries come from our heap and are paired with the
 * vendor's by order, their contents are compared too.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_wdr.h"

#define FR	64u
#define CAN	0xc5
#define ARN	(1u << 15)
#define NEV	32

u64 tisp_wdr_get_map(u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32);

static u32 rs = 0x1f83d9abu;
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

static u32 GA[12];
static u64 wrap(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
	return tisp_wdr_get_map(GA[0], GA[1], GA[2], GA[3], GA[4], GA[5], GA[6], GA[7], GA[8], GA[9], GA[10], GA[11]);
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

static u8 IMG[ARN], OA[ARN] __attribute__((aligned(16)));
static u8 *VA;
static u32 OFF[12], TOT, cur;

static void lay_begin(void) { cur = 0; memset(IMG, CAN, sizeof(IMG)); }
static u32 lay(u32 n)
{
	u32 i, o;

	cur = (cur + FR + 7u) & ~7u;
	o = cur;
	for (i = 0; i < n; i++)
		IMG[o + i] = (u8)(rnd() >> 9);
	cur += n;
	TOT = cur + FR;
	return o;
}

static void hist(u32 o, u32 mode)
{
	u32 i, c = rb(256), w = 1 + rb(40);

	for (i = 0; i < 256; i++) {
		u32 v;

		switch (mode) {
		case 0: v = rb(1000); break;
		case 1: v = rb(8) ? 0 : rb(5000); break;
		case 2: v = (i > c ? i - c : c - i) < w ? 1000 + rb(20000) : rb(3); break;
		case 3: v = rnd() >> rb(4); break;	/* sums wrap, differences >= 2^31 */
		case 4: v = 100; break;
		default: v = rb(3); break;
		}
		memcpy(IMG + o + 4 * i, &v, 4);
	}
}

int main(void)
{
	u32 n, i, bad = 0, cases = 0;

	for (n = 0; n < 4000; n++) {
		const u32 *h[6];
		u64 rv, ro;
		int d = 0;

		lay_begin();
		for (i = 0; i < 6; i++) {
			OFF[i] = lay(1024);
			hist(OFF[i], rb(6));
		}
		if (rb(4) == 0) memcpy(IMG + OFF[3], IMG + OFF[0], 1024);	/* identical pair: the identity match */
		if (rb(4) == 0) {	/* first 4 bins of h4 at the rounding edge of the return value: 4 h0 + 3 h1 + 2 h2 + h3 = 1024 + k 2048 */
			u32 v[4] = { 256 + 512 * rb(4), 0, 0, rb(2) ? 0 : 2048 };
			memcpy(IMG + OFF[4], v, 16);
		}
		for (i = 6; i < 12; i++)
			OFF[i] = lay(512);
		t32o_reset();
		VA = t32o_alloc(ARN);
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		NVK = NVF = NOK = NOF = ohu = 0;
		for (i = 0; i < 12; i++)
			GA[i] = T32O_PTR(VA + OFF[i]);
		rv = t32o_call((t32o_fn)wrap, 0, 0, 0, 0, 0);
		for (i = 0; i < 6; i++)
			h[i] = (const u32 *)(OA + OFF[i]);
		ro = t32_wdr_get_map(&OPS, h, (u16 *)(OA + OFF[6]), (u16 *)(OA + OFF[7]), (u16 *)(OA + OFF[8]),
				     (u16 *)(OA + OFF[9]), (u16 *)(OA + OFF[10]), (u16 *)(OA + OFF[11]));
		cases++;
		if ((u32)rv != (u32)ro)
			d = 1;
		else if (memcmp(VA, OA, TOT))
			d = 2;
		else if (NVK != NOK || NVF != NOF || NVK > NEV)
			d = 3;
		for (i = 0; !d && i < NVK; i++)
			if (VK[i][0] != OK[i][0] || VK[i][1] != OK[i][1] || memcmp(VAL[i], OAL[i], OK[i][0]))
				d = 4;
		for (i = 0; !d && i < NVF; i++)
			if (VF[i] != (u32)OF[i])
				d = 5;
		if (d && bad++ < 5)
			fprintf(stderr, "t32_wdr8_oracle: case %u differs (%d; ret %08x / %08x)\n", n, d, (u32)rv, (u32)ro);
	}
	printf("t32_wdr8_oracle: %u cases, %u mismatches\n", cases, bad);
	return bad ? 1 : 0;
}
