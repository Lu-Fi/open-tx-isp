// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_ae_ev_list_alloc_calc (Matt Davis' recovered C, PR #15
 * @ 2e2e06bc, run unmodified) against t32_aev_list_alloc_calc of
 * driver/t32/tx_isp_t32_aev.h.  Both sides run from the same random state
 * (channel state, tuning, buffer, exposure list) and use the same vendor
 * fix_point_* functions (our side through hooks and t32o_call).  The two frame
 * rate callees tisp_set_ae_sensor_fps / tisp_ae_max_exp_calc_fps are host stubs
 * on both sides that log into t32o_evlog (call order, channel, value) and
 * return a value chosen per case.  Compared: the three outputs, the whole arena
 * (st, tune[1664], buf clamps), the event log and the printf counter.  Not
 * covered: fix_point_* and the two callees themselves (vendor code / stubs on
 * both sides), the stale 3rd/4th argument registers of tisp_ae_max_exp_calc_fps
 * (not compared), the vendor's leftover return value.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_aev.h"

#define WEAKD __attribute__((weak))
WEAKD u32 t32_data_e1588[4], t32_data_e15a8[4];

u64 tisp_ae_ev_list_alloc_calc(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7);
/* the vendor's selector jump table (.rodata + 0x1f3c, data.c:2298..2305) */
u32 t32_data_bf35c[8] = { 0x2cb60, 0x2cd50, 0x2cf7c, 0x2d154, 0x2d37c, 0x2d558, 0x2d768, 0x2e094 };
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
/* the two frame rate callees: host stubs, shared by both sides, logging into t32o_evlog */
static u32 g_mret;
static void evpush(u32 tag, u32 a, u32 b)
{
	if (t32o_ev_n < T32O_EVLOG) {
		t32o_evlog[t32o_ev_n][0] = tag;
		t32o_evlog[t32o_ev_n][1] = a;
		t32o_evlog[t32o_ev_n][2] = b;
	}
	t32o_ev_n++;
}
u64 tisp_set_ae_sensor_fps(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	evpush(1, a0, 0);
	return 0;
}
u64 tisp_ae_max_exp_calc_fps(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;	/* stale registers in the vendor code: not compared */
	evpush(2, a0, a1);
	return g_mret;
}
static void h_set_fps(u32 ch) { CALLF(tisp_set_ae_sensor_fps, ch, 0, 0, 0, 0); }
static u32 h_max_exp(u32 ch, u32 ev) { return (u32)CALLF(tisp_ae_max_exp_calc_fps, ch, ev, 0, 0, 0); }

/* probe ops: the same maths, remembering the first div64 divisor and the 32 bit results */
static t32_u64 pr_div;
static int pr_have;
static u32 pr_c[64];
static unsigned int pr_n;
static void pr_add(u32 v) { if (pr_n < 64) pr_c[pr_n++] = v; }
static u32 p_div32(u32 sh, u32 a, u32 b) { u32 r = h_div32(sh, a, b); pr_add(r); return r; }
static u32 p_mult2_32(u32 sh, u32 a, u32 b) { u32 r = h_mult2_32(sh, a, b); pr_add(r); return r; }
static t32_u64 p_div64(u32 sh, t32_u64 a, t32_u64 b)
{
	if (!pr_have) {
		pr_have = 1;
		pr_div = b;
	}
	return h_div64(sh, a, b);
}
static void p_set(u32 ch) { (void)ch; }
static u32 p_max(u32 ch, u32 ev) { (void)ch; (void)ev; return g_mret; }
static const struct t32_aev_ops POPS = { p_div32, p_mult2_32, p_div64, h_mult2_64, h_mult3_64, p_set, p_max };
static const struct t32_aev_ops OPS = { h_div32, h_mult2_32, h_div64, h_mult2_64, h_mult3_64, h_set_fps, h_max_exp };

struct par {
	u32 ch, a2, a3, k;
};

