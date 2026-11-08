// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, AWB colour temperature helpers 2: our t32_awb_interp1/2 and
 * t32_awb_zone_ct_weight (driver/t32/tx_isp_t32_awbct.h) against the vendor
 * ISPAWBInterpolation1 (0x1fdd0), ISPAWBInterpolation2 (0x1fe44) and
 * func_zone_ct_weight (0x1ff94) from Matt Davis' mechanical MIPS-to-C recovery
 * (ref/, PR #15 @ 2e2e06bc).  Interpolation: x around x0 / x1 (+-1), x0 == x1
 * (zero divisor), y1 below / above / equal y0, shifts 0..33.  Zone weight: x at
 * every threshold +-1 and between, thresholds in and out of order, spans of 0,
 * base above and below 1.0.  Compared: the return value and, for the weight, the
 * stored weight.
 * Exit 0 = equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_awbct.h"

static u32 rs = 0x2468aeu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 val(void)
{
	switch (rnd() % 6) {
	case 0: return rnd() % 8;
	case 1: return rnd() & 0xfff;
	case 2: return rnd() & 0xffff;
	case 3: return 0xffffffffU - rnd() % 3;
	case 4: return 1U << (rnd() % 32);
	default: return rnd();
	}
}

static u32 rmod(u32 m) { return m ? rnd() % m : 0; }

static u32 near(u32 v) { return v + rnd() % 3 - 1; }

static unsigned int cases, failed, hit[4];

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 200000;

	for (c = 0; c < n; c++) {
		u32 s = rnd() % 4 ? rnd() % 14 : rnd() % 34, x0 = rnd() % 2 ? rnd() % 5000 : val();
		u32 x1 = rnd() % 8 ? x0 + rnd() % 5000 : val(), y0 = val(), y1 = rnd() % 8 ? y0 + (rnd() % 2000) - 1000 : val();
		u32 x, ov, ours, th[4], base, scale, *vth, *vb, *vw, w;

		switch (rnd() % 5) {
		case 0: x = near(x0 << (s & 31)); break;
		case 1: x = near(x1 << (s & 31)); break;
		case 2: x = (x0 << (s & 31)) + rmod(((x1 - x0) << (s & 31)) + 2); break;
		default: x = val(); break;
		}
		t32o_reset();
		t32o_arg5 = y0;
		t32o_arg6 = y1;
		ov = (u32)t32o_call((t32o_fn)(void (*)(void))ISPAWBInterpolation1, 0, s, x, x0, x1);
		ours = t32_awb_interp1(s, x, x0, x1, y0, y1);
		cases++;
		if (ov != ours && failed++ < 5)
			printf("MISMATCH interp1 s=%u x=%u x0=%u x1=%u y0=%u y1=%u vendor %08x ours %08x\n", s, x, x0, x1, y0, y1, ov, ours);
		t32o_reset();
		t32o_arg5 = y0;
		t32o_arg6 = y1;
		ov = (u32)t32o_call((t32o_fn)(void (*)(void))ISPAWBInterpolation2, 0, s, x, x0, x1);
		ours = t32_awb_interp2(s, x, x0, x1, y0, y1);
		cases++;
		if (ov != ours && failed++ < 5)
			printf("MISMATCH interp2 s=%u x=%u x0=%u x1=%u y0=%u y1=%u vendor %08x ours %08x\n", s, x, x0, x1, y0, y1, ov, ours);

		/* zone weight */
		th[1] = rnd() % 2 ? rnd() % 4000 : val();
		th[0] = rnd() % 8 ? th[1] + rnd() % 3000 : val();
		th[2] = rnd() % 8 ? th[0] + rnd() % 3000 : val();
		th[3] = rnd() % 8 ? th[2] + rnd() % 3000 : val();
		switch (rnd() % 8) {
		case 0: x = near(th[0]); break;
		case 1: x = near(th[1]); break;
		case 2: x = near(th[2]); break;
		case 3: x = near(th[3]); break;
		case 4: x = th[1] + rmod(th[3] - th[1] + 2); break;
		default: x = rnd() % 2 ? th[1] + rnd() % 12000 : val(); break;
		}
		s = rnd() % 4 ? 8 + rnd() % 8 : rnd() % 34;
		base = rnd() % 2 ? (rnd() % (1U << (s & 31))) : val();
		scale = val();
		t32o_reset();
		vth = t32o_alloc(16);
		vb = t32o_alloc(8);
		vw = t32o_alloc(8);
		memcpy(vth, th, 16);
		vb[1] = base;
		vw[0] = 0xa5a5a5a5U;
		t32o_arg5 = T32O_PTR(vw);
		ov = (u32)t32o_call((t32o_fn)(void (*)(void))func_zone_ct_weight, x, T32O_PTR(vth), T32O_PTR(vb), s, scale);
		w = 0xa5a5a5a5U;
		ours = t32_awb_zone_ct_weight(x, th, base, s, scale, &w);
		cases++;
		hit[(x < th[3] && th[1] < x) ? (th[2] < x ? 1 : (x < th[0] ? 2 : 3)) : 0]++;
		if ((ov != ours || vw[0] != w) && failed++ < 5)
			printf("MISMATCH weight x=%u th %u %u %u %u base %u s=%u vendor %08x/%08x ours %08x/%08x\n", x, th[0], th[1], th[2], th[3], base, s, ov, vw[0], ours, w);
	}
	printf("t32_awbi_oracle: %u cases, weight zones outside/upper/lower/flat %u %u %u %u, %u mismatches\n", cases, hit[0], hit[1], hit[2], hit[3], failed);
	return failed != 0;
}
