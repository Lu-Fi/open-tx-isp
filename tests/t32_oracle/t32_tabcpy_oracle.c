// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: table_intp and memcpy_simd of driver/t32/tx_isp_t32_tabcpy.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc; table_intp
 * built through fix_op8_tabintp.sh, memcpy_simd through fix_op8_glue.sh).  Both
 * sides use the vendor's fix_point_intp (reference copy) for the
 * interpolation itself; compared are the calls handed to it (all six
 * arguments), the return values, the printk call (format, all three
 * words) and every byte of the destination buffers incl. canaries.
 * Inputs: tables of 1..40 entries with sorted / equal / unsorted keys,
 * keys below / on / between / above the entries; copies of -3..300 bytes,
 * NULL source or destination, unaligned buffers.  Exit 0 = all equal.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_tabcpy.h"

u64 table_intp(u32, u32, u32, u32);
u64 memcpy_simd(u32, u32, u32, u32);
u64 fix_point_intp(u32, u32, u32, u32, u32, u32);

enum { F_INTP = 1, F_PRINTK };
char t32_data_c963c[] = "BAD_PARAM: src %p, dst %p, size %d\n";	/* data.c strings */
char t32_data_bee70[] = "fix_point_intp";
char t32_data_c7a2c[] = "%s,%d: error\n";
static struct t32o8_log *cur;

/* the vendor's table_intp calls fix_point_intp: record, then run the reference copy */
u64 t32o8_fpi(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	struct t32o8_call *c = t32o8_rec(cur, F_INTP, a0, a1, a2, a3);
	u64 r;

	c->a[4] = a4; c->a[5] = a5;
	if (cur == &t32o8_v)	/* called from the vendor table_intp: already on the low stack */
		return fix_point_intp(a0, a1, a2, a3, a4, a5);
	t32o_arg5 = a5;
	r = t32o_call((t32o_fn)(void (*)(void))fix_point_intp, a0, a1, a2, a3, a4);
	t32o_arg5 = 0;
	return r;
}
u64 t32o8_printk(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 w[3] = { a1, a2, a3 };

	t32o8_woff = 1;
	t32o8_rec_fmt(cur, F_PRINTK, 0, T32O_HOST(a0), w, 3);
	return 0;
}
static int o_printk(const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	t32o8_woff = 1;
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(cur, F_PRINTK, 0, fmt, w, n);
	return 0;
}

static u32 rs = 0x7ab1e5edu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, k, failed = 0, compared = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;

	for (c = 0; c < n; c++) {
		u32 vr = 0, orr = 0, vr_hi = 0, orr_hi = 0;
		int bad = 0;
		u64 r;

		t32o_reset();
		t32o8_clear();
		if (c & 1) {
			u32 cnt = 1 + rnd() % 40, a0 = (rnd() & 3) ? rnd() % 33 : rnd() % 256, key;	/* fix_point_intp loops for >= 256 bits */
			struct t32_tab_entry *t = t32o_alloc(8 * 48);

			for (k = 0; k < cnt; k++) {
				t[k].value = (rnd() & 1) ? rnd() % 4096 : rnd();
				t[k].key = (rnd() % 4) ? k * 100 + rnd() % 100 : (k ? t[k - 1].key : rnd() % 50);
			}
			key = (rnd() % 3) ? rnd() % (cnt * 100 + 100) : (rnd() & 1) ? t[rnd() % cnt].key : rnd();
			cur = &t32o8_v;
			r = t32o_call((t32o_fn)(void (*)(void))table_intp, a0, T32O_PTR(t), cnt, key, 0);
			vr = (u32)r; vr_hi = (u32)(r >> 32);
			cur = &t32o8_o;
			r = t32_table_intp((t32_fix_point_intp_fn)t32o8_fpi, a0, t, cnt, key);
			orr = (u32)r; orr_hi = (u32)(r >> 32);
			/* the vendor's v1 is only defined when fix_point_intp ran */
			if (!t32o8_v.n)
				vr_hi = orr_hi = 0;
		} else {
			u8 *dv = t32o_alloc(400), *dO = t32o_alloc(400), *src = t32o_alloc(400);
			s32 len = (rnd() % 8) ? (s32)(rnd() % 301) : -(s32)(rnd() % 4);
			u32 sh = rnd() % 4, nul = rnd() % 10;

			for (k = 0; k < 400; k++) {
				src[k] = (u8)rnd();
				dv[k] = dO[k] = (u8)rnd();
			}
			cur = &t32o8_v;
			vr = (u32)t32o_call((t32o_fn)(void (*)(void))memcpy_simd, nul == 0 ? 0 : T32O_PTR(dv + sh),
					    nul == 1 ? 0 : T32O_PTR(src + sh), (u32)len, 0, 0);
			cur = &t32o8_o;
			orr = (u32)t32_memcpy_simd(o_printk, nul == 0 ? NULL : dO + sh, nul == 1 ? NULL : src + sh, len);
			/* the printk %p words: map our destination to the vendor's */
			for (k = 0; k < t32o8_o.n; k++)
				if (t32o8_o.c[k].a[2] == T32O_PTR(dO + sh))
					t32o8_o.c[k].a[2] = T32O_PTR(dv + sh);
			bad = memcmp(dv, dO, 400) != 0;
		}
		bad |= t32o8_cmp("tabcpy", failed < 8) || vr != orr || vr_hi != orr_hi;
		compared++;
		if (bad && failed++ < 8)
			printf("MISMATCH case %u: ret %08x:%08x / %08x:%08x\n", c & 1, vr_hi, vr, orr_hi, orr);
	}
	printf("t32_tabcpy_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
