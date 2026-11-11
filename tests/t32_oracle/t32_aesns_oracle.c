// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_ae_sensor_blk (driver/t32/tx_isp_t32_aesns.h, the dd2d4
 * sensor block of the driver's AE context) against the vendor
 * ispcore_sync_sensor_attr -> lib_tisp_sync_sensor_attr -> tisp_ae_par_sensor_trig
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Random 104 B video_in blocks
 * whose +52 / +56 point to random sensor attribute / channel info (channel 0,
 * the sensor's +196 byte 0 or not); compared: the whole 232 B dd2d4 entry
 * after the vendor chain and after ours (t32_ae_sensor_blk +
 * t32_lib_sync_sensor_attr, as tx_isp_t32_tisp.c does).  The block words the
 * sync does not copy (+80, +84, +108, +112) are not used by the driver.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispsync.h"
#include "tx_isp_t32_libwrap.h"
#include "tx_isp_t32_aesns.h"

u64 ispcore_sync_sensor_attr(u32 a0, u32 a1, u32 a2, u32 a3);
u64 lib_tisp_sync_sensor_attr(u32 a0, u32 a1, u32 a2, u32 a3);
extern u8 t32_data_dd2d4[0x400];
#define WSZ	0x2000u

static u32 rs = 0x1d2d4u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
static u32 o_trig(void *c, u32 ch) { (void)c; (void)ch; return 0; }

int main(void)
{
	unsigned int it, failed = 0, zero196 = 0;
	static u8 S[0x400], blk[T32_AESNS_BLK];
	struct t32_lib_hooks h;

	memset(&h, 0, sizeof(h));
	h.ae_sensor_trig = o_trig;
	for (it = 0; it < 30000; it++) {
		u8 *world;
		u32 W, i, v;

		t32o_reset();
		world = t32o_alloc(WSZ);
		W = T32O_PTR(world);
		for (i = 0; i < WSZ; i += 4) {
			v = rnd();
			memcpy(world + i, &v, 4);
		}
		t32_ic_w32(W, T32_SD_PRIV, W + 0x400);
		t32_ic_w32(W + 0x1000, 52, W + 0x1200);
		t32_ic_w32(W + 0x1000, 56, W + 0x1600);
		*(u16 *)T32O_HOST(W + 0x1600 + 92) = 0;
		if (rnd() % 3 == 0) {
			world[0x1200 + 196] = 0;
			zero196++;
		}
		memset(t32_data_dd2d4, 0, sizeof(t32_data_dd2d4));
		/* ours first: the reference overwrites the private copies, not the vin / si blocks */
		t32_ae_sensor_blk(blk, world + 0x1000, world + 0x1200);
		memset(S, 0, sizeof(S));
		t32_lib_sync_sensor_attr(&h, S, 0, blk);
		t32o_call((t32o_fn)(void (*)(void))ispcore_sync_sensor_attr, W, W + 0x1000, 0, 0, 0);
		if (memcmp(S, t32_data_dd2d4, T32_SENSOR_ATTR_COPY)) {
			if (failed++ < 5)
				for (i = 0; i < T32_SENSOR_ATTR_COPY; i++)
					if (S[i] != t32_data_dd2d4[i]) {
						printf("t32_aesns_oracle: FAIL case %u dd2d4 byte %u vendor %02x ours %02x\n", it,
						       i, t32_data_dd2d4[i], S[i]);
						break;
					}
		}
	}
	printf("t32_aesns_oracle: 30000 cases (%u with sensor +196 = 0), %u mismatches\n", zero196, failed);
	return failed ? 1 : 0;
}
