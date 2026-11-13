// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE scene parameters (tisp_ae_api_set_scene_par /
 * get_scene_par / get_scene_luma).  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc; get_scene_par calls the vendor
 * tisp_ae_api_get_luma and get_scene_luma, fix_point_div_32 / mult2_32 run as
 * the vendor refs on both sides) and our t32_aea_* from
 * driver/t32/tx_isp_t32_stats.h from the same random state and compares the
 * tune / buf / st blocks of the used and of a decoy channel, the input and
 * output buffers, the register and event log.  Vendor reads of st at
 * 8 * (tune[1656] + 1) are kept inside the block (larger values read outside
 * and are not covered).
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
#define OUTB	128u
#define INB	64u

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
u8 t32_data_d0950[4 * 1052] __asm__("t32_data_d0950");
u8 t32_data_e1570[8] __asm__("t32_data_e1570");
u8 t32_data_d2a30[4 * 1028] __asm__("t32_data_d2a30");

extern u64 tisp_ae_api_set_scene_par(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_scene_par(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_scene_luma(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_div_32(u32, u32, u32, u32);
u64 fix_point_mult2_32(u32, u32, u32, u32);

static u32 h_div(u32 s, u32 a, u32 b)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))fix_point_div_32, s, a, b, 0, 0);
}
static u32 h_mult2(u32 s, u32 a, u32 b)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))fix_point_mult2_32, s, a, b, 0, 0);
}
static const struct t32_ae_ops ops = { .div32 = h_div, .mult2_32 = h_mult2 };

static u32 rs = 0x1d872b41u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_SET, C_GET, C_LUMA, C_N };
static const char *const names[C_N] = { "set_scene_par", "get_scene_par", "get_scene_luma" };

struct env {
	u8 *tune[2], *buf[2], *st[2], *in, *out;
};
static u8 d_tune[2][TUNEB], d_buf[2][BUFB], d_st[2][STB], d_in[INB];
struct snap {
	u8 tune[2][TUNEB], buf[2][BUFB], st[2][STB], in[INB], out[OUTB];
	u32 evlog[T32O_EVLOG][9], evn, logn, printfs;
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
	case C_SET: VC(tisp_ae_api_set_scene_par, ch, T32O_PTR(e.in), 0); break;
	case C_GET: VC(tisp_ae_api_get_scene_par, ch, T32O_PTR(e.out), 0); break;
	case C_LUMA: VC(tisp_ae_api_get_scene_luma, ch, T32O_PTR(e.out), 0); break;
	}
	take(&sr, &e);

	t32o_reset();
	if (setup(&e, ch))
		return 2;
	c.tune = e.tune[0];
	c.buf = e.buf[0];
	c.st = e.st[0];
	switch (which) {
	case C_SET: t32_aea_set_scene_par(&c, e.in); break;
	case C_GET: t32_aea_get_scene_par(&c, e.out, &ops); break;
	case C_LUMA: *(u32 *)e.out = t32_aea_get_scene_luma(&c, &ops); break;
	}
	take(&so, &e);
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
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
		/* modes 0..4 and a flag word, mostly the interesting ones */
		r = rnd() & 7;
		*(u32 *)d_in = r < 5 ? r : rnd();
		r = rnd() & 7;
		*(u32 *)(d_in + 8) = r < 5 ? r : rnd();
		if (rnd() & 1)
			*(u32 *)(d_in + 16) = 0;
		if (rnd() & 1)
			*(u32 *)(d_in + 24) = 0;
		/* tune: scene mode word 0..3, comp 128 sometimes, sane shift, st index kept inside the block */
		*(u32 *)(d_tune[0] + 1684) = rnd() & 3;
		if (!(rnd() & 3))
			*(u16 *)(d_tune[0] + 2960) = 128;
		*(u16 *)(d_tune[0] + 1736) = (u16)(rnd() % 11);
		*(u32 *)(d_tune[0] + 1656) = rnd() % 150;
		if (!(rnd() & 3))
			*(u16 *)(d_buf[0] + 8566) = (u16)(rnd() & 1);
		if (!(rnd() & 7))
			*(u16 *)(d_tune[0] + 2770) = 0;
		if (!(rnd() & 7))
			*(u32 *)d_buf[0] = rnd() & 0x7fffffff, *(u32 *)(d_buf[0] + 1020) = rnd() & 0x7fffffff;
		if (!(rnd() & 3))
			*(u32 *)(d_st[0] + 8 * (*(u32 *)(d_tune[0] + 1656) + 1)) = 0;
		/* histogram sum exactly equal to the size product (and one off) */
		r = rnd() & 7;
		if (r < 3) {
			*(u16 *)(d_tune[0] + 2770) = 2 + (*(u16 *)(d_tune[0] + 2770) & 1); *(u16 *)(d_tune[0] + 2774) = 2 + (*(u16 *)(d_tune[0] + 2774) & 1);
			*(u16 *)(d_buf[0] + 11796) = 2 + (*(u16 *)(d_buf[0] + 11796) & 1); *(u16 *)(d_buf[0] + 11826) = 2 + (*(u16 *)(d_buf[0] + 11826) & 1);
			*(u32 *)d_buf[0] &= 7u;
			{
			u32 p = (u32)*(u16 *)(d_tune[0] + 2770) * *(u16 *)(d_tune[0] + 2774) * *(u16 *)(d_buf[0] + 11796) *
				*(u16 *)(d_buf[0] + 11826), b0 = *(u32 *)d_buf[0] >> 1 & 0x3fffffffu;

			if (p >= b0 + 2 && p - b0 <= 0x3fffffffu)
				*(u32 *)(d_buf[0] + 1020) = p - b0 + (r == 1) - 2 * (r == 2);
			}
		}
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_aeac_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aeac_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
