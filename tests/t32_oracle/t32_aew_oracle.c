// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_ae_algo_handle, tisp_ae_sleep, tisp_ae_wake_up of
 * Matt Davis' recovered C (ref/, PR #15 @ 2e2e06bc) against our
 * t32_ae_algo_handle / t32_ae_sleep / t32_ae_wake_up (tx_isp_t32_stats.h).
 *
 * The vendor's dd2d4 + ch * 232 function table is filled with deterministic
 * stubs (hook_impl) that are shared with the port.  Compared: every register
 * write (reg, value, order), the AE buffer, the per-channel state words, the
 * event count and the last event.  Exit 1 on the first mismatch.
 *
 * Data objects the refs read and that env.c must provide (not invented here):
 * t32_data_dd2d4, e15a8, e1598, e1528, e153c, e1548/50/58/60/68, bf404,
 * tisp_event_push, tisp_reg_read (via t32o_rd_hook), printk,
 * tisp_log2_fixed_to_fixed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define SECB	3288u
#define SEC_OFF	0x68u
#define QB	12288u

/* weak: used when env.c does not provide the object (set per case below) */
#define T32O_WEAK __attribute__((weak))
u32 t32_data_e15a8[4] T32O_WEAK, t32_data_e1598[4] T32O_WEAK;
/* not in env.c: .bss words that only tisp_ae_sleep / tisp_ae_wake_up use (set per case below) */
u32 t32_data_e1528[5], t32_data_e153c[3];
/* printk file-name / format pointers of tisp_ae_wake_up: only passed on, never dereferenced by the stub */
u8 t32_data_bf394[32], t32_data_c870c[64];
u32 t32_data_e1568[2] T32O_WEAK, t32_data_e1558[2] T32O_WEAK, t32_data_e1548[2] T32O_WEAK;
u32 t32_data_e1560[2] T32O_WEAK, t32_data_e1550[2] T32O_WEAK;
/* not in env.c: read by tisp_ae_algo_handle only, set per case below */
u32 t32_data_bf404[2];
u8 t32_data_dd2d4[512] T32O_WEAK;

u64 tisp_ae_algo_handle(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_sleep(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult2_32(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_log2_fixed_to_fixed(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_math_exp2(u32 a0, u32 a1, u32 a2, u32 a3);

/* not in env.c: register read stub of tisp_ae_sleep, answered by the case's t32o_rd_hook */
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a2; (void)a3;
	return t32o_rd_hook ? t32o_rd_hook(a1) : 0;
}

/*
 * TEST MODEL, not the vendor function (its ref is not in ref/): fixed point
 * log2, input with a1 fraction bits, output with a2 fraction bits.  Weak, so
 * an env.c or imported ref definition replaces it.  Both the vendor code and
 * the port call it, so it only has to be deterministic.
 */
u64 tisp_log2_fixed_to_fixed(u32 a0, u32 a1, u32 a2, u32 a3) T32O_WEAK;
u64 tisp_log2_fixed_to_fixed(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u64 m;
	u32 e, i, frac = 0;

	(void)a3;
	if (!a0)
		return 0;
	for (e = 31; !(a0 >> e); e--)
		;
	m = (u64)a0 << (31 - e);
	for (i = 0; i < (a2 & 31); i++) {
		m = (m * m) >> 31;
		frac <<= 1;
		if (m >= (2ULL << 31)) {
			m >>= 1;
			frac |= 1;
		}
	}
	return (u32)(((int)e - (int)a1) * (int)(1u << (a2 & 31)) + (int)frac);
}

static u32 rs = 0x5d3a71u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 mixv(u32 which, u32 a)
{
	u32 x = a * 2654435761u ^ which * 0x9e3779b9u;

	x ^= x >> 15; x *= 0x85ebca6bu; x ^= x >> 13;
	return x;
}

/* shared hook model (blk = the vendor's stack block) */
static u32 hook_impl(u32 which, u32 a, u8 *blk)
{
	u32 m = mixv(which, a);

	switch (which) {
	case 140:
	case 148:
		t32_wr16(blk, 16, m >> 3);
		t32_wr16(blk, 18, m >> 11);
		return (m & 7) == 0 ? a : a + m % (a + 1);
	case 124:
	case 128:
	case 132:
	case 136:
		t32_wr16(blk, 4, m >> 2);
		t32_wr16(blk, 6, m >> 5);
		t32_wr16(blk, 14, m >> 9);
		t32_wr16(blk, 20, m >> 13);
		return (1 + (m & 3)) << 16 | ((m >> 8) & 0xffff);
	default:
		return m;
	}
}

#define HOOKS(X) X(124) X(128) X(132) X(136) X(140) X(148) X(168) X(172) X(184) X(188) X(192) X(196)
#define PH(n) static u32 ph##n(u32 v, u8 *b) { return hook_impl(n, v, b); }
#define RH(n) static u64 rh##n(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ (void)a2; (void)a3; return hook_impl(n, a0, (u8 *)T32O_HOST(a1)); }
HOOKS(PH)
HOOKS(RH)

static u32 callu(u64 (*f)(u32, u32, u32, u32), u32 a, u32 b, u32 c, u32 d)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))f, a, b, c, d, 0);
}
static u32 op_div(u32 s, u32 a, u32 b) { return callu(fix_point_div_32, s, a, b, 0); }
static u32 op_m2(u32 s, u32 a, u32 b) { return callu(fix_point_mult2_32, s, a, b, 0); }
static u32 op_m3(u32 s, u32 a, u32 b, u32 c) { return callu(fix_point_mult3_32, s, a, b, c); }
static u32 pl2(u32 v, u32 a, u32 b) { return callu(tisp_log2_fixed_to_fixed, v, a, b, 0); }
static u32 pe2(u32 v, u32 a, u32 b) { return callu(tisp_math_exp2, v, a, b, 0); }