struct snap {
	u8 mem[ARENA];
	u32 ev_n, evlog[T32O_EVLOG][9];
	unsigned int printfs, nlog;
	u64 ret;
};

static u8 img[ARENA];
static u8 *base_a;
static struct snap sref, sour;
static unsigned int ncmp, nsel[9];

static u32 ld(u32 off) { u32 v; memcpy(&v, img + off, 4); return v; }
static u32 near(u32 v)
{
	switch (rnd() % 6) {
	case 0: return v;
	case 1: return v + 1;
	case 2: return v ? v - 1 : 0;
	case 3: return v + rnd() % 30;
	case 4: return v > 30 ? v - rnd() % 30 : v;
	default: return rnd() % 4 ? rsh() % 4000 : rnd();
	}
}
/* X with div64(sh, X, d) ~ T */
static void xfor(struct par *p, u32 sh, t32_u64 T, t32_u64 d)
{
	t32_u64 x = h_mult2_64(sh, T, d);

	x += (t32_u64)(int)(rnd() % 3) - 1;
	if (rnd() % 12 == 0)
		x = (x & 0xffffffffu) | (t32_u64)(rnd() % 3) << 32;
	p->a2 = (u32)x;
	p->a3 = (u32)(x >> 32);
}
static void put64(u8 *q, t32_u64 v) { put32(q, (u32)v); put32(q + 4, (u32)(v >> 32)); }

