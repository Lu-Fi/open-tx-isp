// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE day/night parameter refresh and long luma-histogram
 * process (api_ae_dn_par_refresh, api_ae_long_process).  Runs the unmodified
 * vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc, with the real
 * fix_point_mult2 / fix_point_div) and our t32_aea_* from
 * driver/t32/tx_isp_t32_stats.h from the same random state and compares the
 * tune / buf / st blocks of the used and of a decoy channel, the hist, lum and
 * comp tables, the f0900 flag bytes, the call sequence of the three hooks
 * (recording stubs on both sides), the register and event log.  The spin lock
 * is a no-op stub (left to the caller in the port).
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

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
u8 t32_data_d0950[4 * 1052] __asm__("t32_data_d0950");
u8 t32_data_d19c0[4 * 1052] __asm__("t32_data_d19c0");
u8 t32_data_d2a30[4 * 1028] __asm__("t32_data_d2a30");
extern u8 t32_data_f0900[0x10000];

extern u64 api_ae_dn_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 api_ae_long_process(u32 a0, u32 a1, u32 a2, u32 a3);

struct callrec {
	u32 n, id[8], a0[8];
};
static struct callrec cr;

static void rec(u32 id, u32 a0)
{
	if (cr.n < 8) {
		cr.id[cr.n] = id;
		cr.a0[cr.n] = a0;
	}
	cr.n++;
}

u64 tisp_ae_set_hardware_param(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rec(1, a0);
	return 0;
}

u64 tisp_ae_clac_deflicker_cfg(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rec(2, a0);
	return 0;
}

u64 tisp_ae_set_riscv_param(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rec(3, a0);
	return 0;
}

static void hw_hook(void *priv, u32 ch) { (void)priv; rec(1, ch); }
static void defl_hook(void *priv, u32 ch) { (void)priv; rec(2, ch); }
static void risc_hook(void *priv, u32 ch) { (void)priv; rec(3, ch); }

static u32 rs = 0x2c1b3c6du;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_DN, C_LP, C_N };
static const char *const names[C_N] = { "dn_par_refresh", "long_process" };

struct env {
	u8 *buf[2], *st[2];
};
static u8 d_buf[2][BUFB], d_st[2][STB], d_hist[sizeof(t32_data_d0950)], d_lum[sizeof(t32_data_d19c0)],
	d_comp[sizeof(t32_data_d2a30)], d_f[8];
struct snap {
	u8 buf[2][BUFB], st[2][STB], hist[sizeof(t32_data_d0950)], lum[sizeof(t32_data_d19c0)],
		comp[sizeof(t32_data_d2a30)], f[8];
	u32 evlog[T32O_EVLOG][9], evn, logn, printfs;
	struct callrec cr;
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
	memcpy(t32_data_d0950, d_hist, sizeof(d_hist));
	memcpy(t32_data_d19c0, d_lum, sizeof(d_lum));
	memcpy(t32_data_d2a30, d_comp, sizeof(d_comp));
	memcpy(t32_data_f0900, d_f, 8);
	memset(&cr, 0, sizeof(cr));
	return 0;
}

static void take(struct snap *s, const struct env *e)
{
	u32 k;

	for (k = 0; k < 2; k++) {
		memcpy(s->buf[k], e->buf[k], BUFB);
		memcpy(s->st[k], e->st[k], STB);
	}
	memcpy(s->hist, t32_data_d0950, sizeof(s->hist));
	memcpy(s->lum, t32_data_d19c0, sizeof(s->lum));
	memcpy(s->comp, t32_data_d2a30, sizeof(s->comp));
	memcpy(s->f, t32_data_f0900, 8);
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->evn = t32o_ev_n;
	s->logn = t32o_log.n;
	s->printfs = t32o_log.printfs;
	s->cr = cr;
}

