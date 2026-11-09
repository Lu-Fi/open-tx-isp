// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_ae_api_ev_alloc_calc (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc, run unmodified) against t32_aev_api_ev_alloc_calc of
 * driver/t32/tx_isp_t32_aev.h.  Both sides run from the same random state
 * (channel state, tuning, buffer) and use the same vendor fix_point_* functions
 * (our side through hooks and t32o_call).  Compared: the three outputs, the
 * return value and the whole arena.  Not covered: fix_point_* themselves
 * (vendor code on both sides).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_aev.h"

#define WEAKD __attribute__((weak))
WEAKD u32 t32_data_e1588[4], t32_data_e15a8[4];

u64 tisp_ae_api_ev_alloc_calc(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6);
u64 fix_point_div_32(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult2_32(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_div_64(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult2_64(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult3_64(u32 a0, u32 a1, u32 a2, u32 a3);

#define CALLF(f, a, b, c, d, e) t32o_call((t32o_fn)(void (*)(void))(f), a, b, c, d, e)

#define OFF_ST		0x0000u
#define OFF_TUNE	0x1000u
#define OFF_BUF		0x2000u
#define OFF_CTX		0x5400u
#define OFF_OUT		0x5500u
#define ARENA		0x5600u

struct par {
	u32 ch, a2, a3;
};

struct snap {
	u8 mem[ARENA];
	u64 ret;
};

static u32 rs = 0x7a3c19u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rsh(void) { return rnd() >> (rnd() % 28); }

static void put32(u8 *p, u32 v) { memcpy(p, &v, 4); }
static void put16(u8 *p, u32 v) { u16 x = (u16)v; memcpy(p, &x, 2); }

static u32 h_div32(u32 sh, u32 a, u32 b) { return (u32)CALLF(fix_point_div_32, sh, a, b, 0, 0); }
static u32 h_mult2_32(u32 sh, u32 a, u32 b) { return (u32)CALLF(fix_point_mult2_32, sh, a, b, 0, 0); }
static t32_u64 h_div64(u32 sh, t32_u64 a, t32_u64 b)
{
	u64 r;

	t32o_arg5 = (u32)(b >> 32);
	r = CALLF(fix_point_div_64, sh, 0, (u32)a, (u32)(a >> 32), (u32)b);
	t32o_arg5 = 0;
	return r;
}
static t32_u64 h_mult2_64(u32 sh, t32_u64 a, t32_u64 b)
{
	u64 r;

	t32o_arg5 = (u32)(b >> 32);
	r = CALLF(fix_point_mult2_64, sh, 0, (u32)a, (u32)(a >> 32), (u32)b);
	t32o_arg5 = 0;
	return r;
}
static t32_u64 h_mult3_64(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c)
{
	u64 r;

	t32o_arg5 = (u32)(b >> 32);
	t32o_arg6 = (u32)c;
	t32o_arg7 = (u32)(c >> 32);
	r = CALLF(fix_point_mult3_64, sh, 0, (u32)a, (u32)(a >> 32), (u32)b);
	t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
	return r;
}
static const struct t32_aev_ops OPS = { h_div32, h_mult2_32, h_div64, h_mult2_64, h_mult3_64 };

static u8 img[ARENA];
static u8 *base_a;
static struct snap sref, sour;
static unsigned int ncmp;

static void gen(struct par *p)
{
	u32 i, k, sh, mode, n, m, lo, B, C, A, base, pos, step;
	t32_u64 xx;
	unsigned int sel = rnd() % 4;

	for (i = 0; i < ARENA; i++)
		img[i] = sel ? (u8)rnd() : (u8)(rnd() & 7);
	sh = rnd() % 5 ? 4 + rnd() % 9 : rnd() % 17;
	mode = rnd() % 5 ? 1 : rnd() % 3;
	n = 1 + rnd() % (rnd() & 1 ? 6 : 40);
	put16(img + OFF_TUNE + 1736, sh);
	put16(img + OFF_TUNE + 2920, mode);
	put16(img + OFF_TUNE + 2922, rnd() % 3 ? rnd() % 3 : rnd());
	put16(img + OFF_TUNE + 2924, rnd() % 4 ? rnd() % 200 : rnd());
	img[OFF_BUF + 12842] = (u8)n;
	A = rsh(); B = 1 + rsh(); C = 1 + rsh();
	if (rnd() & 1) { B = 1 + rnd() % 3000; C = 1 + rnd() % 3000; }
	if (rnd() % 4 == 0) A = rnd() % 5;
	put32(img + OFF_ST + 608, A);
	put32(img + OFF_ST + 612, B);
	put32(img + OFF_ST + 620, C);
	put32(img + OFF_ST + 624, rnd() & 1 ? rsh() : rnd() % 4000);
	put32(img + OFF_ST + 628, rsh());
	put32(img + OFF_ST + 636, rsh());
	p->ch = rnd() & 3;
	p->a3 = rnd() % 6 ? 0 : (rnd() & 1 ? rnd() % 3 : rnd());
	p->a2 = rnd() & 1 ? rsh() : rnd() % 5000;
	/* pre-values on the vendor maths, to place the table / a2 around them */
	m = h_mult2_32(sh, B, C);
	xx = h_div64(sh, (t32_u64)p->a3 << 32 | p->a2, m);
	lo = (u32)xx;
	if (rnd() % 3 == 0 && !(xx >> 32)) {
		/* a2 around mult2_32(m, amax) for the non-mode-1 test */
		u32 r = h_mult2_32(sh, m, A << (sh & 31));

		p->a2 = r + (u32)(int)(rnd() % 5) - 2;
		if (rnd() & 1)
			p->a3 = 0;
	}
	/* ascending table, thresholds around lo (<< sh) */
	pos = rnd() % (n + 3);
	step = rnd() % 4 ? 1 + rnd() % 20 : 1 + rnd() % 4000;
	base = (lo >> (sh & 31)) + (rnd() & 1);
	for (k = 0; k < 64; k++) {
		long v = (long)base + ((long)k - (long)pos) * (long)step;

		if (rnd() % 8 == 0)
			put32(img + OFF_ST + 1700 + 4 * k, rsh());
		else
			put32(img + OFF_ST + 1700 + 4 * k, v < 0 ? 0 : (u32)v);
	}
	if (rnd() % 3 == 0) {	/* table entries n / n-1 around the limit (20 << sh) + (E << sh) */
		u32 E = t32_ld32(img, OFF_ST + 624), lim = (20U << (sh & 31)) + (E << (sh & 31));
		u32 am = A << (sh & 31), tv;

		if (am) {	/* v = div32(tbl[n] << sh, amax) ~ tbl[n] << sh << sh / amax */
			tv = (u32)(((t32_u64)lim * am) >> (2 * sh));
			put32(img + OFF_ST + 1700 + 4 * n, tv + (u32)(int)(rnd() % 7) - 3);
		}
		put32(img + OFF_ST + 1700 + 4 * (n - 1), (lim >> (sh & 31)) + (u32)(int)(rnd() % 3) - 1);
	}
	if (rnd() % 3 == 0)	/* E near the table (limits) */
		put32(img + OFF_ST + 624, (lo >> (sh & 31)) + rnd() % 50);
}

static void run_side(int side, const struct par *p, struct snap *s)
{
	u8 *A;
	u32 ch = p->ch;
	u32 *out;

	t32o_reset();
	A = t32o_alloc(ARENA);
	if (!side)
		base_a = A;
	else if (A != base_a) {
		fprintf(stderr, "arena address differs between runs\n");
		exit(2);
	}
	memcpy(A, img, ARENA);
	put32(A + OFF_CTX + 0, T32O_PTR(A + OFF_TUNE));
	put32(A + OFF_CTX + 4, T32O_PTR(A + OFF_BUF));
	t32_data_e1588[ch] = T32O_PTR(A + OFF_ST);
	t32_data_e15a8[ch] = T32O_PTR(A + OFF_CTX);
	out = (u32 *)(A + OFF_OUT);
	if (!side) {
		t32o_arg5 = T32O_PTR(out + 1);
		t32o_arg6 = T32O_PTR(out + 2);
		s->ret = CALLF(tisp_ae_api_ev_alloc_calc, ch, 0, p->a2, p->a3, T32O_PTR(out));
		t32o_arg5 = t32o_arg6 = 0;
	} else {
		s->ret = t32_aev_api_ev_alloc_calc(A + OFF_TUNE, A + OFF_BUF, A + OFF_ST, p->a2, p->a3, out,
						   out + 1, out + 2, &OPS);
		s->ret |= (u64)T32O_PTR(out + 2) << 32;
	}
	memcpy(s->mem, A, ARENA);
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000, c, i;

	for (c = 0; c < n; c++) {
		struct par p;

		gen(&p);
		run_side(0, &p, &sref);
		run_side(1, &p, &sour);
		ncmp++;
		if (sref.ret != sour.ret)
			printf("MISMATCH case %u: ret ref %016llx ours %016llx\n", c, (unsigned long long)sref.ret,
			       (unsigned long long)sour.ret);
		for (i = 0; i < ARENA; i++)
			if (sref.mem[i] != sour.mem[i]) {
				printf("MISMATCH case %u: arena +%x ref %02x ours %02x\n", c, i, sref.mem[i],
				       sour.mem[i]);
				return 1;
			}
		if (sref.ret != sour.ret)
			return 1;
	}
	printf("t32_aev_oracle: api_ev_alloc_calc %u compared, 0 mismatches\n", ncmp);
	return 0;
}
