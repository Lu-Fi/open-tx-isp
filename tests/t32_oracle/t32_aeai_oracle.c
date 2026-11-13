// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE exposure info attribute (api_ae_get_exprinfo_attr /
 * api_ae_set_exprinfo_attr).  Runs the unmodified vendor functions (Matt
 * Davis' recovered C, PR #15 @ 2e2e06bc) and our t32_aea_*_exprinfo_attr from
 * driver/t32/tx_isp_t32_stats.h from the same random state and compares the
 * tune / buf / st blocks of the used and of a decoy channel, the dd2d4 table,
 * the input and output blocks, the returned step count, the calls of the
 * recording stubs private_msleep / tisp_math_exp2 / tisp_log2_fixed_to_fixed
 * (arguments in order), the register and event log.  private_msleep clears the
 * state byte st + 1285 after a chosen number of calls (1, 2, 49, 50, 51, ..),
 * so that the wait loop, its end and its timeout are all run.
 * Exit 0 = all equal, 1 = first mismatch, 2 = setup failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define TUNEB	0x1000u
#define BUFB	0x3400u
#define STB	0x1000u
#define OUTB	256u
#define INB	256u
#define FPSB	0x400u

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
extern u8 t32_data_dd2d4[0x400];

extern u64 api_ae_get_exprinfo_attr(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 api_ae_set_exprinfo_attr(u32 a0, u32 a1, u32 a2, u32 a3);

struct callrec {
	u32 n, id[24], a0[24], a1[24], a2[24];
};
static struct callrec cr;
static u8 *sleep_st;
static u32 sleep_calls, clear_after;

static void rec(u32 id, u32 a0, u32 a1, u32 a2)
{
	if (cr.n < 24) {
		cr.id[cr.n] = id; cr.a0[cr.n] = a0; cr.a1[cr.n] = a1; cr.a2[cr.n] = a2;
	}
	cr.n++;
}

u64 private_msleep(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (cr.n < 24)
		rec(1, a0, 0, 0);
	else
		cr.n++;
	if (++sleep_calls == clear_after)
		sleep_st[1285] = 0;
	return 0;
}

u64 tisp_math_exp2(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	rec(2, a0, a1, a2);
	return a0 * 2654435761u + a1 * 40503u + a2 * 7u + 12345u;
}

u64 tisp_log2_fixed_to_fixed(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	rec(3, a0, a1, a2);
	return a0 * 40503u + a1 * 2654435761u + a2 * 11u + 777u;
}

static void sleep_hook(void *priv, u32 ms) { (void)priv; private_msleep(ms, 0, 0, 0); }
static u32 exp2_hook(void *priv, u32 a0, u32 a1, u32 a2) { (void)priv; return (u32)tisp_math_exp2(a0, a1, a2, 0); }
static u32 log2_hook(void *priv, u32 a0, u32 a1, u32 a2) { (void)priv; return (u32)tisp_log2_fixed_to_fixed(a0, a1, a2, 0); }

static u32 rs = 0x6c078965u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_GET, C_SET, C_N };
static const char *const names[C_N] = { "get_exprinfo_attr", "set_exprinfo_attr" };

struct env {
	u8 *tune[2], *buf[2], *st[2], *in, *out;
};
static u8 d_tune[2][TUNEB], d_buf[2][BUFB], d_st[2][STB], d_in[INB], d_fps[FPSB];
struct snap {
	u8 tune[2][TUNEB], buf[2][BUFB], st[2][STB], in[INB], out[OUTB], fps[FPSB];
	u32 ret, evlog[T32O_EVLOG][9], evn, logn, printfs, sleeps;
	struct callrec cr;
};
static struct snap sr, so;

static int setup(struct env *e, u32 ch)
{
	u32 k, slot[2] = { ch, (ch + 1) & 3 };

	for (k = 0; k < 2; k++) {
		u8 *ctx = t32o_alloc(16);

		e->tune[k] = t32o_alloc(TUNEB);
		e->buf[k] = t32o_alloc(BUFB);
		e->st[k] = t32o_alloc(STB);
		if (!ctx || !e->tune[k] || !e->buf[k] || !e->st[k])
			return 2;
		memcpy(e->tune[k], d_tune[k], TUNEB);
		memcpy(e->buf[k], d_buf[k], BUFB);
		memcpy(e->st[k], d_st[k], STB);
		*(u32 *)(ctx + 0) = T32O_PTR(e->tune[k]);
		*(u32 *)(ctx + 4) = T32O_PTR(e->buf[k]);
		t32_data_e15a8[slot[k]] = T32O_PTR(ctx);
		t32_data_e1588[slot[k]] = T32O_PTR(e->st[k]);
	}
	e->in = t32o_alloc(INB);
	e->out = t32o_alloc(OUTB);
	if (!e->in || !e->out)
		return 2;
	memcpy(e->in, d_in, INB);
	memcpy(e->out, d_in, OUTB);	/* get reads the scale flag word of the block */
	memcpy(t32_data_dd2d4, d_fps, FPSB);
	memset(&cr, 0, sizeof(cr));
	sleep_st = e->st[0];
	sleep_calls = 0;
	return 0;
}

static void take(struct snap *s, const struct env *e)
{
	u32 k;

	for (k = 0; k < 2; k++) {
		memcpy(s->tune[k], e->tune[k], TUNEB);
		memcpy(s->buf[k], e->buf[k], BUFB);
		memcpy(s->st[k], e->st[k], STB);
	}
	memcpy(s->in, e->in, INB);
	memcpy(s->out, e->out, OUTB);
	memcpy(s->fps, t32_data_dd2d4, FPSB);
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->evn = t32o_ev_n;
	s->logn = t32o_log.n;
	s->printfs = t32o_log.printfs;
	s->sleeps = sleep_calls;
	s->cr = cr;
}

static void fill(u8 *p, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		p[i] = (u8)(rnd() >> 11);
}

static int run_case(unsigned int which, u32 ch)
{
	struct env e;
	struct t32_aea_ctx c;
	const char *what = NULL;
	u32 k;

	t32o_reset();
	if (setup(&e, ch))
		return 2;
	switch (which) {
	case C_GET: sr.ret = (u32)t32o_call((t32o_fn)(void (*)(void))api_ae_get_exprinfo_attr, ch, T32O_PTR(e.out), 0, 0, 0); break;
	case C_SET: sr.ret = (u32)t32o_call((t32o_fn)(void (*)(void))api_ae_set_exprinfo_attr, ch, T32O_PTR(e.in), 0, 0, 0); break;
	}
	take(&sr, &e);

	t32o_reset();
	if (setup(&e, ch))
		return 2;
	memset(&c, 0, sizeof(c));
	c.tune = e.tune[0];
	c.buf = e.buf[0];
	c.st = e.st[0];
	c.fps = t32_data_dd2d4 + ch * 232;
	so.ret = 0;
	switch (which) {
	case C_GET: so.ret = t32_aea_get_exprinfo_attr(&c, e.out, sleep_hook, log2_hook, NULL); break;
	case C_SET: t32_aea_set_exprinfo_attr(&c, e.in, exp2_hook, NULL); break;
	}
	take(&so, &e);
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	if (!what && which == C_GET && so.ret != sr.ret)
		what = "return value";
	if (!what && (so.sleeps != sr.sleeps || memcmp(&so.cr, &sr.cr, sizeof(sr.cr))))
		what = "stub calls";
	for (k = 0; !what && k < 2; k++) {
		if (memcmp(so.tune[k], sr.tune[k], TUNEB))
			what = k ? "decoy tune block" : "tune block";
		else if (memcmp(so.buf[k], sr.buf[k], BUFB))
			what = k ? "decoy buf block" : "buf block";
		else if (memcmp(so.st[k], sr.st[k], STB))
			what = k ? "decoy st block" : "st block";
	}
	if (!what && memcmp(so.fps, sr.fps, FPSB))
		what = "dd2d4 table";
	if (!what && memcmp(so.in, sr.in, INB))
		what = "input block";
	if (!what && memcmp(so.out, sr.out, OUTB))
		what = "output block";
	if (what) {
		printf("MISMATCH %s ch %u: %s\n", names[which], ch, what);
		return 1;
	}
	return 0;
}

static u32 flagword(void)
{
	u32 r = rnd() % 6;

	return r < 3 ? 1 : r == 3 ? 0 : r == 4 ? 2 : rnd();
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % C_N;
		u32 ch = rnd() & 3, r, k;
		int rc;

		fill(d_tune[0], TUNEB); fill(d_tune[1], TUNEB);
		fill(d_buf[0], BUFB); fill(d_buf[1], BUFB);
		fill(d_st[0], STB); fill(d_st[1], STB);
		fill(d_in, INB);
		fill(d_fps, FPSB);
		/* the scale flag, the enables and flags of the block */
		*(u32 *)(d_in + 0) = flagword();
		for (k = 1; k < 6; k++)
			*(u32 *)(d_in + 4 * k) = flagword();
		for (k = 10; k < 18; k++)
			*(u32 *)(d_in + 4 * k) = flagword();
		for (k = 27; k < 31; k++)
			*(u32 *)(d_in + 4 * k) = flagword();
		for (k = 35; k < 43; k++)
			*(u32 *)(d_in + 4 * k) = flagword();
		/* q: sometimes a zero divisor (zero fps high half or zero frame size) */
		r = rnd() % 8;
		if (r == 0)
			*(uint16_t *)(d_fps + ch * 232 + 58) = 0;
		else if (r == 1)
			*(uint16_t *)(d_fps + ch * 232 + 2) = 0;
		else if (r < 5)
			*(u32 *)(d_fps + ch * 232 + 56) = (rnd() % 60 + 1) << 16 | (rnd() % 60 + 1);
		/* the exact pattern behind the "enabled" test of word 104; get copies st + 1276 (9 bytes) to buf + 12833 first */
		r = rnd() % 3;
		if (r == 0) {
			d_st[0][1279] = (u8)rnd();
			d_st[0][1280] = 1; d_st[0][1281] = 1; d_st[0][1282] = 1;
			d_st[0][1283] = (rnd() & 3) ? 1 : (u8)rnd();
		}
		d_st[0][1284] = (rnd() & 1) ? (u8)(rnd() & 3) : (u8)rnd();	/* copied to buf + 12841 */
		/* wait state: clear now, or after a chosen number of sleeps */
		r = rnd() % 4;
		d_st[0][1285] = r == 0 ? 0 : (u8)(1 + rnd() % 255);
		r = rnd() % 8;
		clear_after = r == 0 ? 1 : r == 1 ? 2 : r == 2 ? 49 : r == 3 ? 50 : r == 4 ? 51 : 1 + rnd() % 60;
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_aeai_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aeai_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
