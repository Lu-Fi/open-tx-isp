// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our fixed point helpers (driver/t32/tx_isp_t32_fixp.h) against the vendor
 * fix_point_add / _sub / _add_64 / _sub_64 / _add_32 / _sub_32 / _mult3 (+ mult2) / _intp and
 * tisp_ae_lib_div_64 / tisp_ae_lib_bilinear_intp, from Matt Davis' mechanical MIPS-to-C recovery
 * (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 *
 * Compared bit-exact: the return value (64 bit for the 64 bit functions, the low word for the
 * u32 ones) and the number of error prints.  Inputs: random words, small values, equal and
 * adjacent operands, every pointpos 0..63 (0..300 for intp).  Refused on purpose (counted, not
 * compared): intp with q >= 256 on the dividing path (vendor never ends), a div_64 divisor with a
 * zero low word and a non-zero high word (vendor divides by zero).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fixp.h"

u64 fix_point_add(u32, u32, u32, u32, u32, u32);
u64 fix_point_sub(u32, u32, u32, u32, u32, u32);
u64 fix_point_add_64(u32, u32, u32, u32, u32, u32);
u64 fix_point_sub_64(u32, u32, u32, u32, u32, u32);
u64 fix_point_sub_32(u32, u32, u32, u32);
int32_t fix_point_add_32(uint32_t, uint32_t, uint32_t);
u64 fix_point_mult2(u32, u32, u32, u32, u32, u32);
u64 fix_point_mult3(u32, u32, u32, u32, u32, u32, u32, u32);
u64 fix_point_intp(u32, u32, u32, u32, u32);
u64 tisp_ae_lib_div_64(u32, u32, u32, u32);
u64 tisp_ae_lib_bilinear_intp(u32, u32, u32, u32, u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define LO(v)	((u32)(v))
#define HI(v)	((u32)((u64)(v) >> 32))

static u32 rs = 0x51f0c3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* a random 64 bit operand of one of several shapes */
static u64 r64(void)
{
	switch (rnd() % 6) {
	case 0: return rnd() & 0xff;
	case 1: return rnd();
	case 2: return (u64)rnd() << 32 | rnd();
	case 3: return (u64)(rnd() & 0xffff) << 16;
	case 4: return (u64)(rnd() & 3) << 32 | rnd();
	default: return (u64)rnd() << (rnd() % 40);
	}
}

static unsigned int compared, refused, failed;

static void check(const char *what, u64 vendor, u64 ours, u32 vw, u32 ow, u32 a, u64 b, u64 c)
{
	compared++;
	if (vendor == ours && vw == ow)
		return;
	if (failed++ < 10)
		printf("MISMATCH %s: q/a %u b %llx c %llx: vendor %llx (%u prints) ours %llx (%u)\n", what, a,
		       (unsigned long long)b, (unsigned long long)c, (unsigned long long)vendor, vw,
		       (unsigned long long)ours, ow);
}

/* run one reference call; *pr = its error prints */
static u64 call(t32o_fn fn, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, u32 *pr)
{
	u64 r;

	t32o_reset();
	t32o_arg5 = a5;
	t32o_arg6 = a6;
	t32o_arg7 = a7;
	r = t32o_call(fn, a0, a1, a2, a3, a4);
	*pr = t32o_log.printfs;
	t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
	return r;
}

static void one_round(u32 c)
{
	u32 q = rnd() % 64, vw = 0, ow;
	u64 v;
	u64 a = r64(), b = r64(), x, x0, x1;
	u32 a32, b32, y0, y1;

	switch (c % 5) {	/* equal / adjacent operands */
	case 1: b = a; break;
	case 2: b = a + 1; break;
	case 3: b = a - 1; break;
	case 4: b = (a & 0xffffffff00000000ULL) | (u32)rnd(); break;	/* same high word */
	}
	if (c < 64)
		q = c;

	ow = 0;
	v = call(F(fix_point_add), q, 0, LO(a), HI(a), LO(b), HI(b), 0, 0, &vw);
	check("add", v, t32_fixp_add(q, a, b), vw, 0, q, a, b);
	v = call(F(fix_point_add_64), q, 0, LO(a), HI(a), LO(b), HI(b), 0, 0, &vw);
	check("add_64", v, t32_fixp_add(q, a, b), vw, 0, q, a, b);
	ow = 0;
	x = t32_fixp_sub(q, a, b, &ow);
	v = call(F(fix_point_sub), q, 0, LO(a), HI(a), LO(b), HI(b), 0, 0, &vw);
	check("sub", v, x, vw, ow, q, a, b);
	ow = 0;
	x = t32_fixp_sub(q, a, b, &ow);
	v = call(F(fix_point_sub_64), q, 0, LO(a), HI(a), LO(b), HI(b), 0, 0, &vw);
	check("sub_64", v, x, vw, ow, q, a, b);

	a32 = (u32)a;
	b32 = (u32)b;
	v = LO(call(F(fix_point_add_32), q, a32, b32, 0, 0, 0, 0, 0, &vw));
	check("add_32", v, t32_fixp_add_32(q, a32, b32), vw, 0, q, a32, b32);
	ow = 0;
	x = t32_fixp_sub_32(q, a32, b32, &ow);
	v = LO(call(F(fix_point_sub_32), q, a32, b32, 0, 0, 0, 0, 0, &vw));
	check("sub_32", v, x, vw, ow, q, a32, b32);

	x = r64();
	v = call(F(fix_point_mult2), q, 0, LO(a), HI(a), LO(b), HI(b), 0, 0, &vw);
	check("mult2", v, t32_fixp_mult2(q, a, b), vw, 0, q, a, b);
	v = call(F(fix_point_mult3), q, 0, LO(a), HI(a), LO(b), HI(b), LO(x), HI(x), &vw);
	check("mult3", v,
	      t32_fixp_mult3(q, a, b, x), vw, 0, q, a, b);

	/* intp(q, x0, x1, y0, y1) */
	{
		u32 iq = c < 301 ? c : rnd() % 300, ix0 = rnd() % 4 ? (u32)r64() : rnd() % 8, ix1, iy0 = (u32)r64(), iy1;

		ix1 = c % 3 == 0 ? ix0 : (c % 3 == 1 ? ix0 + (rnd() & 0xffff) : (u32)r64());
		iy1 = rnd() % 3 == 0 ? iy0 : (u32)r64();
		if (iq >= 256 && ix1 != ix0 && iy0 != iy1) {
			refused++;	/* vendor: 8 bit loop counter never reaches q */
		} else {
			ow = 0;
			x = t32_fixp_intp(iq, ix0, ix1, iy0, iy1, &ow);
			v = LO(call(F(fix_point_intp), iq, ix0, ix1, iy0, iy1, 0, 0, 0, &vw));
			check("intp", v, x, vw, ow, iq, ix0, ix1);
		}
	}

	/* div_64 */
	switch (c % 4) {
	case 0: b = rnd() % 4 ? (u32)r64() : 0; break;
	case 1: b = (u64)(rnd() % 4) << 32 | rnd(); break;
	case 2: b = (u64)(rnd() % 3) << 32; break;	/* zero low word */
	default: b = r64(); break;
	}
	if (b && !(u32)b) {
		refused++;
	} else {
		v = call(F(tisp_ae_lib_div_64), LO(a), HI(a), LO(b), HI(b), 0, 0, 0, 0, &vw);
		check("div_64", v,
		      t32_ae_lib_div_64(a, b), vw, 0, 0, a, b);
	}

	/* bilinear(x, x0, x1, y0, y1) */
	x0 = r64();
	switch (c % 6) {
	case 0: x1 = x0; break;
	case 1: x1 = x0 + (rnd() & 0xffff); break;
	case 2: x1 = x0 - (rnd() & 0xffff); break;
	case 3: x1 = (x0 & 0xffffffff00000000ULL) | (u32)rnd(); break;
	default: x1 = r64(); break;
	}
	switch (rnd() % 5) {
	case 0: x = x0; break;
	case 1: x = x0 + (rnd() & 0xff); break;
	case 2: x = x0 - (rnd() & 0xff); break;
	case 3: x = (x0 & 0xffffffff00000000ULL) | (u32)rnd(); break;
	default: x = r64(); break;
	}
	y0 = rnd() % 3 ? rnd() & 0xffff : rnd();
	y1 = rnd() % 4 == 0 ? y0 : (rnd() % 3 ? rnd() & 0xffff : rnd());
	if (x1 != x0 && !(u32)(x1 > x0 ? x1 - x0 : x0 - x1)) {
		refused++;
	} else {
		v = LO(call(F(tisp_ae_lib_bilinear_intp), LO(x), HI(x), LO(x0), HI(x0), LO(x1), HI(x1), y0, y1, &vw));
		check("bilinear", v,
		      t32_ae_lib_bilinear_intp(x, x0, x1, y0, y1), vw, 0, y0, x, x1);
	}
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;

	for (c = 0; c < n; c++)
		one_round(c);
	printf("t32_fixp_oracle: %u compared, %u refused (vendor hang / divide by zero), %u mismatches\n",
	       compared, refused, failed);
	return failed ? 1 : 0;
}
