// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our VIC stream on / off (driver/t32/tx_isp_t32_vic_stream.h)
 * against the unmodified vendor vic_core_s_stream (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc).  A random image (subdev, VIC private data, argument)
 * runs through ours, is restored and runs through the vendor; compared:
 * every byte, the return value and the ordered log of the hook calls
 * (index_cal, enable / disable IRQ, vic_start, lib_tisp_vic_stop).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_vic_stream.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
u64 vic_core_s_stream(u32, u32, u32, u32);

static u32 rng = 0x3c6ef372u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 12
static struct { unsigned int n; u32 e[MAXE][3]; } EV[2];
static int side;
static u32 r_idx, r_start;
static unsigned int nstart;
static void ev(u32 a, u32 b, u32 c)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c;
	}
}
static u32 h_idx(u8 *vic, u32 ch) { ev(1, T32O_PTR(vic), ch); return r_idx; }
static int h_start(u8 *vic, u32 m) { ev(2, T32O_PTR(vic), m); return (int)(r_start + nstart++); }
static void h_stop(void) { ev(3, 0, 0); }
static void h_en(u32 n) { ev(4, n, 0); }
static void h_dis(u32 n) { ev(5, n, 0); }
static const struct t32_vicops_os os = { .index_cal = h_idx, .vic_start = h_start, .lib_stop = h_stop };
static const struct t32_vic_irq_os irq = { h_en, h_dis };

u64 tx_isp_vic_index_cal(u32 v, u32 ch, u32 a2, u32 a3) { (void)a2; (void)a3; return h_idx(T32O_HOST(v), ch); }
u64 tx_isp_vic_start(u32 v, u32 m, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)h_start(T32O_HOST(v), m); }
u64 lib_tisp_vic_stop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; h_stop(); return 0; }
u64 tx_vic_enable_irq(u32 n, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; h_en(n); return 0; }
u64 tx_vic_disable_irq(u32 n, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; h_dis(n); return 0; }

#define O_SD	0x0000u
#define O_VIC	0x0200u		/* 0x500 */
#define O_ARG	0x0700u
#define IMGSZ	0x0740u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c)
{
	printf("t32_vicstr_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 60000, eff = 0, calls[6] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u8 *sd = img + O_SD, *vic = img + O_VIC, *arg = img + O_ARG;
		u32 sdw = T32O_PTR(sd), ro = 0, rr = 0, ch = rnd() % 16 - 3;	/* -3 .. 12: inside the image */

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		r_idx = rnd() % 4 ? rnd() % 2 : rnd() % 2 ? 2 : rnd();
		r_start = rnd();
		t32_g_wr32(sd, T32_VICD_SD_PRIV, rnd() % 10 == 0 ? 0 : rnd() % 10 == 0 ? 0xfffff001u : T32O_PTR(vic));
		t32_g_wr32(arg, 0, rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd());
		t32_g_wr32(arg, 4, ch);
		t32_g_wr32(vic, T32_VICD_STATE(ch), rnd() % 3 ? 3 + rnd() % 2 : rnd());
		t32_g_wr32(vic, T32_VICD_BUSY, rnd() % 3 ? 0 : rnd());
		t32_g_wr32(vic, T32_VICD_CFG, rnd() % 6 ? 1 + rnd() % 4 : rnd() % 2 ? ch + 1 : rnd());
		if (rnd() % 12 == 0)
			sdw = rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			nstart = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_vic_core_s_stream(&os, &irq, T32O_HOST(sdw), arg);
				memcpy(save1, img, IMGSZ);
			} else {
				rr = (u32)t32o_call(R(vic_core_s_stream), sdw, T32O_PTR(arg), 0, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		if (memcmp(img, save0, IMGSZ))
			eff++;
		for (i = 0; i < EV[0].n && i < MAXE; i++)
			calls[EV[0].e[i][0]]++;
	}
	printf("t32_vicstr_oracle: %u cases, 0 mismatches; changed memory %u; calls idx/start/stop/en/dis %u %u %u %u %u\n",
	       n, eff, calls[1], calls[2], calls[3], calls[4], calls[5]);
	return 0;
}