static void gen_list(struct par *p, u32 sh, u32 sx)
{
	u32 s5, i, f1, f2, f3, nf1, pe, sE = ld(OFF_ST + 624);
	t32_u64 X, e;
	u32 L = OFF_ST + 672, E = OFF_ST + 80;
	unsigned int ck;

	/* the list entries (16 bytes: ?, exposure, gain, gain): exposures near each other */
	for (i = 0; i < 18; i++) {
		put32(img + L + 16 * i + 4, 1 + rnd() % (rnd() % 3 ? 400 : 6));
		put32(img + L + 16 * i + 8, rnd() % 12 ? (1U << sx) + rnd() % ((4U << sx) | 1) : rnd() % 3 ? 0 : rsh());
		put32(img + L + 16 * i + 12, (1U << sx) + rnd() % ((6U << sx) | 1));
	}
	s5 = rnd() % 5 == 0 ? 15 : rnd() % 16;
	f1 = ld(L + 16 * s5 + 4);
	f2 = ld(L + 16 * s5 + 8);
	f3 = ld(L + 16 * s5 + 12);
	nf1 = ld(L + 16 * s5 + 20);
	pe = h_mult2_32(sh, f2, f3);
	/* X from the wanted d = X / pe against f1 << sx and nf1 << sx */
	switch (rnd() % 7) {
	case 0: e = (t32_u64)f1 << sx; break;
	case 1: e = (t32_u64)nf1 << sx; break;
	case 2: e = ((t32_u64)f1 << sx) + (rnd() % 3) - 1; break;
	case 3: e = ((t32_u64)nf1 << sx) + (rnd() % 3) - 1; break;
	case 4: e = ((t32_u64)(f1 < nf1 ? f1 : nf1) << sx) / 2 + 1; break;
	case 5: e = ((t32_u64)(f1 > nf1 ? f1 : nf1) << sx) + (rnd() % ((100U << sx) | 1)); break;
	default: e = (t32_u64)(f1 < nf1 ? f1 : nf1) << sx; e += rnd() % ((t32_u64)(f1 > nf1 ? f1 - nf1 : nf1 - f1) << sx | 1); break;
	}
	xfor(p, sh, e, pe ? pe : 1);
	X = (t32_u64)p->a3 << 32 | p->a2;
	/* the 8 byte entries: ascending below X up to s5, then at / above X */
	ck = rnd() % 8;
	for (i = 0; i < 17; i++) {
		t32_u64 v;

		if (i <= s5)
			v = X - 1 - (t32_u64)(s5 - i) * (ck ? 1 + rnd() % 50 : 1 + rnd() % 3);
		else
			v = X + (i - s5 - 1) * (1 + rnd() % 50) + (rnd() % 3 ? 0 : 1);
		if (X < 400 && i <= s5)
			v = i;
		put64(img + E + 8 * i, v);
	}
	if (rnd() % 6 == 0 && s5 < 15) {	/* duplicate: entry s5 + 1 equals entry s5 (so the scan ends at s5) */
		memcpy(img + E + 8 * (s5 + 1), img + E + 8 * s5, 8);
		if (rnd() % 3 == 0)	/* same low word, other high word */
			put32(img + E + 8 * (s5 + 1) + 4, ld(E + 8 * s5 + 4) + 1 + rnd() % 2);
	} else if (rnd() % 5 == 0) {	/* X below / equal to the first entry */
		switch (rnd() % 4) {
		case 0: put64(img + E, X); break;
		case 1: put64(img + E, X + 1 + rnd() % 3); break;
		case 2: put64(img + E, X + ((t32_u64)1 << 32)); break;
		default: put64(img + E, X + rnd() % 3 - (rnd() & 1)); break;
		}
	}
	if (s5 == 0 && rnd() % 3 == 0)
		put64(img + E, X - (rnd() % 2));
	/* the stored position */
	{
		u32 pos = rnd() % 4 ? rnd() % 17 : rnd() % 3 ? s5 : s5 + 1;

		put16(img + OFF_BUF + 8576, pos);
	}
	/* the exposure limit sE near f1 / nf1 */
	switch (rnd() % 6) {
	case 0: put32(img + OFF_ST + 624, f1); break;
	case 1: put32(img + OFF_ST + 624, nf1); break;
	case 2: put32(img + OFF_ST + 624, f1 + 1); break;
	case 3: put32(img + OFF_ST + 624, nf1 + 1); break;
	case 4: put32(img + OFF_ST + 624, (f1 > nf1 ? f1 : nf1) + 1 + rnd() % 40); break;
	default: (void)sE; break;
	}
	/* st[1700] table: th near the exposure candidates; entries ascending */
	{
		u32 c[4] = { f1, nf1, (u32)(((t32_u64)(rnd() & 1 ? f1 : nf1) << sx) / (pe ? pe : 1)), 1 + rnd() % 100 };
		u32 th = c[rnd() % 4] + (u32)(int)(rnd() % 4) - 1;

		for (i = 0; i < 64; i++)
			put32(img + OFF_ST + 1700 + 4 * i, th + (i ? rnd() % 30 + i : 0));
		put32(img + OFF_ST + 1696, th + rnd() % 5);
		if (rnd() % 7 == 0)
			put32(img + OFF_ST + 1700, rsh());
	}
}