static unsigned int pe_n, pe_ch;
static u32 pe_w[8];
static void pev(u32 ch, u32 *ev)
{
	pe_n++;
	pe_ch = ch;
	memset(pe_w, 0, sizeof(pe_w));
	memcpy(pe_w, ev, 6 * sizeof(u32));
}

static const struct t32_ae_ops ops = { op_div, op_m2, op_m3, 0, 0 };
static const struct t32_ae_algo_hooks hooks = {
	ph124, ph128, ph132, ph136, ph140, ph148, ph168, ph172, ph184, ph188, ph192, ph196,
	pl2, pe2, pev
};

struct res {
	unsigned int n;
	struct t32_stats_write w[T32_STATS_MAX_WRITES];
	u8 q[QB];
	u32 sl[6];
	unsigned int en, ech;
	u32 e2, e4, e5;
};
static struct res rr, pr, sc;
static unsigned int compared_a, compared_s, compared_w;

static void fail(const char *what, unsigned int i, const struct res *a, const struct res *b)
{
	printf("MISMATCH %s: writes vendor %u ours %u, first diff at #%u\n", what, a->n, b->n, i);
	if (i < T32_STATS_MAX_WRITES)
		printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i, a->w[i].reg, a->w[i].val, b->w[i].reg, b->w[i].val);
	printf("t32_aew_oracle: algo %u, sleep %u, wake %u compared, 1 mismatches\n", compared_a, compared_s, compared_w);
	exit(1);
}

static void cmp_writes(const char *what, const struct res *a, const struct res *b)
{
	unsigned int i;

	if (a->n != b->n)
		fail(what, a->n < b->n ? a->n : b->n, a, b);
	for (i = 0; i < a->n && i < T32_STATS_MAX_WRITES; i++)
		if (a->w[i].reg != b->w[i].reg || a->w[i].val != b->w[i].val)
			fail(what, i, a, b);
}

static void grab_log(struct res *o)
{
	unsigned int i;

	o->n = t32o_log.n;
	for (i = 0; i < o->n && i < T32_STATS_MAX_WRITES; i++) {
		o->w[i].reg = t32o_log.w[i].reg;
		o->w[i].val = t32o_log.w[i].val;
	}
}

static void port_algo(u32 ch, const u32 *inw, const u8 *q0, u32 flag, const u32 *sl0, struct res *o)
{
	struct t32_ae_algo_state st;
	struct t32_ae_algo_in in;

	memcpy(&in, inw, sizeof(in));
	memcpy(o->q, q0, QB);
	memcpy(o->sl, sl0, sizeof(o->sl));
	st.buf = o->q;
	st.flag = flag;
	st.e1568 = &o->sl[0]; st.e1558 = &o->sl[1]; st.e1548 = &o->sl[2];
	st.e1560 = &o->sl[3]; st.e1550 = &o->sl[4]; st.bf404 = &o->sl[5];
	pe_n = 0;
	o->n = t32_ae_algo_handle(ch, &in, &hooks, &ops, &st, o->w);
	o->en = pe_n; o->ech = pe_ch; o->e2 = pe_w[2]; o->e4 = pe_w[4]; o->e5 = pe_w[5];
}

