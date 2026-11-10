// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable core pm callbacks (driver/t32/tx_isp_t32_isppm.h) against the
 * vendor ispcore_suspend_module, ispcore_resume_early_module and ispcore_complete_module from
 * Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 * Random worlds (core device NULL / error pointer / valid, private block NULL / error / valid,
 * channel state words around 2, 16 sensor slots NULL / error / valid with pm ops or not,
 * recorder pm callbacks with scripted results 0 / -515 / error) built twice; compared: return
 * value (not for complete: void), the call log (pm callbacks, isp_printf level / line / argument,
 * lib_tisp_aov_sleep, lib_tisp_intp_clear) and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_isppm.h"

u64 ispcore_suspend_module(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_resume_early_module(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_complete_module(u32 a0, u32 a1, u32 a2, u32 a3);

#define WSZ	0x4000u
#define MAXL	48
static u32 rs = 0x9a11u;
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
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		u32 *p = lg.e[lg.n];

		p[0] = id; p[1] = a; p[2] = b; p[3] = c;
	}
	lg.n++;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a1; (void)a2;
	L(1, a0, a3, a3 == 4052 || a3 == 4175 ? a4 : 0);
	return 0;
}
u64 lib_tisp_aov_sleep(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(2, a0, 0, 0); return srnd(); }
u64 lib_tisp_intp_clear(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(3, 0, 0, 0); return srnd(); }
static u64 rec(u32 id, u32 s)
{
	u32 k = srnd() % 8;

	L(4, id, s, 0);
	return k < 4 ? 0 : k < 6 ? (u32)T32_ENOIOCTLCMD : srnd();
}
static u64 r_sus(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return rec(1, a0); }
static u64 r_res(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return rec(2, a0); }
static u64 r_cmp(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return rec(3, a0); }

static int o_call(u32 fn, u32 a0, u32 a1, u32 a2) { return (int)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, 0); }
static void o_log(u32 l, u32 line, u32 a4) { isp_printf(l, 0, 0, line, a4); }
static void o_aov(u32 a) { lib_tisp_aov_sleep(a, 0, 0, 0); }
static void o_ic(void) { lib_tisp_intp_clear(0, 0, 0, 0); }
static const struct t32_pm_ops ops = { o_call, o_log, o_aov, o_ic };

static u8 *world, snap[WSZ];
static u32 W, SD;
static u32 badp(void)
{
	return rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff;
}
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
	P = rnd() % 12 ? W + 0x400 : badp();
	t32_ic_w32(W, T32_SD_PRIV, P);
	P = W + 0x400;
	for (i = 0; i < 4; i++) {
		u32 k = rnd() % 8;

		t32_ic_w32(P, T32_PM_CH_STATE + 4 * i, k < 5 ? 2 + k % 2 : k == 5 ? 1 : k == 6 ? 0 : 0x80000000u);
	}
	t32_ic_w32(P, T32_PM_AOV, rnd() % 2 ? 0 : rnd());
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		u32 s = W + 0x1400 + 0x100 * i, o = W + 0x2400 + 0x40 * i, pm = W + 0x2800 + 0x40 * i, k = rnd() % 8;

		t32_ic_w32(P, T32_SD_SENSORS + 4 * i, k < 5 ? s : k == 5 ? 0 : 0xfffff001u + rnd() % 0xfff);
		t32_ic_w32(s, T32_SD_OPS, o);
		t32_ic_w32(o, T32_OPS_PM, rnd() % 6 ? pm : 0);
		t32_ic_w32(pm, T32_PM_SUSPEND, rnd() % 5 ? T32O_PTR(r_sus) : 0);
		t32_ic_w32(pm, T32_PM_RESUME_EARLY, rnd() % 5 ? T32O_PTR(r_res) : 0);
		t32_ic_w32(pm, T32_PM_COMPLETE, rnd() % 5 ? T32O_PTR(r_cmp) : 0);
	}
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x2545f491u) | 1;
	rs = save;
}

int main(void)
{
	static u64 (*const f[3])(u32, u32, u32, u32) = { ispcore_suspend_module, ispcore_resume_early_module, ispcore_complete_module };
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 30000; it++) {
		u32 seed = rnd(), k = it % 3;
		int r = 0, rr;

		build(seed);
		if (k == 0)
			r = t32_ispcore_suspend_module(&ops, SD);
		else if (k == 1)
			r = t32_ispcore_resume_early_module(&ops, SD);
		else
			t32_ispcore_complete_module(&ops, SD);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))f[k], SD, rnd(), rnd(), rnd(), 0);
		if (k == 2)
			rr = 0;
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_isppm_oracle: MISMATCH fn %u seed %08x: %d/%d log %u/%u\n", k, seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_isppm_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
