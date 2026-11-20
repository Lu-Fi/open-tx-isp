// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle (openimp #686): tisp_ae_roi_point (tx_isp_t32_stats.h,
 * t32_ae_roi_point) against the vendor function (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc).  Per case: random channel (high bits set now and
 * then), corners as random u32 (high halves set: the vendor masks them),
 * biased to valid windows, to the 0x100000 area limit and to x1 < x0 /
 * y1 < y0, AE state word st + 1520 = 0 / 1 / random, cfgMode (d03a0) 0 or 1.
 * Compared: return value, every system_reg_write (address, value, order),
 * the isp_printf count and every byte of the four AE state blocks.
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

extern u32 t32_data_e1588[4];
extern u64 tisp_ae_roi_point(u32, u32, u32, u32, u32);

static u32 rs = 0x6d2b79f5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define STB 2184
#define LOGN 8
struct olog { unsigned int n, msgs; u32 w[LOGN][2]; };
static struct olog ol;

static void o_wr(void *priv, u32 reg, u32 val)
{
	(void)priv;
	if (ol.n < LOGN) { ol.w[ol.n][0] = reg; ol.w[ol.n][1] = val; }
	ol.n++;
}
static void o_msg(void *priv, u32 which, u32 x0, u32 x1, u32 y0, u32 y1)
{
	(void)priv; (void)which; (void)x0; (void)x1; (void)y0; (void)y1;
	ol.msgs++;
}
static const struct t32_aea_roi_ops ops = { o_wr, o_msg };

static u32 coord(void)
{
	switch (rnd() % 4) {
	case 0: return rnd();				/* high half set */
	case 1: return rnd() & 0xffff;
	default: return rnd() % 4096;
	}
}

int main(void)
{
	u8 *st[2][4];
	unsigned long n, N = 40000, ok = 0, big = 0, bad = 0;
	u32 i, s;

	for (s = 0; s < 2; s++)
		for (i = 0; i < 4; i++)
			st[s][i] = t32o_alloc(STB);
	for (n = 0; n < N; n++) {
		u32 ch = rnd() & 3, cha = ch | (rnd() & 1 ? rnd() & ~0xffu : 0), cfg = rnd() & 1;
		u32 x0 = coord(), y0 = coord(), x1 = coord(), y1 = coord(), ro, rv;

		switch (rnd() % 4) {	/* windows: valid, at the area limit, reversed, random */
		case 0:
			x1 = (x0 & 0xffff) + rnd() % 2048; y1 = (y0 & 0xffff) + rnd() % 2048;
			break;
		case 1:
			x0 &= 0xff; y0 &= 0xff; x1 = x0 + 2048 + rnd() % 3; y1 = y0 + 2048 - rnd() % 3;
			break;
		case 2:
			x0 = 0xff00 | (rnd() & 0xff); y0 = rnd() & 0xffff; x1 = rnd() & 0xffff; y1 = 0xffff;
			break;
		}
		for (i = 0; i < 4; i++) {
			for (s = 0; s < STB; s++)
				st[0][i][s] = (u8)rnd();
			t32_wr32(st[0][i], 1520, rnd() % 3 == 0 ? rnd() : rnd() & 1);
			memcpy(st[1][i], st[0][i], STB);
		}

		memset(&ol, 0, sizeof(ol));
		ro = (u32)t32_ae_roi_point(st[1][cha & 0xff], (u8)cfg, x0, y0, x1, y1, &ops, NULL);

		t32o_reset();
		t32_data_d03a0[0] = (u8)cfg;
		for (i = 0; i < 4; i++)
			t32_data_e1588[i] = T32O_PTR(st[0][i]);
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_roi_point, cha, x0, y0, x1, y1);
		t32_data_d03a0[0] = 0;

		if (rv != ro || t32o_log.n != ol.n || t32o_log.printfs != ol.msgs) {
			printf("t32_aeroi_oracle: FAIL case %lu ch %#x cfg %u (%#x,%#x)-(%#x,%#x): ret %#x/%#x writes %u/%u msgs %u/%u\n",
			       n, cha, cfg, x0, y0, x1, y1, rv, ro, t32o_log.n, ol.n, t32o_log.printfs, ol.msgs);
			return 1;
		}
		for (i = 0; i < ol.n && i < LOGN; i++)
			if (t32o_log.w[i].reg != ol.w[i][0] || t32o_log.w[i].val != ol.w[i][1]) {
				printf("t32_aeroi_oracle: FAIL case %lu write %u: %#x=%#x / %#x=%#x\n", n, i,
				       t32o_log.w[i].reg, t32o_log.w[i].val, ol.w[i][0], ol.w[i][1]);
				return 1;
			}
		for (i = 0; i < 4; i++)
			if (memcmp(st[0][i], st[1][i], STB)) {
				printf("t32_aeroi_oracle: FAIL case %lu: AE state block %u differs\n", n, i);
				return 1;
			}
		if (ro == 0)
			ok++;
		else if (ol.msgs == 2)
			big++;
		else
			bad++;
	}
	printf("t32_aeroi_oracle: PASS %lu cases (%lu written, %lu area limit, %lu reversed)\n", N, ok, big, bad);
	return 0;
}
