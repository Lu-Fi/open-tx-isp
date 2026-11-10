// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our log2 helpers (driver/t32/tx_isp_t32_log2.h) against the vendor
 * tisp_log2_int_to_fixed / _fixed_to_fixed / _int_to_fixed_64 / _fixed_to_fixed_64, from Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 *
 * Compared bit-exact: the returned word.  Inputs: 0, powers of two and their neighbours, random
 * words of every width, fp 0..255 (mostly 0..31), out 0..255 for the 32 bit forms, in_fp random.
 * Refused on purpose (counted, not compared): the _64 forms with out > 15 (the vendor shifts with
 * __lshrdi3 by a negative count, undefined in libgcc).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_log2.h"

u64 tisp_log2_int_to_fixed(u32, u32, u32, u32);
u64 tisp_log2_fixed_to_fixed(u32, u32, u32, u32);
u64 tisp_log2_int_to_fixed_64(u32, u32, u32, u32);
u64 tisp_log2_fixed_to_fixed_64(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

static u32 rs = 0x1092e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, refused, failed;

static void check(const char *what, u32 vendor, u32 ours, u64 v, u32 fp, u32 out)
{
	compared++;
	if (vendor == ours)
		return;
	if (failed++ < 10)
		printf("MISMATCH %s: v %llx fp %u out %u: vendor %08x ours %08x\n", what, (unsigned long long)v, fp,
		       out, vendor, ours);
}

static u64 val(u32 c)
{
	u32 sh = rnd() % 64;

	switch (c % 6) {
	case 0: return c < 64 ? 1ULL << c : 0;
	case 1: return (1ULL << sh) - 1;
	case 2: return (1ULL << sh) + (rnd() & 1);
	case 3: return rnd() >> (rnd() % 32);
	case 4: return (u64)rnd() << 32 | rnd();
	default: return ((u64)rnd() << 32 | rnd()) >> sh;
	}
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 40000;

	for (c = 0; c < n; c++) {
		u64 v = val(c);
		u32 w = (u32)v, fp = rnd() % 8 ? rnd() % 32 : rnd() % 256, out = rnd() % 4 ? rnd() % 16 : rnd() % 256;
		u32 in = rnd() % 4 ? rnd() % 32 : rnd(), r;

		if (c % 7 == 0)
			fp |= 0x100 * (rnd() & 0xff);	/* the vendor takes the low byte */
		if (c % 11 == 0)
			out |= 0x100 * (rnd() & 0xff);
		t32o_reset();
		r = (u32)t32o_call(F(tisp_log2_int_to_fixed), w, fp, out, 0, 0);
		check("int_to_fixed", r, t32_log2_int_to_fixed(w, fp, out), w, fp, out);
		r = (u32)t32o_call(F(tisp_log2_fixed_to_fixed), w, in, fp, 0, 0);
		check("fixed_to_fixed", r, t32_log2_fixed_to_fixed(w, in, fp), w, in, fp);
		if ((out & 0xff) > 15) {
			refused++;
		} else {
			r = (u32)t32o_call(F(tisp_log2_int_to_fixed_64), (u32)v, (u32)(v >> 32), fp, out, 0);
			check("int_to_fixed_64", r, t32_log2_int_to_fixed_64(v, fp, out), v, fp, out);
		}
		r = (u32)t32o_call(F(tisp_log2_fixed_to_fixed_64), (u32)v, (u32)(v >> 32), in, fp, 0);
		check("fixed_to_fixed_64", r, t32_log2_fixed_to_fixed_64(v, in, fp), v, in, fp);
	}
	printf("t32_log2_oracle: %u compared, %u refused (_64 with out > 15), %u mismatches\n", compared, refused,
	       failed);
	return failed ? 1 : 0;
}