#define VC(fn, a0) ((void)t32o_call((t32o_fn)(void (*)(void))fn, a0, 0, 0, 0, 0))

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
	case C_DN: VC(api_ae_dn_par_refresh, ch); break;
	case C_LP: VC(api_ae_long_process, ch); break;
	}
	take(&sr, &e);

	t32o_reset();
	if (setup(&e, ch))
		return 2;
	memset(&c, 0, sizeof(c));
	c.buf = e.buf[0];
	c.st = e.st[0];
	c.hist = t32_data_d0950 + ch * 1052;
	c.lum = t32_data_d19c0 + ch * 1052;
	c.comp = t32_data_d2a30 + ch * 1028;
	switch (which) {
	case C_DN: t32_aea_dn_par_refresh(&c, ch, t32_data_f0900[ch], hw_hook, defl_hook, risc_hook, NULL); break;
	case C_LP: t32_aea_long_process(&c); break;
	}
	take(&so, &e);
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	if (!what && memcmp(&so.cr, &sr.cr, sizeof(sr.cr)))
		what = "hook call sequence";
	for (k = 0; !what && k < 2; k++) {
		if (memcmp(so.buf[k], sr.buf[k], BUFB))
			what = k ? "decoy buf block" : "buf block";
		else if (memcmp(so.st[k], sr.st[k], STB))
			what = k ? "decoy st block" : "st block";
	}
	if (!what && memcmp(so.hist, sr.hist, sizeof(sr.hist)))
		what = "hist table";
	if (!what && memcmp(so.lum, sr.lum, sizeof(sr.lum)))
		what = "lum table";
	if (!what && memcmp(so.comp, sr.comp, sizeof(sr.comp)))
		what = "comp table";
	if (!what && memcmp(so.f, sr.f, 8))
		what = "f0900 flags";
	if (what) {
		printf("MISMATCH %s ch %u: %s\n", names[which], ch, what);
		return 1;
	}
	return 0;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % C_N;
		u32 ch = rnd() & 3, k, r;
		int rc;

		fill(d_buf[0], BUFB); fill(d_buf[1], BUFB);
		fill(d_st[0], STB); fill(d_st[1], STB);
		fill(d_hist, sizeof(d_hist));
		fill(d_lum, sizeof(d_lum));
		fill(d_comp, sizeof(d_comp));
		fill(d_f, 8);
		for (k = 0; k < 8; k++) {
			r = rnd() % 3;
			d_f[k] = r == 0 ? 1 : r == 1 ? 0 : d_f[k];
		}
		if (which == C_LP) {
			u8 *hb = d_hist + ch * 1052 + 1044;

			/* small words so that the bin sums vary in a controlled range, sometimes all zero (total 0) */
			r = rnd() % 8;
			for (k = 0; k < 256; k++)
				((u32 *)d_buf[0])[k] = r == 0 ? 0 : r == 1 ? rnd() : rnd() % 500;
			r = rnd() % 4;
			if (r == 0) {	/* sorted bin bounds */
				hb[0] = rnd() % 64; hb[1] = hb[0] + rnd() % 64; hb[2] = hb[1] + rnd() % 64;
				hb[3] = hb[2] + rnd() % 64;
			} else if (r == 1) {	/* any order, full range */
				for (k = 0; k < 4; k++)
					hb[k] = (u8)rnd();
			} else if (r == 2) {	/* extremes */
				for (k = 0; k < 4; k++)
					hb[k] = (rnd() & 1) ? 0 : 255;
			}
			if (!(rnd() & 7)) {	/* bins that are not zero but wrap to a total of 0 */
				memset(d_buf[0], 0, 1024);
				hb[0] = 10; hb[1] = 20; hb[2] = 30; hb[3] = 40;
				((u32 *)d_buf[0])[5 + 10 * (rnd() % 5)] = 0x80000000u;
				((u32 *)d_buf[0])[6 + 10 * (rnd() % 5)] += 0x80000000u;
				if (rnd() & 1)
					((u32 *)d_buf[0])[240] = 0;
			}
			*(uint16_t *)(d_buf[0] + 8548) = (uint16_t)rnd();
			*(uint16_t *)(d_buf[0] + 8550) = (uint16_t)rnd();
		}
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_aeah_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aeah_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