static void set_tab(u32 ch)
{
	static const struct { u32 off; u64 (*f)(u32, u32, u32, u32); } t[] = {
		{ 124, rh124 }, { 128, rh128 }, { 132, rh132 }, { 136, rh136 }, { 140, rh140 }, { 148, rh148 },
		{ 168, rh168 }, { 172, rh172 }, { 184, rh184 }, { 188, rh188 }, { 192, rh192 }, { 196, rh196 },
	};
	unsigned int i;

	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		u32 v = T32O_PTR(t[i].f);

		memcpy(t32_data_dd2d4 + ch * 232 + t[i].off, &v, 4);
	}
}

static void run_algo(unsigned int c)
{
	static const u32 flags[4] = { 0, 1, 1, 2 };
	u32 ch = rnd() & 1, flag = flags[rnd() & 3], inw[12] = { 0 }, sl[6], cand[6], i;
	static u8 q0[QB];
	u8 *P, *Q, *inp;

	inw[4] = 1 + rnd() % ((c & 3) ? 4096 : 5);
	inw[5] = 1024 + rnd() % 59000;
	inw[6] = 1 + rnd() % 60000;
	inw[7] = 1024 + rnd() % 64512;
	inw[8] = 1 + rnd() % ((c & 1) ? 4096 : 5);
	inw[9] = 16 + rnd() % 60000;
	inw[10] = 1 + rnd() % 60000;
	inw[11] = rnd() % 16384;
	for (i = 0; i < QB; i++)
		q0[i] = (u8)rnd();
	for (i = 0; i < 6; i++)
		sl[i] = rnd();
	port_algo(ch, inw, q0, flag, sl, &sc);
	cand[0] = sc.sl[0]; cand[1] = sc.sl[1]; cand[2] = t32_ld32(sc.q, 8612);
	cand[3] = sc.sl[3]; cand[4] = sc.sl[4];
	cand[5] = flag == 1 ? t32_ld32(sc.q, 8628) : inw[9];
	for (i = 0; i < 6; i++)
		if (rnd() % 3 == 0)
			sl[i] = cand[i];
	port_algo(ch, inw, q0, flag, sl, &pr);

	t32o_reset();
	P = (u8 *)t32o_alloc(16);
	Q = (u8 *)t32o_alloc(QB);
	inp = (u8 *)t32o_alloc(64);
	memcpy(Q, q0, QB);
	t32_wr32(P, 0, 0);
	t32_wr32(P, 4, T32O_PTR(Q));
	t32_wr32(P, 8, flag);
	for (i = 0; i < 12; i++)
		t32_wr32(inp, 4 * i, inw[i]);
	t32_data_e15a8[ch] = T32O_PTR(P);
	t32_data_e1568[ch] = sl[0]; t32_data_e1558[ch] = sl[1]; t32_data_e1548[ch] = sl[2];
	t32_data_e1560[ch] = sl[3]; t32_data_e1550[ch] = sl[4]; t32_data_bf404[ch] = sl[5];
	set_tab(ch);
	t32o_ev_n = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_ae_algo_handle, ch, T32O_PTR(inp), 0, 0, 0);
	grab_log(&rr);
	memcpy(rr.q, Q, QB);
	rr.sl[0] = t32_data_e1568[ch]; rr.sl[1] = t32_data_e1558[ch]; rr.sl[2] = t32_data_e1548[ch];
	rr.sl[3] = t32_data_e1560[ch]; rr.sl[4] = t32_data_e1550[ch]; rr.sl[5] = t32_data_bf404[ch];
	rr.en = t32o_ev_n; rr.ech = t32o_ev_ch; rr.e2 = t32o_ev_w[2]; rr.e4 = t32o_ev_w[4]; rr.e5 = t32o_ev_w[5];

	compared_a++;
	cmp_writes("algo", &rr, &pr);
	if (memcmp(rr.q, pr.q, QB)) {
		for (i = 0; i < QB && rr.q[i] == pr.q[i]; i++)
			;
		printf("MISMATCH algo: buffer byte %u vendor %02x ours %02x (ch %u flag %u)\n", i, rr.q[i], pr.q[i], ch, flag);
		exit(1);
	}
	if (memcmp(rr.sl, pr.sl, sizeof(rr.sl))) {
		printf("MISMATCH algo: state words differ (ch %u flag %u)\n", ch, flag);
		exit(1);
	}
	if (rr.en != pr.en || (rr.en && (rr.ech != pr.ech || rr.e2 != pr.e2 || rr.e4 != pr.e4 || rr.e5 != pr.e5))) {
		printf("MISMATCH algo: events vendor %u ours %u\n", rr.en, pr.en);
		exit(1);
	}
}

