// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable subdev clock list (driver/t32/tx_isp_t32_ispclk.h) against the
 * vendor isp_subdev_init_clks and isp_subdev_release_clks from Matt Davis' mechanical MIPS-to-C
 * recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 * Random subdevs (0..8 clocks, signed-negative counts for release), clock tables with rate 0xffff
 * or a rate, scripted kmalloc failure, devm_clk_get error pointers and set_rate errors, built
 * twice; compared: return value, call log (kmalloc size/flags, clk get/put/set_rate, kfree,
 * isp_printf line and defined arguments) and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispclk.h"

u64 isp_subdev_init_clks(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_subdev_release_clks(u32 a0, u32 a1, u32 a2, u32 a3);

#define WSZ	0x4000u
#define MAXL	48
static u32 rs = 0xc10cu;
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
static u32 W;
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		u32 *p = lg.e[lg.n];

		p[0] = id; p[1] = a; p[2] = b; p[3] = c;
	}
	lg.n++;
}
u64 private_kmalloc(u32 n, u32 gfp, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	L(1, n, gfp, 0);
	return srnd() % 8 ? W + 0x3000 : 0;
}
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(2, a0, 0, 0); return 0; }
u64 private_devm_clk_get(u32 dev, u32 name, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	L(3, dev, name, 0);
	return srnd() % 10 ? W + 0x2000 + (srnd() % 64) * 16 : (u32)-(int)(1 + srnd() % 4095);
}
u64 private_devm_clk_put(u32 dev, u32 clk, u32 a2, u32 a3) { (void)a2; (void)a3; L(4, dev, clk, 0); return 0; }
u64 private_clk_set_rate(u32 clk, u32 rate, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	L(5, clk, rate, 0);
	return srnd() % 10 ? 0 : srnd();
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	(void)a1; (void)a2;
	/* line 2941: a4 is an unset stack slot (undefined), not compared */
	L(6, a0, a3, a3 == 2948 ? a4 : 0);
	L(7, a3 == 2948 || a3 == 2941 ? a5 : 0, a3 == 2941 ? a6 : 0, 0);
	return 0;
}

static void *o_km(u32 n, u32 g) { return T32O_HOST((u32)private_kmalloc(n, g, 0, 0)); }
static void o_kf(u32 p) { private_kfree(p, 0, 0, 0); }
static u32 o_get(u32 d, u32 n) { return (u32)private_devm_clk_get(d, n, 0, 0); }
static void o_put(u32 d, u32 c) { private_devm_clk_put(d, c, 0, 0); }
static int o_rate(u32 c, u32 r) { return (int)private_clk_set_rate(c, r, 0, 0); }
static void o_log(u32 line, u32 a4, u32 a5, u32 a6) { isp_printf(2, 0, 0, line, a4, a5, a6); }
static const struct t32_clk_ops ops = { o_km, o_kf, o_get, o_put, o_rate, o_log };

static void build(u32 seed, int rel)
{
	u32 save = rs, i, n;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	n = rnd() % 10 ? rnd() % 9 : 0;
	if (rel && rnd() % 8 == 0)
		n = 0x80000000u | rnd();	/* a negative signed count: no put */
	t32_ic_w32(W, T32_SD_NCLKS, n);
	t32_ic_w32(W, T32_SD_CLKS, rel ? (rnd() % 5 ? W + 0x1000 : 0) : rnd());
	for (i = 0; i < 9; i++) {
		t32_ic_w32(W + 0x800, 8 * i, W + 0xc00 + 16 * i);
		t32_ic_w32(W + 0x800, 8 * i + 4, rnd() % 3 ? T32_CLK_KEEP_RATE : rnd());
	}
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x6c8e9cf5u) | 1;
	rs = save;
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 30000; it++) {
		u32 seed = rnd(), rel = it & 1;
		int r, rr;

		build(seed, rel);
		r = rel ? t32_isp_subdev_release_clks(&ops, W) : t32_isp_subdev_init_clks(&ops, W, W + 0x800);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed, rel);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))(rel ? isp_subdev_release_clks : isp_subdev_init_clks),
					 W, W + 0x800, rnd(), rnd(), 0);
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispclk_oracle: MISMATCH %s seed %08x: %d/%d log %u/%u\n", rel ? "release" : "init", seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispclk_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
