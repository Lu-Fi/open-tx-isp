// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_sync_sensor_attr (driver/t32/tx_isp_t32_ispsync.h)
 * against the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc, built for the host with env.c).  Random worlds (core device / private block NULL,
 * error pointer or valid; attr NULL or a random 104 B attribute whose +52 / +56 point to random
 * sensor / channel info, channel 0..3) built twice; compared: return value, the call log
 * (isp_printf level / line, lib_tisp_sync_sensor_attr channel and all 124 bytes of the block)
 * and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispsync.h"

u64 ispcore_sync_sensor_attr(u32 a0, u32 a1, u32 a2, u32 a3);

#define WSZ	0x2000u
static u32 rs = 0x5c5au;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
struct lg { unsigned int n; u32 lvl, line, ch; u8 blk[T32_SYNC_BLK]; };
static struct lg lg, lg_ours;
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2;
	lg.n++;
	lg.lvl = a0;
	lg.line = a3;
	return 0;
}
u64 lib_tisp_sync_sensor_attr(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	lg.n += 0x100;
	lg.ch = a0;
	memcpy(lg.blk, T32O_HOST(a1), T32_SYNC_BLK);
	return rnd();
}
static void o_log(u32 l, u32 line) { isp_printf(l, 0, 0, line); }
static void o_sync(u32 ch, const u8 *b) { lib_tisp_sync_sensor_attr(ch, T32O_PTR(b), 0, 0); }
static const struct t32_sync_ops ops = { o_log, o_sync };

/* ours on the low stack (its block buffer must have a 32-bit address) */
static u64 t32o_sync_ours(u32 sd, u32 at, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	return (u32)t32_ispcore_sync_sensor_attr(&ops, sd, at);
}

static u8 *world, snap[WSZ];
static u32 W, SD, AT;
static u32 badp(void) { return rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff; }
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
	SD = rnd() % 16 ? W : badp();
	t32_ic_w32(W, T32_SD_PRIV, rnd() % 12 ? W + 0x400 : badp());
	AT = rnd() % 6 ? W + 0x1000 : 0;
	t32_ic_w32(W + 0x1000, 52, W + 0x1200);
	t32_ic_w32(W + 0x1000, 56, W + 0x1600);
	*(u16 *)T32O_HOST(W + 0x1600 + 92) = (u16)(rnd() % 4);
	*(u8 *)T32O_HOST(W + 0x1200 + 196) = rnd() % 3 ? 0 : rnd();
	memset(&lg, 0, sizeof(lg));
	rs = save;
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;
	for (it = 0; it < 30000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))t32o_sync_ours, SD, AT, 0, 0, 0);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ispcore_sync_sensor_attr, SD, AT, rnd(), rnd(), 0);
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispsync_oracle: MISMATCH seed %08x: %d/%d log %x/%x\n", seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispsync_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
