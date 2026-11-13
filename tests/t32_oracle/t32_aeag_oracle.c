// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE parameter array (tisp_ae_param_array_get /
 * tisp_ae_param_array_set).  Runs the unmodified vendor functions (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc) and our t32_aea_param_array_* from
 * driver/t32/tx_isp_t32_stats.h from the same random state and compares the
 * tune / buf / st blocks of the used and of a decoy channel, the shared dd2d4
 * table, the input and output buffers, the length word, the register and
 * event log.  set reads dc284 + ch * 904 + 900 (two channels only).
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
#define OUTB	0x4100u
#define INB	0x4100u
#define FPSB	0x400u

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
extern u8 t32_data_dd2d4[0x400];

extern u64 tisp_ae_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x1b873593u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_GET, C_SET, C_N };
static const char *const names[C_N] = { "param_array_get", "param_array_set" };

struct env {
	u8 *tune[2], *buf[2], *st[2], *in, *out;
	u32 *len;
};
static u8 d_tune[2][TUNEB], d_buf[2][BUFB], d_st[2][STB], d_in[INB], d_fps[FPSB];
static u32 d_dcw;
struct snap {
	u8 tune[2][TUNEB], buf[2][BUFB], st[2][STB], in[INB], out[OUTB], fps[FPSB];
	u32 len, evlog[T32O_EVLOG][9], evn, logn, printfs;
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
	e->len = (u32 *)t32o_alloc(16);
	if (!e->in || !e->out || !e->len)
		return 2;
	memcpy(e->in, d_in, INB);
	memcpy(t32_data_dd2d4, d_fps, FPSB);
	*(u32 *)(t32_data_dc284 + (ch & 1) * 904 + 900) = d_dcw;
	memset(e->out, 0xa5, OUTB);
	*e->len = 0x5a5a5a5au;
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
	s->len = *e->len;
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->evn = t32o_ev_n;
	s->logn = t32o_log.n;
	s->printfs = t32o_log.printfs;
}

#define VC(fn, a0, a1, a2) ((void)t32o_call((t32o_fn)(void (*)(void))fn, a0, a1, a2, 0, 0))

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
	case C_GET: VC(tisp_ae_param_array_get, ch, T32O_PTR(e.out), T32O_PTR(e.len)); break;
	case C_SET: VC(tisp_ae_param_array_set, ch, T32O_PTR(e.in), 0); break;
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
	switch (which) {
	case C_GET: t32_aea_param_array_get(&c, e.out, e.len); break;
	case C_SET: t32_aea_param_array_set(&c, *(u32 *)(t32_data_dc284 + ch * 904 + 900), e.in); break;
	}
	take(&so, &e);
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	if (!what && so.len != sr.len)
		what = "length word";
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
		what = "input buffer";
	if (!what && memcmp(so.out, sr.out, OUTB))
		what = "output buffer";
	if (what) {
		printf("MISMATCH %s ch %u: %s\n", names[which], ch, what);
		return 1;
	}
	return 0;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % C_N;
		u32 ch = rnd() & 1, r;
		int rc;

		fill(d_tune[0], TUNEB); fill(d_tune[1], TUNEB);
		fill(d_buf[0], BUFB); fill(d_buf[1], BUFB);
		fill(d_st[0], STB); fill(d_st[1], STB);
		fill(d_in, INB);
		fill(d_fps, FPSB);
		r = rnd() % 4;
		d_dcw = r == 3 ? rnd() : r;
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_aeag_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aeag_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