static void gen(struct par *p)
{
	u32 i, sh, sx, V, cur, c, k;
	unsigned int sel = rnd() % 4;
	t32_u64 T;

	for (i = 0; i < ARENA; i++)
		img[i] = sel ? (u8)rnd() : (u8)(rnd() & 7);
	/* the next channel's state block behind st (d3a40 + 2184 * (ch + 1)): zero with one pipeline */
	memset(img + OFF_ST + T32_AEV_ST_BYTES, 0, OFF_TUNE - OFF_ST - T32_AEV_ST_BYTES);
	sh = rnd() % 5 ? 4 + rnd() % 9 : rnd() % 17;
	if (rnd() % 40 == 0)
		sh = 31;
	if (rnd() % 60 == 0)
		sh = 32 + rnd() % 40;
	sx = sh & 31;
	put16(img + OFF_TUNE + 1736, sh);
	put16(img + OFF_TUNE + 2920, rnd() % 6 ? 1 : rnd() % 3);
	put16(img + OFF_TUNE + 2922, rnd() % 4 ? 1 + rnd() % 2 : rnd() % 5);
	put16(img + OFF_TUNE + 2924, rnd() % 4 ? rnd() % 200 : rnd());
	p->k = rnd() % 3 ? 50 : rnd() % 300;
	p->ch = rnd() & 3;
	/* ev state: st[608] .. st[636], gains near 1.0 .. 6.0 */
	put32(img + OFF_ST + 608, rnd() % 4 ? 1 + rnd() % 60 : rnd() % 3);
	put32(img + OFF_ST + 612, (1U << sx) + rnd() % ((5U << sx) | 1));
	put32(img + OFF_ST + 620, rnd() % 6 ? (1U << sx) + rnd() % ((5U << sx) | 1) : rsh());
	put32(img + OFF_ST + 624, rnd() % 3 ? 1 + rnd() % 400 : rsh());
	put32(img + OFF_ST + 628, rnd() % 3 ? (1U << sx) + rnd() % ((8U << sx) | 1) : rsh());
	put32(img + OFF_ST + 636, rnd() % 3 ? (1U << sx) + rnd() % ((8U << sx) | 1) : rsh());
	/* selector bytes */
	{
		u32 s = rnd() % 10;

		img[OFF_ST + 1276] = s & 1;
		img[OFF_ST + 1277] = (s >> 1) & 1;
		img[OFF_ST + 1279] = (s >> 2) & 1;
		if (s == 8) {
			img[OFF_ST + 1276] = rnd();
			img[OFF_ST + 1279] = rnd();
		}
		if (s == 9)
			img[OFF_ST + 1276] = img[OFF_ST + 1277] = img[OFF_ST + 1279] = 0;
		img[OFF_ST + 1284] = rnd() % 8 ? 0 : 1;
		if (rnd() % 20 == 0)
			img[OFF_ST + 1284] = rnd();
	}
	/* frame rate state: nv, t3, t4 (buf), t1, t2 (st) near each other */
	V = 5 + rnd() % 300;
	put32(img + OFF_BUF + 8684, near(V));
	put32(img + OFF_BUF + 8688, near(V));
	put32(img + OFF_BUF + 8692, near(V));
	cur = near(V);
	put32(img + OFF_BUF + 8608, cur);
	put32(img + OFF_BUF + 8612, near(ld(OFF_ST + 628)));
	put32(img + OFF_BUF + 8620, near(ld(OFF_ST + 636)));
	put32(img + OFF_ST + 1264, 1 + rnd() % 60);
	put32(img + OFF_ST + 1260, rnd() % 4 ? near(ld(OFF_ST + 1264)) : rnd() % 3 ? 0 : ld(OFF_ST + 1264));
	c = ld(OFF_ST + 1264);
	switch (rnd() % 8) {
	case 0: g_mret = c; break;
	case 1: g_mret = c + 1; break;
	case 2: g_mret = c - 1; break;
	case 3: g_mret = ld(OFF_ST + 1260); break;
	case 4: g_mret = ld(OFF_ST + 1260) + 1; break;
	case 5: g_mret = 0; break;
	case 6: g_mret = ld(OFF_BUF + 8692) + (rnd() % 3) - 1; break;
	default: g_mret = rnd() % 100; break;
	}
	put16(img + OFF_BUF + 8576, rnd() % 4 ? rnd() % 17 : rnd() % 4);
	/* the target x */
	{
		u32 st0 = img[OFF_ST + 1277] << 1 | img[OFF_ST + 1279] << 2 | img[OFF_ST + 1276];

		if (img[OFF_ST + 1284] != 1 && !(st0 - 1 < 7)) {
			gen_list(p, sh, sx);
			return;
		}
	}
	/* cases 1..7: probe the first divisor and the 32 bit results */
	{
		static u8 sv[ARENA];
		struct par q = *p;
		u32 cand[160], nc = 0, amx = ld(OFF_ST + 608) << sx, nv = ld(OFF_BUF + 8684), o[3];

		memcpy(sv, img, ARENA);
		pr_have = 0; pr_n = 0; pr_div = 1;
		t32_aev_list_alloc_calc(sv + OFF_TUNE, sv + OFF_BUF, sv + OFF_ST, q.ch, 12345, 0, o, o + 1, o + 2, q.k, &POPS);
		for (k = 0; k < pr_n; k++)
			cand[nc++] = pr_c[k];
		cand[nc++] = amx; cand[nc++] = nv << sx; cand[nc++] = nv; cand[nc++] = ld(OFF_BUF + 8688);
		cand[nc++] = ld(OFF_ST + 628); cand[nc++] = ld(OFF_ST + 636); cand[nc++] = ld(OFF_ST + 612);
		cand[nc++] = ld(OFF_ST + 620); cand[nc++] = ld(OFF_BUF + 8692); cand[nc++] = ld(OFF_ST + 1264);
		cand[nc++] = ld(OFF_ST + 1260); cand[nc++] = ld(OFF_ST + 624);
		T = cand[rnd() % nc];
		switch (rnd() % 6) {
		case 0: T += 1; break;
		case 1: T = T ? T - 1 : 0; break;
		case 2: T = (T << sx) | (rnd() % 3 == 0); break;
		case 3: T = rnd() % 20 ? T : (t32_u64)rnd() << 3; break;
		default: break;
		}
		if (!pr_div)
			pr_div = 1;
		xfor(p, sh, T, pr_div);
		if (rnd() % 10 == 0) {
			p->a3 = rnd() % 3;
			p->a2 = rnd() & 1 ? rsh() : rnd();
		}
	}
}

