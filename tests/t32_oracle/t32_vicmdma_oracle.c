// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_vic_mdma_enable (tx_isp_t32_blkrest.h) against the
 * vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case
 * random VIC (0..3), mode (0, 1, 2, other), stride, buffer count (0..6 and
 * large), base, VIC size table (t32_data_e3f00) and output format bytes
 * (t32_data_e3f34 +4 / +8: 0, 1, 2, other).  Compared: return value and
 * every register write (address, value, order).  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_blkrest.h"

u8 t32_data_e3f00[4 * 16] __attribute__((aligned(16)));
u8 t32_data_e3f34[16] __attribute__((aligned(16)));
extern u64 tisp_vic_mdma_enable(u32, u32, u32, u32), system_reg_write(u32, u32, u32, u32);

static u32 rs = 0x9e3779b9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 pick(void) { u32 r = rnd() % 5; return r < 3 ? r : (r == 3 ? rnd() & 0xff : 1); }

static struct t32o_log ol;
static void o_wr(u32 reg, u32 val) { system_reg_write(reg, val, 0, 0); }

int main(void)
{
	u8 *sen = t32o_alloc(16);
	unsigned long n, N = 30000, per[4] = { 0 };
	unsigned int i;

	for (n = 0; n < N; n++) {
		u32 r = rnd() % 5, mode = r < 3 ? r : rnd(), stride = (rnd() & 1) ? rnd() & 0x1fff : rnd();
		u32 cnt = (rnd() & 7) ? rnd() % 7 : rnd(), base = rnd(), rv, ro;
		struct t32_vmd_dim dim[4];
		struct t32_vmd_fmt fmt;

		for (i = 0; i < 16; i++) sen[i] = rnd();
		memcpy(sen, &mode, 4);
		sen[6] = rnd() & 3;
		for (i = 0; i < 64; i += 4) { u32 w = (rnd() & 1) ? rnd() & 0xfff : rnd(); memcpy(t32_data_e3f00 + i, &w, 4); }
		for (i = 0; i < 16; i++) t32_data_e3f34[i] = rnd();
		t32_data_e3f34[4] = pick(); t32_data_e3f34[8] = pick();
		memcpy(dim, t32_data_e3f00, sizeof(dim));
		memcpy(&fmt, t32_data_e3f34, sizeof(fmt));

		t32o_reset();
		ro = t32_vic_mdma_enable((const struct t32_vmd_sensor *)sen, stride, cnt, base, dim, &fmt, o_wr);
		ol = t32o_log;
		t32o_reset();
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_vic_mdma_enable, T32O_PTR(sen), stride, cnt, base, 0);

		if (rv != ro || t32o_log.n != ol.n) {
			printf("t32_vicmdma_oracle: FAIL case %lu: ret %#x/%#x writes %u/%u\n", n, rv, ro, t32o_log.n, ol.n);
			return 1;
		}
		for (i = 0; i < ol.n; i++)
			if (t32o_log.w[i].reg != ol.w[i].reg || t32o_log.w[i].val != ol.w[i].val) {
				printf("t32_vicmdma_oracle: FAIL case %lu write %u: %#x=%#x / %#x=%#x\n", n, i,
				       t32o_log.w[i].reg, t32o_log.w[i].val, ol.w[i].reg, ol.w[i].val);
				return 1;
			}
		per[mode < 3 ? mode : 3]++;
	}
	printf("t32_vicmdma_oracle: PASS %lu cases (mode 0 %lu, 1 %lu, 2 %lu, other %lu)\n", N, per[0], per[1], per[2], per[3]);
	return 0;
}
