// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_ae_fliker_detect (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc, run unmodified) against t32_aef_fliker_detect of
 * driver/t32/tx_isp_t32_aefu.h.  Both sides start from the same random state
 * block P, tuning Q, sensor block R and sample; the counter phase (counter %
 * 10: reset, collect 1..8, decide 9, negative) is chosen per case, and for
 * phase 9 the history, tolerance and thresholds are derived from a probe run so
 * that all-within-tolerance, partly-within, stored value above / below the
 * threshold and every combination of the decision history occur.  The vendor
 * sort (tisp_ae_sort_H2L) is the same vendor code on both sides (our side
 * through a hook and t32o_call).  Compared: the whole arena (P, Q, R, sample,
 * neighbours), the return value (counter and P) and the event/register logs.
 * Not covered: the sort itself and memset / memcpy (vendor / libc on both
 * sides), the per-channel table lookup (the port takes the resolved P, Q, R),
 * a history length outside 1..11 / 2..11 and a summed count outside 0..15 or
 * a history index beyond the stack copy (undefined in the vendor too).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_aefu.h"

extern u32 t32_data_e15d8[], t32_data_e15c8[], t32_data_e1588[];
u64 tisp_ae_fliker_detect(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_sort_H2L(u32 a0, u32 a1, u32 a2, u32 a3);

typedef int32_t s32;
#define CALLF(f, a, b, c, d, e) t32o_call((t32o_fn)(void (*)(void))(f), a, b, c, d, e)

#define OFF_P		0x0000u
#define OFF_Q		0x3000u
#define OFF_R		0x3100u
#define OFF_S		0x3a00u
#define OFF_D		0x3b00u		/* decoy for the other channels */
#define ARENA		0x3c00u
#define PSZ		11768u

static u32 rs = 0x51f7a3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static void put32(u8 *p, u32 o, u32 v) { memcpy(p + o, &v, 4); }
static u32 ld(const u8 *p, u32 o) { u32 v; memcpy(&v, p + o, 4); return v; }

static u32 lv[15], li[15];	/* static: below 4 GiB for t32o_call */
static void h_sort(u32 *val, u32 *idx)
{
	memcpy(lv, val, sizeof(lv));
	memcpy(li, idx, sizeof(li));
	CALLF(tisp_ae_sort_H2L, T32O_PTR(lv), T32O_PTR(li), 0, 0, 0);
	memcpy(val, lv, sizeof(lv));
	memcpy(idx, li, sizeof(li));
}
static const struct t32_aef_ops OPS = { h_sort };

struct par { u32 ch, junk; };
static u8 img[ARENA];
static struct par par;
static unsigned int nph[12], nl[8];

static s32 sm(u32 sh) { return (s32)rnd() >> sh; }
static u32 pick(u32 n) { return rnd() % n; }

static void gen(void)
{
	u32 i, ph, cnt, nlim, n2, tq, thr, sel;
	static u8 sc[ARENA];

	for (i = 0; i < ARENA; i++)
		img[i] = (u8)rnd();
	par.ch = pick(4);
	par.junk = pick(4) ? 0 : rnd() & 0xffffff00u;
	/* phase */
	sel = pick(20);
	if (sel < 7) { ph = 9; cnt = 10 * (pick(4) ? pick(1000) : pick(0x7fffff)) + 9; }
	else if (sel < 9) { ph = 0; cnt = 10 * pick(1000); }
	else if (sel < 17) { ph = 1 + pick(8); cnt = 10 * pick(1000) + ph; }
	else if (sel < 19) { ph = 10; cnt = (u32)-(s32)(1 + pick(100)); }	/* negative: rem <= 0 */
	else { ph = 0; cnt = 0x80000000u + 10 * pick(3); }
	if (sel == 19 && pick(2)) { cnt = 0x80000000u; }
	put32(img, OFF_P, cnt);
	/* previous / current sample sets and the diff sets: small values so the clamp is hit sometimes */
	for (i = 0; i < 225; i++) {
		u32 sh = pick(3) ? 20 : 8;

		put32(img, OFF_P + 4 + 4 * i, (u32)sm(sh));
		put32(img, OFF_P + 904 + 4 * i, (u32)sm(sh));
	}
	{
		u32 sh = 17 + pick(14);

		for (i = 0; i < 9 * 225; i++)
			put32(img, OFF_P + 2704 + 4 * i, (u32)sm(sh));
	}
	for (i = 0; i < 225 * 9; i += 1 + pick(40))
		put32(img, OFF_P + 2704 + 4 * i, 0);
	put32(img, OFF_P + 10804, (u32)sm(20));
	put32(img, OFF_P + 10808, pick(3) ? pick(2) : rnd());
	for (i = 0; i < 10; i++)
		put32(img, OFF_P + 10812 + 4 * i, (u32)sm(22));
	for (i = 0; i < 3; i++)
		put32(img, OFF_P + 11756 + 4 * i, pick(5) ? pick(2) : rnd());
	/* Q */
	nlim = pick(8) ? 1 + pick(15) : pick(3) == 0 ? 0 : (u32)-(s32)(1 + pick(5));
	if (pick(5) == 0)
		nlim = 15;
	if (nlim == 0 && pick(2))
		nlim = 1;
	n2 = ph == 9 ? 2 + pick(10) : 1 + pick(11);
	tq = pick(6) == 0 ? (u32)-(s32)pick(50) : pick(3) ? pick(3000) : pick(50);
	put32(img, OFF_Q + 0, (u32)sm(22));
	put32(img, OFF_Q + 4, tq);
	put32(img, OFF_Q + 8, nlim);
	put32(img, OFF_Q + 12, n2);
	/* phase 9: derive thr with a probe run, then place history, stored value, threshold */
	if (ph == 9) {
		u32 q0, mode = pick(10), st = pick(4);
		s32 t = (s32)tq;

		memcpy(sc, img, ARENA);
		t32_aef_fliker_detect(sc + OFF_P, sc + OFF_Q, sc + OFF_R, sc + OFF_S, &OPS);
		thr = ld(sc, OFF_P + 10812);
		/* the probe's own diff phase touched P+4/904/diff 8; the thr depends on diff sets only */
		for (i = 0; i < 10; i++) {
			s32 nz = t > 0 ? (s32)pick(2 * t + 1) - t : (s32)pick(3) - 1;

			if (mode < 5)
				put32(img, OFF_P + 10812 + 4 * i, thr + (u32)nz);
			else if (mode < 7)
				put32(img, OFF_P + 10812 + 4 * i, thr + (u32)(pick(2) ? t + 1 + (s32)pick(2) : -t - 1 - (s32)pick(2)) + (u32)nz * (pick(3) == 0));
			else if (mode == 7)
				put32(img, OFF_P + 10812 + 4 * i, thr + (u32)(i == 0 ? t + 1 : nz));
			else
				put32(img, OFF_P + 10812 + 4 * i, (u32)sm(20));
		}
		if (mode == 8 && n2 > 2)
			put32(img, OFF_P + 10812 + 4 * (pick(n2 - 1) + 1), thr + (u32)(pick(2) ? t + 1 : -t - 1));
		if (mode == 9)
			put32(img, OFF_P + 10812, thr + (u32)(pick(2) ? t + 1 : -t - 1));
		q0 = thr + (u32)((s32)pick(3) - 1);
		if (pick(6) == 0)
			q0 = (u32)sm(22);
		put32(img, OFF_Q + 0, q0);
		put32(img, OFF_P + 10804, q0 + (u32)((s32)pick(3) - 1));
		for (i = 0; i < 3; i++)
			put32(img, OFF_P + 11756 + 4 * i, st == 0 ? pick(2) : st == 1 ? (i == 2 ? rnd() : pick(2)) : pick(2) ? 1 : 0);
		if (pick(3) == 0) {
			u32 f = !((s32)ld(img, OFF_P + 10804) < (s32)q0);
			put32(img, OFF_P + 11756, f);
			put32(img, OFF_P + 11760, pick(2) ? f : !f);
		}
	}
	nph[ph]++;
	nl[nlim > 15 ? 0 : (nlim & 7)]++;
}

struct snap { u8 mem[ARENA]; u64 ret; u32 ev_n, printfs, nlog; u32 evlog[256][9]; };
static struct snap sref, sour;
static u8 *base_a;

static void run_side(int side, struct snap *s)
{
	u8 *A;
	u32 ch = par.ch, o;

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
	for (o = 0; o < 4; o++) {
		t32_data_e15d8[o] = T32O_PTR(A + OFF_D);
		t32_data_e15c8[o] = T32O_PTR(A + OFF_D + 16);
		t32_data_e1588[o] = T32O_PTR(A + OFF_D + 32);
	}
	t32_data_e15d8[ch] = T32O_PTR(A + OFF_P);
	t32_data_e15c8[ch] = T32O_PTR(A + OFF_Q);
	t32_data_e1588[ch] = T32O_PTR(A + OFF_R);
	if (!side)
		s->ret = CALLF(tisp_ae_fliker_detect, ch | par.junk, T32O_PTR(A + OFF_S), 0, 0, 0);
	else
		s->ret = (u64)T32O_PTR(A + OFF_P) << 32 |
			 t32_aef_fliker_detect(A + OFF_P, A + OFF_Q, A + OFF_R, A + OFF_S, &OPS);
	memcpy(s->mem, A, ARENA);
	s->ev_n = t32o_ev_n;
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog) < sizeof(t32o_evlog) ? sizeof(s->evlog) : sizeof(t32o_evlog));
	s->printfs = t32o_log.printfs;
	s->nlog = t32o_log.n;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000, c, i;

	for (c = 0; c < n; c++) {
		gen();
		run_side(0, &sref);
		run_side(1, &sour);
		for (i = 0; i < ARENA; i++)
			if (sref.mem[i] != sour.mem[i]) {
				printf("MISMATCH case %u: arena +%x ref %02x ours %02x\n", c, i, sref.mem[i], sour.mem[i]);
				return 1;
			}
		if (sref.ret != sour.ret || sref.ev_n != sour.ev_n || sref.printfs != sour.printfs || sref.nlog != sour.nlog) {
			printf("MISMATCH case %u: ret %llx/%llx ev %u/%u\n", c, (unsigned long long)sref.ret,
			       (unsigned long long)sour.ret, sref.ev_n, sour.ev_n);
			return 1;
		}
	}
	printf("t32_aef_oracle: fliker_detect %u compared, 0 mismatches (phases", n);
	for (i = 0; i < 11; i++)
		printf(" %u", nph[i]);
	printf(")\n");
	return 0;
}