static void run_side(int side, const struct par *p, struct snap *s)
{
	u8 *A;
	u32 ch = p->ch;
	u32 *out;

	t32o_reset();
	t32o_ev_n = 0;
	memset(t32o_evlog, 0, sizeof(t32o_evlog));
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
		t32o_arg7 = p->k;
		s->ret = CALLF(tisp_ae_ev_list_alloc_calc, ch, 0, p->a2, p->a3, T32O_PTR(out));
		t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
	} else {
		t32_aev_list_alloc_calc(A + OFF_TUNE, A + OFF_BUF, A + OFF_ST, ch, p->a2, p->a3, out, out + 1,
					out + 2, p->k, &OPS);
		s->ret = 0;
	}
	memcpy(s->mem, A, ARENA);
	s->ev_n = t32o_ev_n;
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->printfs = t32o_log.printfs;
	s->nlog = t32o_log.n;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 40000, c, i;

	for (c = 0; c < n; c++) {
		struct par p;
		u32 st0;

		gen(&p);
		st0 = img[OFF_ST + 1277] << 1 | img[OFF_ST + 1279] << 2 | img[OFF_ST + 1276];
		nsel[img[OFF_ST + 1284] == 1 ? 7 : (st0 - 1 < 7 ? st0 : 0)]++;
		run_side(0, &p, &sref);
		run_side(1, &p, &sour);
		ncmp++;
		for (i = 0; i < ARENA; i++)
			if (sref.mem[i] != sour.mem[i]) {
				printf("MISMATCH case %u: arena +%x ref %02x ours %02x (sel %u)\n", c, i, sref.mem[i],
				       sour.mem[i], st0);
				return 1;
			}
		if (sref.ev_n != sour.ev_n || memcmp(sref.evlog, sour.evlog, sizeof(sref.evlog)) ||
		    sref.printfs != sour.printfs || sref.nlog != sour.nlog) {
			printf("MISMATCH case %u: event log (n %u/%u printfs %u/%u)\n", c, sref.ev_n, sour.ev_n,
			       sref.printfs, sour.printfs);
			return 1;
		}
	}
	printf("t32_aev3_oracle: ev_list_alloc_calc %u compared, 0 mismatches (sel", ncmp);
	for (i = 0; i < 8; i++)
		printf(" %u", nsel[i]);
	printf(")\n");
	return 0;
}
