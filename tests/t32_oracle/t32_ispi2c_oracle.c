// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable isp_i2c_new_subdev_board (driver/t32/tx_isp_t32_ispi2c.h) against
 * the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc,
 * built for the host with env.c).  Random board infos (address 0 or not), scripted clients
 * (driver word 28 = none, or a driver with owner), try_module_get and clientdata results; the
 * client is never NULL (the vendor reads its driver word before the NULL check: a crash).
 * Compared: return value, call log, every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispi2c.h"

u64 isp_i2c_new_subdev_board(u32 a0, u32 a1, u32 a2, u32 a3);

#define WSZ	0x1000u
#define MAXL	16
static u32 rs = 0x12c0u;
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
struct lg { unsigned int n; u32 e[MAXL][3]; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W;
static void L(u32 id, u32 a, u32 b)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b;
	}
	lg.n++;
}
u64 private_request_module(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(1, a0, a1); return srnd(); }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; L(2, a0, a3); return 0; }
u64 private_i2c_new_device(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(3, a0, a1); return W + 0x200; }
u64 private_try_module_get(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(4, a0, 0); return srnd() % 4 ? 1 : 0; }
u64 private_i2c_get_clientdata(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(5, a0, 0); return srnd() % 4 ? srnd() | 1 : 0; }
u64 private_module_put(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(6, a0, 0); return 0; }
u64 private_i2c_unregister_device(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(7, a0, 0); return 0; }

static void o_rm(u32 w, u32 i) { private_request_module(w, i, 0, 0); }
static void o_log(u32 l, u32 line) { isp_printf(l, 0, 0, line); }
static u32 o_nd(u32 a, u32 i) { return (u32)private_i2c_new_device(a, i, 0, 0); }
static int o_tg(u32 m) { return (int)private_try_module_get(m, 0, 0, 0); }
static u32 o_cd(u32 c) { return (u32)private_i2c_get_clientdata(c, 0, 0, 0); }
static void o_mp(u32 m) { private_module_put(m, 0, 0, 0); }
static void o_un(u32 c) { private_i2c_unregister_device(c, 0, 0, 0); }
static const struct t32_i2c_ops ops = { o_rm, o_log, o_nd, o_tg, o_cd, o_mp, o_un };

static void build(u32 seed)
{
	u32 save = rs, i;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	*(u16 *)T32O_HOST(W + T32_I2C_INFO_ADDR) = rnd() % 6 ? (u16)rnd() | 1 : 0;
	t32_ic_w32(W + 0x200, T32_I2C_CLIENT_DRV, rnd() % 5 ? W + 0x400 : T32_I2C_DRV_EMBED);
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x1234567u) | 1;
	rs = save;
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 20000; it++) {
		u32 seed = rnd(), adap = rnd(), r, rr;

		build(seed);
		r = t32_isp_i2c_new_subdev_board(&ops, adap, W);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (u32)t32o_call((t32o_fn)(void (*)(void))isp_i2c_new_subdev_board, adap, W, rnd(), rnd(), 0);
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispi2c_oracle: MISMATCH seed %08x: %08x/%08x log %u/%u\n", seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispi2c_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