static u32 rdseed;
static u32 rdh(u32 reg) { return mixv(rdseed, reg); }

static void run_sleep(void)
{
	struct t32_ae_sleep_state s;
	u32 ch = rnd() & 1, i;
	u32 v[8], o[8];

	rdseed = rnd();
	t32o_reset();
	t32o_rd_hook = rdh;
	for (i = 0; i < 5; i++)
		t32_data_e1528[i] = 0xdeadbee0u + i;
	for (i = 0; i < 3; i++)
		t32_data_e153c[i] = 0xcafe0000u + i;
	t32o_call((t32o_fn)(void (*)(void))tisp_ae_sleep, ch, 0, 0, 0, 0);
	for (i = 0; i < 3; i++)
		v[i] = t32_data_e153c[i];
	for (i = 0; i < 5; i++)
		v[3 + i] = t32_data_e1528[i];
	t32_ae_sleep(&s, rdh);
	for (i = 0; i < 3; i++)
		o[i] = s.e153c[i];
	for (i = 0; i < 5; i++)
		o[3 + i] = s.e1528[i];
	compared_s++;
	for (i = 0; i < 8; i++)
		if (v[i] != o[i]) {
			printf("MISMATCH sleep: word %u vendor %08x ours %08x\n", i, v[i], o[i]);
			exit(1);
		}
}

static void run_wake(void)
{
	static u8 sec[SECB];
	struct t32_ae_sleep_state sv;
	u32 w_ = 64 + rnd() % 4000, h_ = 64 + rnd() % 3000, i, pa;
	u8 *base, *info, *P, *Q;

	for (i = 0; i < SECB; i++)
		sec[i] = (rnd() % 3) ? (u8)rnd() : (u8)(rnd() & 7);
	sec[2770] = (u8)(rnd() % 16); sec[2771] = 0;
	sec[2774] = (u8)(rnd() % 16); sec[2775] = 0;
	sec[2768] = (u8)(rnd() % 64); sec[2769] = 0;
	sec[2772] = (u8)(rnd() % 64); sec[2773] = 0;

	t32o_reset();
	base = (u8 *)t32o_alloc(0x8000);
	info = (u8 *)t32o_alloc(64);
	memcpy(base + SEC_OFF, sec, SECB);
	info[0] = (u8)w_; info[1] = (u8)(w_ >> 8); info[4] = (u8)h_; info[5] = (u8)(h_ >> 8); info[12] = 25;
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32o_call((t32o_fn)(void (*)(void))tisp_ae_init, 0, T32O_PTR(info), 0, 0, 0);

	if (!t32_data_e15a8[0]) {
		P = (u8 *)t32o_alloc(16);
		t32_wr32(P, 0, T32O_PTR(base + SEC_OFF));
		t32_data_e15a8[0] = T32O_PTR(P);
	}
	P = (u8 *)T32O_HOST(t32_data_e15a8[0]);
	if (!t32_ld32(P, 4))
		t32_wr32(P, 4, T32O_PTR(t32o_alloc(QB)));
	Q = (u8 *)T32O_HOST(t32_ld32(P, 4));
	t32_wr16(Q, 8548, w_);
	t32_wr16(Q, 8550, h_);
	for (i = 0; i < 24; i++)
		Q[7448 + i] = (u8)rnd();
	if (!t32_data_e1598[0]) {
		u8 *e = (u8 *)t32o_alloc(64);

		t32_wr32(e, 8, rnd());
		t32_data_e1598[0] = T32O_PTR(e);
	}
	pa = t32_ld32((u8 *)T32O_HOST(t32_data_e1598[0]), 8);
	for (i = 0; i < 5; i++)
		sv.e1528[i] = t32_data_e1528[i] = rnd();
	for (i = 0; i < 3; i++)
		sv.e153c[i] = t32_data_e153c[i] = rnd();
	t32o_log.n = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_ae_wake_up, 0, 0, 0, 0, 0);
	grab_log(&rr);
	pr.n = t32_ae_wake_up(base + SEC_OFF, Q, pa, &sv, pr.w);
	compared_w++;
	cmp_writes("wake", &rr, &pr);
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;

	for (c = 0; c < n; c++)
		run_algo(c);
	for (c = 0; c < n / 6; c++)
		run_sleep();
	for (c = 0; c < n / 5; c++)
		run_wake();
	printf("t32_aew_oracle: algo %u, sleep %u, wake %u compared, 0 mismatches\n", compared_a, compared_s, compared_w);
	return 0;
}
