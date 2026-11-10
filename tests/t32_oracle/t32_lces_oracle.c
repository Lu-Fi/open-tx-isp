// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_lce_16bit_data_converge_simd of driver/t32/tx_isp_t32_lce.h against the unmodified
 * vendor tisp_lce_16bit_data_converge_simd (0x4ce94; Matt Davis' recovered C, PR #15 @ 2e2e06bc; the
 * MXU2 statements of that translation are checked against the MXU2 model by tests/t32_oracle/mxu2).
 * The SIMD function does elements 0..15 with MXU2 and 16..19 scalar (lo / hi masked to 8 bits,
 * gain not masked, and the two paths clamp in a different order, see the port).  Compared: all 20 output bytes, the target stays unchanged, and
 * the 12 guard bytes behind cur.  Random gain / lo / hi over the full 32 bit range, cur near and far from target.
 * Exit 0 = equal; 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_lce.h"

extern u64 tisp_lce_16bit_data_converge_simd(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

static u32 rs = 0x51a3c9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared;

static int one(u32 gain, u32 lo, u32 hi, const u8 *target, const u8 *cur)
{
	u8 oc[20], *vt, *vc;

	memcpy(oc, cur, 20);
	t32_lce_16bit_data_converge_simd(gain, lo, hi, target, oc);
	t32o_reset();
	vt = t32o_alloc(48); vc = t32o_alloc(48);
	memset(vt, 0x5a, 48); memset(vc, 0xa5, 48);
	memcpy(vt, target, 20); memcpy(vc, cur, 20);
	t32o_call((t32o_fn)(void (*)(void))tisp_lce_16bit_data_converge_simd, gain, lo, hi, T32O_PTR(vt), T32O_PTR(vc));
	compared++;
	if (memcmp(vt, target, 20))
		return printf("MISMATCH vendor changed target\n"), 1;
	if (memcmp(vc, oc, 20) || vc[20] != 0xa5 || vc[31] != 0xa5)
		{ unsigned k; printf("MISMATCH converge gain %u lo %u hi %u\n", gain, lo, hi); for (k = 0; k < 20; k++) printf("%d t%u c%u o%u v%u\n", k, target[k], cur[k], oc[k], vc[k]); return 1; }
	return 0;
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	int fail = 0;

	for (c = 0; c < n && !fail; c++) {
		u8 target[20], cur[20];
		u32 gain, lo, hi;
		unsigned int i;

		switch (rnd() % 4) {
		case 0: gain = rnd(); break;
		case 1: gain = rnd() & 0xff; break;
		case 2: gain = 128; break;
		default: gain = rnd() & 0x3ff;
		}
		lo = (rnd() % 4) ? rnd() % 24 : rnd();
		hi = (rnd() % 4) ? lo + rnd() % 30 : rnd();
		if (!(rnd() % 8)) { lo = rnd() & 0x1ff; hi = rnd() & 0x1ff; }
		for (i = 0; i < 20; i++) {
			target[i] = (u8)rnd();
			cur[i] = (rnd() % 5) ? (u8)(target[i] + (int)(rnd() % 81) - 40) : (u8)rnd();
		}
		fail = one(gain, lo, hi, target, cur);
	}
	printf("t32_lces_oracle: %u compared, %u mismatches\n", compared, fail ? 1u : 0u);
	return fail ? 1 : 0;
}
