// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_slake_module (driver/t32/tx_isp_t32_ispslake.h) against
 * the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc,
 * built for the host with env.c).  Random worlds built twice (core / private NULL, error or
 * valid; channel states 0..4 / random, 0..4 frame channels, threads present or not with scripted
 * stop results, sensors with or without a slake op and scripted results, 0..6 clocks and
 * negative counts); compared: return value, call log (isp_printf level / line / defined argument,
 * ispcore_core_ops_init sd and flag word, tuning event, kthread_stop, tisp_disable_tuning, the
 * clock queries, clk_disable_unprepare) and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispslake.h"

u64 ispcore_slake_module(u32 a0, u32 a1, u32 a2, u32 a3);

#define WSZ	0x4000u
#define MAXL	64
static u32 rs = 0x51a4eu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 ss;
static u32 srnd(void)
{
	ss ^= ss << 13; ss ^= ss >> 17; ss ^= ss << 5;
	return ss;
}
struct lg { unsigned int n; u32 e[MAXL][4]; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W, SD;
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b; lg.e[lg.n][3] = c;
	}
	lg.n++;
}
static u32 W32(u32 a) { u32 v; memcpy(&v, T32O_HOST(a), 4); return v; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a1; (void)a2;
	L(1, a0, a3, a3 == 3995 ? a4 : 0);	/* line 3961: a4 is an unset stack word */
	return 0;
}
u64 ispcore_core_ops_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(2, a0, W32(a1), 0); return 0; }
u64 private_kthread_stop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(3, a0, 0, 0); return srnd() % 4 ? 0 : srnd() | 1; }
u64 tisp_disable_tuning(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(4, 0, 0, 0); return 0; }
u64 get_isp_clk(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(5, 0, 0, 0); return srnd(); }
u64 get_isp_clks(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(6, 0, 0, 0); return srnd(); }
u64 get_isp_clkl(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(7, 0, 0, 0); return srnd(); }
u64 private_clk_disable_unprepare(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(8, a0, 0, 0); return 0; }
static u64 r_ev(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(9, a0, a1, a2); return srnd(); }
static u64 r_sl(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 k = srnd() % 8;

	(void)a1; (void)a2; (void)a3;
	L(10, a0, 0, 0);
	return k < 4 ? 0 : k < 6 ? (u32)T32_ENOIOCTLCMD : srnd();
}

static int o_call(u32 fn, u32 a0, u32 a1, u32 a2) { return (int)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, 0); }
static void o_log(u32 l, u32 line, u32 a4) { isp_printf(l, 0, 0, line, a4); }
static void o_coi(u32 sd, u32 *on) { ispcore_core_ops_init(sd, T32O_PTR(on), 0, 0); }
static int o_ks(u32 t) { return (int)private_kthread_stop(t, 0, 0, 0); }
static void o_dt(void) { tisp_disable_tuning(0, 0, 0, 0); }
static void o_gc(void) { get_isp_clk(0, 0, 0, 0); get_isp_clks(0, 0, 0, 0); get_isp_clkl(0, 0, 0, 0); }
static void o_co(u32 c) { private_clk_disable_unprepare(c, 0, 0, 0); }
static const struct t32_slake_ops ops = { o_call, o_log, o_coi, o_ks, o_dt, o_gc, o_co };

static u32 badp(void) { return rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff; }
static void build(u32 seed)
{
	u32 save = rs, i, P;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	SD = rnd() % 16 ? W : badp();
	t32_ic_w32(W, T32_SD_PRIV, rnd() % 12 ? W + 0x400 : badp());
	P = W + 0x400;
	for (i = 0; i < 4; i++) {
		u32 k = rnd() % 7;

		t32_ic_w32(P, T32_PM_CH_STATE + 4 * i, k < 5 ? k : k == 5 ? 1 : rnd());
	}
	t32_ic_w32(P, T32_PRIV_NFCH, rnd() % 5);
	t32_ic_w32(P, T32_PRIV_FCH, W + 0x1000);
	t32_ic_w32(P, T32_PRIV_TUNING, W + 0x1600);
	t32_ic_w32(W + 0x1600, T32_TUN_EVENT, T32O_PTR(r_ev));
	for (i = 0; i < 2; i++)
		t32_ic_w32(P, T32_PRIV_THREADS + 4 * i, rnd() % 3 ? rnd() | 1 : 0);
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		u32 s = W + 0x1800 + 0x100 * i, o = W + 0x2800 + 0x40 * i, pm = W + 0x2c00 + 0x40 * i, k = rnd() % 8;

		t32_ic_w32(P, T32_SD_SENSORS + 4 * i, k < 4 ? s : k < 6 ? 0 : 0xfffff001u + rnd() % 0xfff);
		t32_ic_w32(s, T32_SD_OPS, o);
		t32_ic_w32(o, T32_OPS_PM, rnd() % 5 ? pm : 0);
		t32_ic_w32(pm, T32_PM_SLAKE, rnd() % 4 ? T32O_PTR(r_sl) : 0);
	}
	t32_ic_w32(W, 256, W + 0x3800);
	t32_ic_w32(W, 260, rnd() % 10 ? rnd() % 7 : 0x80000000u | rnd());
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}

static u64 ours(u32 sd, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return (u32)t32_ispcore_slake_module(&ops, sd);
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 20000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, SD, 0, 0, 0, 0);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ispcore_slake_module, SD, rnd(), rnd(), rnd(), 0);
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispslake_oracle: MISMATCH seed %08x: %d/%d log %u/%u\n", seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispslake_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
