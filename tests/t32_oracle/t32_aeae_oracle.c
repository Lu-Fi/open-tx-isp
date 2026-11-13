// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE statistics block and special area
 * (tisp_ae_get_ae_statis, tisp_ae_sepc_area).  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our t32_aea_*
 * from driver/t32/tx_isp_t32_stats.h from the same random state and compares
 * the tune / buf / st blocks of the used and of a decoy channel, the hist
 * table, the four ROI byte tables, the input and output buffers, the
 * tisp_ae_roi_point calls (recording stub on both sides), the register and
 * event log.  The spin lock is a no-op stub (left to the caller in the port).
 * Exit 0 = all equal, 1 = first mismatch, 2 = setup failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define BUFB	0x3300u
#define STB	0x1000u
#define OUTB	2048u
#define INB	64u

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
u8 t32_data_d0950[4 * 1052] __asm__("t32_data_d0950");
u8 t32_data_e151c[4] __asm__("t32_data_e151c");
u8 t32_data_e1518[4] __asm__("t32_data_e1518");
u8 t32_data_e1524[4] __asm__("t32_data_e1524");
u8 t32_data_e1520[4] __asm__("t32_data_e1520");

extern u64 tisp_ae_get_ae_statis(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_sepc_area(u32 a0, u32 a1, u32 a2, u32 a3);

struct roirec {
	u32 n, a[5];
};
static struct roirec rr;

u64 tisp_ae_roi_point(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	rr.n++;
	rr.a[0] = a0; rr.a[1] = a1; rr.a[2] = a2; rr.a[3] = a3; rr.a[4] = a4;
	return 0;
}

static void roi_hook(void *priv, u32 ch, u32 x0, u32 y0, u32 x1, u32 y1)
{
	(void)priv;
	tisp_ae_roi_point(ch, x0, y0, x1, y1);
}

static u32 rs = 0x5bd1e995u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_STAT, C_SEPC, C_N };
static const char *const names[C_N] = { "get_ae_statis", "sepc_area" };

struct env {
	u8 *buf[2], *st[2], *in, *out;
};
static u8 d_buf[2][BUFB], d_st[2][STB], d_in[INB], d_hist[sizeof(t32_data_d0950)], d_roi[4][4];
struct snap {
	u8 buf[2][BUFB], st[2][STB], in[INB], out[OUTB], hist[sizeof(t32_data_d0950)], roi[4][4];
	u32 evlog[T32O_EVLOG][9], evn, logn, printfs;
	struct roirec rr;
};
static struct snap sr, so;

static int setup(struct env *e, u32 ch)
{
	u32 k, slot[2] = { ch, (ch + 1) & 3 };

	for (k = 0; k < 2; k++) {
		u8 *ctx = t32o_alloc(16);

		e->buf[k] = t32o_alloc(BUFB);
		e->st[k] = t32o_alloc(STB);
		if (!ctx || !e->buf[k] || !e->st[k])
			return 2;
		memcpy(e->buf[k], d_buf[k], BUFB);
		memcpy(e->st[k], d_st[k], STB);
		*(u32 *)(ctx + 0) = 0;
		*(u32 *)(ctx + 4) = T32O_PTR(e->buf[k]);
		t32_data_e15a8[slot[k]] = T32O_PTR(ctx);
		t32_data_e1588[slot[k]] = T32O_PTR(e->st[k]);
	}
	e->in = t32o_alloc(INB);
	e->out = t32o_alloc(OUTB);
	if (!e->in || !e->out)
		return 2;
	memcpy(e->in, d_in, INB);
	memset(e->out, 0xa5, OUTB);
	memcpy(t32_data_d0950, d_hist, sizeof(d_hist));
	memcpy(t32_data_e151c, d_roi[0], 4);
	memcpy(t32_data_e1518, d_roi[1], 4);
	memcpy(t32_data_e1524, d_roi[2], 4);
	memcpy(t32_data_e1520, d_roi[3], 4);
	memset(&rr, 0, sizeof(rr));
	return 0;
}

