// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our MSCA polyphase curve (driver/t32/tx_isp_t32_msca_curve.h,
 * t32_msca_curve) against the vendor's tisp_msca_curve_calc -> tisp_sin +
 * tisp_msca_normalized, from Matt Davis' mechanical MIPS-to-C recovery (ref/,
 * PR #15 @ 2e2e06bc, built for the host with env.c).
 *
 * The vendor derives the two Q14 ratios of a channel set from u16 size fields
 * (+26/+28 in, +32/+34 out; 872 bytes per set) and fills six 25-tap arrays
 * (50 bytes apart from +4: horizontal / vertical for each of 3 sets).  Compared
 * bit-exact: every tap of all arrays, for all 3 set selectors, over random input
 * and output sizes (including ratios above the 27306 limit, equal sizes, and
 * sizes of the 3840x2160 traces).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_curve.h"

static u32 rs = 0x5e19a3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put16(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8);
}

static unsigned int compared, failed;

static void run_case(u32 in_a, u32 out_a, u32 in_b, u32 out_b, unsigned int which)
{
	u8 *par, *outb;
	int ca[T32_CURVE_TAPS], cb[T32_CURVE_TAPS];
	unsigned int i, arr;

	t32o_reset();
	par = t32o_alloc(3 * 872 + 64);
	outb = t32o_alloc(512);
	t32_data_e2768[0] = T32O_PTR(par);
	t32_data_e28ac[0] = T32O_PTR(outb);
	put16(par + which * 872 + 28, in_a);	/* + 32 / + 28: first ratio */
	put16(par + which * 872 + 32, out_a);
	put16(par + which * 872 + 26, in_b);	/* + 34 / + 26: second ratio */
	put16(par + which * 872 + 34, out_b);
	t32o_call((t32o_fn)(void (*)(void))tisp_msca_curve_calc, 0, which, 0, 0, 0);

	t32_msca_curve(in_a, out_a, ca);
	t32_msca_curve(in_b, out_b, cb);
	compared++;
	/* first ratio -> array at +4, second -> +54 (each + 100 per set) */
	for (arr = 0; arr < 2; arr++) {
		const u8 *v = outb + (arr ? 54 : 4) + 100 * which;
		const int *c = arr ? cb : ca;

		for (i = 0; i < T32_CURVE_TAPS; i++) {
			int vv = (short)(v[2 * i] | v[2 * i + 1] << 8);

			if (vv != c[i]) {
				if (failed++ < 5)
					printf("MISMATCH set %u %s in %u out %u tap %u vendor %d ours %d\n", which,
					       arr ? "second" : "first", arr ? in_b : in_a, arr ? out_b : out_a, i, vv, c[i]);
				return;
			}
		}
	}
	if (!t32_data_e2898[0])
		failed++;
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;
	static const u32 sz[] = { 3840, 2160, 1920, 1080, 1280, 720, 640, 480, 320, 240, 2560, 1440 };

	for (c = 0; c < n; c++) {
		u32 ia, oa, ib, ob;

		if (c % 3 == 0) {
			ia = sz[rnd() % 12]; oa = sz[rnd() % 12]; ib = sz[rnd() % 12]; ob = sz[rnd() % 12];
		} else {
			ia = 1 + rnd() % 4096; oa = 1 + rnd() % 4096; ib = 1 + rnd() % 4096; ob = 1 + rnd() % 4096;
			if (c % 3 == 1) {	/* ratios in the working range */
				oa = 1 + rnd() % ia; ob = 1 + rnd() % ib;
			}
		}
		run_case(ia, oa, ib, ob, rnd() % 3);
	}
	printf("t32_msca_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
