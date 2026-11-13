// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE exposure / ev / at lists (tisp_ae_api_set_exp_list /
 * get_exp_list, tisp_ae_set_ev_list / get_ev_list / set_at_list /
 * get_at_list).  Runs the unmodified vendor functions (Matt Davis' recovered
 * C, PR #15 @ 2e2e06bc) and our t32_aea_* from driver/t32/tx_isp_t32_stats.h
 * from the same random state and compares the tune / buf / st blocks of the
 * used and of a decoy channel, the input and output buffers, the printk
 * count, the register and event log.  set_exp_list reads
 * dc284 + ch * 904 + 900 (two channels only: ch >= 2 is not covered there).
 * Exit 0 = all equal, 1 = first mismatch, 2 = setup failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define TUNEB	0x1000u
#define BUFB	0x3300u
#define STB	0x800u
#define OUTB	400u
#define INB	400u

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
u8 t32_data_d0950[4 * 1052] __asm__("t32_data_d0950");
u8 t32_data_e1570[8] __asm__("t32_data_e1570");
u8 t32_data_d2a30[4 * 1028] __asm__("t32_data_d2a30");

extern u64 tisp_ae_api_set_exp_list(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_exp_list(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_set_ev_list(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_get_ev_list(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_set_at_list(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_get_at_list(u32 a0, u32 a1, u32 a2, u32 a3);
char t32_data_bf3b8[4] = "f", t32_data_c8690[4] = "f";

static unsigned int our_warn;
static void warn_hook(void *priv)
{
	(void)priv;
	our_warn++;
}

static u32 rs = 0x3c6ef372u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_SEXP, C_GEXP, C_SEV, C_GEV, C_SAT, C_GAT, C_N };
static const char *const names[C_N] = {
	"set_exp_list", "get_exp_list", "set_ev_list", "get_ev_list", "set_at_list", "get_at_list"
};

struct env {
	u8 *tune[2], *buf[2], *st[2], *in, *out;
};
static u8 d_tune[2][TUNEB], d_buf[2][BUFB], d_st[2][STB], d_in[INB];
struct snap {
	u8 tune[2][TUNEB], buf[2][BUFB], st[2][STB], in[INB], out[OUTB];
	u32 evlog[T32O_EVLOG][9], evn, logn, printfs;
};
static struct snap sr, so;

static u32 d_dcw;

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
	*(u32 *)(t32_data_dc284 + (ch & 1) * 904 + 900) = d_dcw;
	memset(e->out, 0xa5, OUTB);
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
	case C_SEXP: VC(tisp_ae_api_set_exp_list, ch, ((u32 *)e.in)[0], T32O_PTR(e.in + 4)); break;
	case C_GEXP: VC(tisp_ae_api_get_exp_list, ch, T32O_PTR(e.out), T32O_PTR(e.out + 4)); break;
	case C_SEV: VC(tisp_ae_set_ev_list, ch, ((u32 *)e.in)[0], T32O_PTR(e.in + 4)); break;
	case C_GEV: VC(tisp_ae_get_ev_list, ch, T32O_PTR(e.out), 0); break;
	case C_SAT: VC(tisp_ae_set_at_list, ch, ((u32 *)e.in)[0], T32O_PTR(e.in + 4)); break;
	case C_GAT: VC(tisp_ae_get_at_list, ch, T32O_PTR(e.out), 0); break;
	}
	take(&sr, &e);

	t32o_reset();
	if (setup(&e, ch))
		return 2;
	c.tune = e.tune[0];
	c.buf = e.buf[0];
	c.st = e.st[0];
	our_warn = 0;
	switch (which) {
	case C_SEXP: t32_aea_set_exp_list(&c, *(u32 *)(t32_data_dc284 + ch * 904 + 900), ((u32 *)e.in)[0], e.in + 4, warn_hook, NULL); break;
	case C_GEXP: t32_aea_get_exp_list(&c, e.out, e.out + 4); break;
	case C_SEV: t32_aea_set_ev_list(&c, ((u32 *)e.in)[0], e.in + 4); break;
	case C_GEV: t32_aea_get_ev_list(&c, e.out); break;
	case C_SAT: t32_aea_set_at_list(&c, ((u32 *)e.in)[0], e.in + 4); break;
	case C_GAT: t32_aea_get_at_list(&c, e.out); break;
	}
	take(&so, &e);
	so.printfs = our_warn;
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	if (!what && so.printfs != sr.printfs)
		what = "printk count";
	for (k = 0; !what && k < 2; k++) {
		if (memcmp(so.tune[k], sr.tune[k], TUNEB))
			what = k ? "decoy tune block" : "tune block";
		else if (memcmp(so.buf[k], sr.buf[k], BUFB))
			what = k ? "decoy buf block" : "buf block";
		else if (memcmp(so.st[k], sr.st[k], STB))
			what = k ? "decoy st block" : "st block";
	}
	if (!what && memcmp(so.in, sr.in, INB))
		what = "input buffer";
	if (!what && memcmp(so.out, sr.out, OUTB))
		what = "output buffer";
	if (what && getenv("T32O_DIFF")) {
		u32 o;

		for (o = 0; o < TUNEB; o++)
			if (d_tune[0][o] != sr.tune[0][o] || d_tune[0][o] != so.tune[0][o])
				printf("  +%u init %02x vendor %02x port %02x\n", o, d_tune[0][o], sr.tune[0][o], so.tune[0][o]);
	}
	if (what) {
		printf("MISMATCH %s ch %u: %s\n", names[which], ch, what);
		return 1;
	}
	return 0;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4500;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % C_N;
		u32 ch = rnd() & 3, r;
		int rc;

		fill(d_tune[0], TUNEB); fill(d_tune[1], TUNEB);
		fill(d_buf[0], BUFB); fill(d_buf[1], BUFB);
		fill(d_st[0], STB); fill(d_st[1], STB);
		fill(d_in, INB);
		/* flag word: 1, 0, 2, 0x101 (low byte 1), random */
		r = rnd() % 6;
		*(u32 *)d_in = r == 0 ? 1 : r == 1 ? 0 : r == 2 ? 2 : r == 3 ? 0x101 : r == 4 ? 1 : rnd();
		if (which == C_SEXP) {
			ch &= 1;
			r = rnd() % 4;
			d_dcw = r == 3 ? rnd() : r;
		}
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_aead_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aead_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