static void take(struct snap *s, const struct env *e)
{
	u32 k;

	for (k = 0; k < 2; k++) {
		memcpy(s->buf[k], e->buf[k], BUFB);
		memcpy(s->st[k], e->st[k], STB);
	}
	memcpy(s->in, e->in, INB);
	memcpy(s->out, e->out, OUTB);
	memcpy(s->hist, t32_data_d0950, sizeof(s->hist));
	memcpy(s->roi[0], t32_data_e151c, 4);
	memcpy(s->roi[1], t32_data_e1518, 4);
	memcpy(s->roi[2], t32_data_e1524, 4);
	memcpy(s->roi[3], t32_data_e1520, 4);
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->evn = t32o_ev_n;
	s->logn = t32o_log.n;
	s->printfs = t32o_log.printfs;
	s->rr = rr;
}

#define VC(fn, a0, a1) ((void)t32o_call((t32o_fn)(void (*)(void))fn, a0, a1, 0, 0, 0))

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
	case C_STAT: VC(tisp_ae_get_ae_statis, ch, T32O_PTR(e.out)); break;
	case C_SEPC: VC(tisp_ae_sepc_area, ch, T32O_PTR(e.in)); break;
	}
	take(&sr, &e);

	t32o_reset();
	if (setup(&e, ch))
		return 2;
	memset(&c, 0, sizeof(c));
	c.buf = e.buf[0];
	c.st = e.st[0];
	c.hist = t32_data_d0950 + ch * 1052;
	switch (which) {
	case C_STAT: t32_aea_get_ae_statis(&c, e.out); break;
	case C_SEPC:
		t32_aea_sepc_area(&c, ch, e.in, t32_data_e151c + ch, t32_data_e1524 + ch,
				  t32_data_e1518 + ch, t32_data_e1520 + ch, roi_hook, NULL);
		break;
	}
	take(&so, &e);
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	if (!what && so.printfs != sr.printfs)
		what = "printk count";
	if (!what && memcmp(&so.rr, &sr.rr, sizeof(sr.rr)))
		what = "roi_point call";
	for (k = 0; !what && k < 2; k++) {
		if (memcmp(so.buf[k], sr.buf[k], BUFB))
			what = k ? "decoy buf block" : "buf block";
		else if (memcmp(so.st[k], sr.st[k], STB))
			what = k ? "decoy st block" : "st block";
	}
	if (!what && memcmp(so.hist, sr.hist, sizeof(sr.hist)))
		what = "hist table";
	if (!what && memcmp(so.roi, sr.roi, sizeof(sr.roi)))
		what = "roi byte tables";
	if (!what && memcmp(so.in, sr.in, INB))
		what = "input buffer";
	if (!what && memcmp(so.out, sr.out, OUTB))
		what = "output buffer";
	if (what && getenv("T32O_DIFF")) {
		u32 o;

		for (o = 0; o < OUTB; o++)
			if (so.out[o] != sr.out[o])
				printf("  out+%u vendor %02x port %02x\n", o, sr.out[o], so.out[o]);
	}
	if (what) {
		printf("MISMATCH %s ch %u: %s\n", names[which], ch, what);
		return 1;
	}
	return 0;
}

static u32 pick(u32 big)
{
	u32 r = rnd() % 6;

	return r == 0 ? 0 : r == 1 ? rnd() % 20 : r == 2 ? rnd() % 300 : rnd() % big;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % C_N;
		u32 ch = rnd() & 3, r, k;
		int rc;

		fill(d_buf[0], BUFB); fill(d_buf[1], BUFB);
		fill(d_st[0], STB); fill(d_st[1], STB);
		fill(d_in, INB);
		fill(d_hist, sizeof(d_hist));
		fill(&d_roi[0][0], 16);
		if (which == C_SEPC) {
			/* grid size w = u16 / 15, h = u16 / 15: small (0 .. 29) or full range */
			for (k = 0; k < 2; k++) {
				r = rnd() % 4;
				*(uint16_t *)(d_buf[0] + 8548 + 2 * k) = r == 0 ? rnd() % 30 : r == 1 ? 1920 : r == 2 ? 1080 : rnd();
			}
			for (k = 0; k < 6; k++)
				*(u32 *)(d_in + 4 * k) = k ? pick(k == 5 ? 400 : 5000) : (rnd() & 1 ? rnd() : 1);
			r = rnd() % 5;
			if (r == 0)
				for (k = 1; k < 5; k++)
					*(u32 *)(d_in + 4 * k) = 0;
			else if (r == 1)
				*(u32 *)(d_in + 4 * (1 + rnd() % 4)) = 0;
			/* st[1520] already equal to the flag word half of the time */
			if (rnd() & 1)
				*(u32 *)(d_st[0] + 1520) = *(u32 *)d_in;
		}
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_aeae_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aeae_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
