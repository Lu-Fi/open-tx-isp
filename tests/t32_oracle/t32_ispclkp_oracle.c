// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_set_clk_parent.isra.13 (driver/t32/tx_isp_t32_ispclkp.h)
 * against the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc, built for the host with env.c, parent name table ce220 from data.c).
 * Parent names: the three known ones, prefixes / extensions of them, empty and random strings;
 * scripted devm_clk_get error pointers and clk_set_parent results.  Compared: call log (names by
 * content, clock handles, isp_printf format and defined argument) and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispclkp.h"

u64 ispcore_set_clk_parent_isra_13(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_ccd34[1], t32_data_ccd44[1], t32_data_ccd5c[1];

#define WSZ	0x400u
#define MAXL	16
static u32 rs = 0xc1c9u;
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
struct lg { unsigned int n; u32 e[MAXL][3]; char s[MAXL][8]; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W;
static void L(u32 id, u32 a, u32 b, const char *s)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b;
		if (s)
			strncpy(lg.s[lg.n], s, 7);
	}
	lg.n++;
}
u64 private_devm_clk_get(u32 dev, u32 name, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	L(1, dev, name, 0);
	return srnd() % 5 ? W + 0x300 : (u32)-(int)(1 + srnd() % 4095);
}
u64 private_devm_clk_put(u32 dev, u32 c, u32 a2, u32 a3) { (void)a2; (void)a3; L(2, dev, c, 0); return 0; }
u64 clk_get(u32 dev, u32 name, u32 a2, u32 a3) { (void)a2; (void)a3; L(3, dev, 0, (const char *)T32O_HOST(name)); return W + 0x380; }
u64 clk_set_parent(u32 c, u32 p, u32 a2, u32 a3) { (void)a2; (void)a3; L(4, c, p, 0); return srnd() % 4 ? 0 : srnd() | 1; }
u64 clk_put(u32 c, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(5, c, 0, 0); return 0; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 m = a1 == T32O_PTR(t32_data_ccd34) ? 0 : a1 == T32O_PTR(t32_data_ccd44) ? 1 : a1 == T32O_PTR(t32_data_ccd5c) ? 2 : 9;

	(void)a3;
	L(6, a0, m, m < 2 ? (const char *)T32O_HOST(a2) : 0);
	return 0;
}
static u32 o_dg(u32 d, u32 n) { return (u32)private_devm_clk_get(d, n, 0, 0); }
static void o_dp(u32 d, u32 c) { private_devm_clk_put(d, c, 0, 0); }
static u32 o_cg(u32 d, const char *n)
{
	static char nb[8];	/* 32-bit address for the recorder */

	strncpy(nb, n, 7);
	return (u32)clk_get(d, T32O_PTR(nb), 0, 0);
}
static int o_sp(u32 c, u32 p) { return (int)clk_set_parent(c, p, 0, 0); }
static void o_cp(u32 c) { clk_put(c, 0, 0, 0); }
static void o_log(u32 m, u32 arg)
{
	u32 f = m == 0 ? T32O_PTR(t32_data_ccd34) : m == 1 ? T32O_PTR(t32_data_ccd44) : T32O_PTR(t32_data_ccd5c);

	isp_printf(1, f, arg, 0);
}
static const struct t32_clkp_ops ops = { o_dg, o_dp, o_cg, o_sp, o_cp, o_log };

static void build(u32 seed)
{
	static const char *const pick[] = { "sclka", "mpll", "vpll", "sclk", "mpl", "vpllx", "", "sclkb", "MPLL", "apll" };
	u32 save = rs, i;
	const char *p;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	p = pick[rnd() % 10];
	memcpy(world + 0x100, p, strlen(p) + 1);
	if (rnd() % 8 == 0) {	/* a random string */
		for (i = 0; i < 6; i++)
			world[0x100 + i] = 'a' + rnd() % 26;
		world[0x106] = 0;
	}
	strcpy((char *)world + 0x140, "cgu_isp");
	t32_ic_w32(W, 0, W + 0x200);	/* *devp = device */
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x3c6ef372u) | 1;
	rs = save;
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 20000; it++) {
		u32 seed = rnd();

		build(seed);
		t32_ispcore_set_clk_parent(&ops, W, W + 0x100, W + 0x140);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		t32o_call((t32o_fn)(void (*)(void))ispcore_set_clk_parent_isra_13, W, W + 0x100, W + 0x140, rnd(), 0);
		compared++;
		if (memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispclkp_oracle: MISMATCH seed %08x log %u/%u\n", seed, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispclkp_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
