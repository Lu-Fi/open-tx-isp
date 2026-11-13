// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB long algorithm helper: our t32_awb_fixdiv64 (driver/t32/tx_isp_t32_awblong.h) against the vendor
 * fix_point_div (0x1d52c) from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc): (num << s) / den with
 * 64 bit operands (a2/a3 = numerator lo/hi, arg4/arg5 = divisor lo/hi).  Division by zero is not compared (the host
 * div64_u64 stub gives 0, the kernel would trap).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_awblong.h"

extern u32 t32o_arg5;
u64 fix_point_div(u32 a0, u32 a1, u32 a2, u32 a3, u32 arg4, u32 arg5);

static u32 rs = 0x2468aceu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 val(void)
{
	switch (rnd() % 8) {
	case 0: return rnd() % 5;
	case 1: return rnd() & 0xff;
	case 2: return rnd() & 0xffff;
	case 3: return 0xffffffffU - rnd() % 3;
	case 4: return 0x80000000U + rnd() % 3 - 1;
	case 5: return 1U << (rnd() % 32);
	case 6: return 0;
	default: return rnd();
	}
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 200000, cases = 0, failed = 0, half = 0, wide = 0;

	for (c = 0; c < n; c++) {
		u32 s = rnd() % 4 ? rnd() % 24 : rnd() % 80;
		u64 num = (u64)val() << 32 | val(), den = rnd() % 3 ? val() : (u64)val() << 32 | val(), ov, ours;

		if (rnd() % 3 == 0) {	/* remainder of exactly half the divisor */
			u64 d = (den | 1) << 1, k = (u64)val() << (rnd() % 32);

			den = d;
			num = k * d + d / 2;
			half++;
		}
		if (!den)
			continue;
		wide += (den >> 32) != 0;
		t32o_reset();
		t32o_arg5 = (u32)(den >> 32);
		ov = t32o_call((t32o_fn)(void (*)(void))fix_point_div, s, 0, (u32)num, (u32)(num >> 32), (u32)den);
		ours = t32_awb_fixdiv64(s, num, den);
		cases++;
		if (ov != ours && failed++ < 5)
			printf("MISMATCH s=%u num %016llx den %016llx vendor %016llx ours %016llx\n", s, (unsigned long long)num, (unsigned long long)den,
			       (unsigned long long)ov, (unsigned long long)ours);
	}
	printf("t32_awbdv_oracle: %u cases (%u half-divisor, %u with a 64 bit divisor), %u mismatches\n", cases, half, wide, failed);
	return failed != 0;
}
