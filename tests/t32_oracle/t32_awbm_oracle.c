// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB colour temperature helpers 1: our t32_awb_fixmul3 and
 * t32_awb_fixdiv (driver/t32/tx_isp_t32_awbct.h) against the vendor
 * fix_point_mult3_32 (0x1db9c) and fix_point_div_32 (0x1dbc8) from Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).  Operands at the u32
 * edges, shifts 0..70 (mult: 0..40), divisors 0, 1, powers of two, numerators that
 * leave a remainder of exactly half the divisor (the "equal" exit of the fraction
 * loop).  Compared: the 32 bit result.
 * Exit 0 = equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_awbct.h"

static u32 rs = 0x13579bu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 val(void)
{
	switch (rnd() % 7) {
	case 0: return rnd() % 5;
	case 1: return rnd() & 0xff;
	case 2: return rnd() & 0xffff;
	case 3: return 0xffffffffU - rnd() % 3;
	case 4: return 0x80000000U + rnd() % 3 - 1;
	case 5: return 1U << (rnd() % 32);
	default: return rnd();
	}
}

static unsigned int cases, failed, fails_div, eq_exit;

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 200000;

	for (c = 0; c < n; c++) {
		u32 s = rnd() % 4 ? rnd() % 20 : rnd() % 71, x = val(), y = val(), z = val(), ov, ours;

		t32o_reset();
		ov = (u32)t32o_call((t32o_fn)(void (*)(void))fix_point_mult3_32, s, x, y, z, 0);
		ours = t32_awb_fixmul3(s, x, y, z);
		cases++;
		if (ov != ours && failed++ < 5)
			printf("MISMATCH mult3 s=%u %08x %08x %08x vendor %08x ours %08x\n", s, x, y, z, ov, ours);

		if (rnd() % 3 == 0) {	/* remainder of exactly half the divisor */
			u32 d = (val() | 1) << 1, k = val() >> (rnd() % 32);

			y = d;
			x = k * d + d / 2;
			eq_exit++;
		} else {
			y = val();
		}
		s = rnd() % 4 ? rnd() % 40 : rnd() % 71;
		t32o_reset();
		ov = (u32)t32o_call((t32o_fn)(void (*)(void))fix_point_div_32, s, x, y, 0, 0);
		ours = t32_awb_fixdiv(s, x, y);
		cases++;
		if (ov != ours) {
			fails_div++;
			if (failed++ < 5)
				printf("MISMATCH div s=%u num %08x den %08x vendor %08x ours %08x\n", s, x, y, ov, ours);
		}
	}
	printf("t32_awbm_oracle: %u cases (%u half-divisor), %u mismatches (%u in div)\n", cases, eq_exit, failed, fails_div);
	return failed != 0;
}
