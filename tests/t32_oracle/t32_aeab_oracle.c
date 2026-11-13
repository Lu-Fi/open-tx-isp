// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE API data blocks (api_ae_get_hist_bin / set_hist_bin /
 * get_y_zone / get_weight / set_weight / get_antiflicker_step, tisp_ae_api_
 * get_antiflick / set_antiflick / set_comp).  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our t32_aea_*
 * from driver/t32/tx_isp_t32_stats.h from the same random state and compares
 * the tune / buf / st / base blocks of the used and of a decoy channel, the
 * per-channel global tables (d0950, e1570, d2a30), the input and output
 * buffers, the return value (where the vendor defines one), the register and
 * event log and the tisp_ae_clac_deflicker_cfg calls (recording stub on both
 * sides).  The spin lock of the vendor is left to the caller (port) and is a
 * no-op stub in env.c.
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
#define BASEB	0x800u
#define OUTB	1300u
#define INB	1024u

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
u8 t32_data_d0950[4 * 1052] __asm__("t32_data_d0950");
u8 t32_data_e1570[8] __asm__("t32_data_e1570");
u8 t32_data_d2a30[4 * 1028] __asm__("t32_data_d2a30");

extern u64 api_ae_get_hist_bin(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 api_ae_set_hist_bin(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 api_ae_get_y_zone(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 api_ae_get_weight(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 api_ae_set_weight(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 api_ae_get_antiflicker_step(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_antiflick(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_set_antiflick(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_set_comp(u32 a0, u32 a1, u32 a2, u32 a3);

struct defrec {
	u32 n, a0;
};
static struct defrec dr;

u64 tisp_ae_clac_deflicker_cfg(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	dr.n++;
	dr.a0 = a0;
	return 0;
}

static void def_hook(void *priv, u32 ch)
{
	(void)priv;
	tisp_ae_clac_deflicker_cfg(ch, 0, 0, 0);
}

static u32 rs = 0x2545f491u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_GHIST, C_SHIST, C_YZONE, C_GW, C_SW, C_AFSTEP, C_GAF, C_SAF, C_COMP, C_N };
static const char *const names[C_N] = {
	"get_hist_bin", "set_hist_bin", "get_y_zone", "get_weight", "set_weight",
	"get_antiflicker_step", "get_antiflick", "set_antiflick", "set_comp"
};
static const int has_ret[C_N] = { 1, 1, 1, 0, 0, 1, 0, 0, 0 };

struct env {
	u8 *tune[2], *buf[2], *st[2], *base[2], *in, *out;
};
static u8 d_tune[2][TUNEB], d_buf[2][BUFB], d_st[2][STB], d_base[2][BASEB], d_in[INB];
static u8 d_hist[sizeof(t32_data_d0950)], d_w[8], d_comp[sizeof(t32_data_d2a30)];
struct snap {
	u8 tune[2][TUNEB], buf[2][BUFB], st[2][STB], base[2][BASEB], in[INB], out[OUTB];
	u8 hist[sizeof(t32_data_d0950)], w[8], comp[sizeof(t32_data_d2a30)];
	u32 evlog[T32O_EVLOG][9], evn, logn, printfs;
	u32 ret;
	struct defrec dr;
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
		e->base[k] = t32o_alloc(BASEB);
		if (!ctx || !e->tune[k] || !e->buf[k] || !e->st[k] || !e->base[k])
			return 2;
		memcpy(e->tune[k], d_tune[k], TUNEB);
		memcpy(e->buf[k], d_buf[k], BUFB);
		memcpy(e->st[k], d_st[k], STB);
		memcpy(e->base[k], d_base[k], BASEB);
		*(u32 *)(ctx + 0) = T32O_PTR(e->tune[k]);
		*(u32 *)(ctx + 4) = T32O_PTR(e->buf[k]);
		t32_data_e15a8[slot[k]] = T32O_PTR(ctx);
		t32_data_e1588[slot[k]] = T32O_PTR(e->st[k]);
		t32_data_dd0a4[slot[k]] = T32O_PTR(e->base[k]);
	}
	e->in = t32o_alloc(INB);
	e->out = t32o_alloc(OUTB);
	if (!e->in || !e->out)
		return 2;
	memcpy(e->in, d_in, INB);
	memset(e->out, 0xa5, OUTB);
	memcpy(t32_data_d0950, d_hist, sizeof(d_hist));
	memcpy(t32_data_e1570, d_w, sizeof(d_w));
	memcpy(t32_data_d2a30, d_comp, sizeof(d_comp));
	return 0;
}

static void take(struct snap *s, const struct env *e)
{
	u32 k;

	for (k = 0; k < 2; k++) {
		memcpy(s->tune[k], e->tune[k], TUNEB);
		memcpy(s->buf[k], e->buf[k], BUFB);
		memcpy(s->st[k], e->st[k], STB);
		memcpy(s->base[k], e->base[k], BASEB);
	}
	memcpy(s->in, e->in, INB);
	memcpy(s->out, e->out, OUTB);
	memcpy(s->hist, t32_data_d0950, sizeof(s->hist));
	memcpy(s->w, t32_data_e1570, sizeof(s->w));
	memcpy(s->comp, t32_data_d2a30, sizeof(s->comp));
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->evn = t32o_ev_n;
	s->logn = t32o_log.n;
	s->printfs = t32o_log.printfs;
	s->dr = dr;
}

#define VC(fn, a0, a1, a2) ((u32)t32o_call((t32o_fn)(void (*)(void))fn, a0, a1, a2, 0, 0))

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
	u32 k, ret = 0;

	memset(&dr, 0, sizeof(dr));
	t32o_reset();
	if (setup(&e, ch))
		return 2;
	switch (which) {
	case C_GHIST: ret = VC(api_ae_get_hist_bin, ch, T32O_PTR(e.out), 0); break;
	case C_SHIST: ret = VC(api_ae_set_hist_bin, ch, T32O_PTR(e.in), 0); break;
	case C_YZONE: ret = VC(api_ae_get_y_zone, ch, T32O_PTR(e.out), 0); break;
	case C_GW: ret = VC(api_ae_get_weight, ch, T32O_PTR(e.out), 0); break;
	case C_SW: ret = VC(api_ae_set_weight, ch, T32O_PTR(e.in), 0); break;
	case C_AFSTEP: ret = VC(api_ae_get_antiflicker_step, ch, T32O_PTR(e.out + 8), T32O_PTR(e.out)); break;
	case C_GAF: ret = VC(tisp_ae_api_get_antiflick, ch, T32O_PTR(e.out), 0); break;
	case C_SAF: ret = VC(tisp_ae_api_set_antiflick, ch, T32O_PTR(e.in), 0); break;
	case C_COMP: ret = VC(tisp_ae_api_set_comp, ch, ((u32 *)e.in)[0], 0); break;
	}
	take(&sr, &e);
	sr.ret = ret;

	memset(&dr, 0, sizeof(dr));
	t32o_reset();
	if (setup(&e, ch))
		return 2;
	c.tune = e.tune[0];
	c.buf = e.buf[0];
	c.st = e.st[0];
	c.fps = NULL;
	c.hist = t32_data_d0950 + ch * 1052;
	c.wsel = t32_data_e1570 + ch * 2;
	c.base = e.base[0];
	c.comp = t32_data_d2a30 + ch * 1028;
	ret = 0;
	switch (which) {
	case C_GHIST: t32_aea_get_hist_bin(&c, e.out); break;
	case C_SHIST: t32_aea_set_hist_bin(&c, e.in); break;
	case C_YZONE: t32_aea_get_y_zone(&c, e.out); break;
	case C_GW: t32_aea_get_weight(&c, e.out); break;
	case C_SW: t32_aea_set_weight(&c, e.in); break;
	case C_AFSTEP: ret = (u32)t32_aea_get_antiflicker_step(&c, ch, e.out, e.out + 8, def_hook, NULL); break;
	case C_GAF: t32_aea_get_antiflick(&c, e.out); break;
	case C_SAF: t32_aea_set_antiflick(&c, ch, e.in, def_hook, NULL); break;
	case C_COMP: t32_aea_set_comp(&c, ((u32 *)e.in)[0]); break;
	}
	take(&so, &e);
	so.ret = ret;
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	if (!what && memcmp(&so.dr, &sr.dr, sizeof(so.dr)))
		what = "deflicker_cfg call";
	if (!what && has_ret[which] && so.ret != sr.ret && !(which != C_AFSTEP && !so.ret))
		what = "return value";
	if (!what && has_ret[which] && which != C_AFSTEP && sr.ret)
		what = "return value";
	for (k = 0; !what && k < 2; k++) {
		if (memcmp(so.tune[k], sr.tune[k], TUNEB))
			what = k ? "decoy tune block" : "tune block";
		else if (memcmp(so.buf[k], sr.buf[k], BUFB))
			what = k ? "decoy buf block" : "buf block";
		else if (memcmp(so.st[k], sr.st[k], STB))
			what = k ? "decoy st block" : "st block";
		else if (memcmp(so.base[k], sr.base[k], BASEB))
			what = k ? "decoy base block" : "base block";
	}
	if (!what && memcmp(so.hist, sr.hist, sizeof(so.hist)))
		what = "d0950 table";
	if (!what && memcmp(so.w, sr.w, sizeof(so.w)))
		what = "e1570 table";
	if (!what && memcmp(so.comp, sr.comp, sizeof(so.comp)))
		what = "d2a30 table";
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
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4500;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % C_N;
		u32 ch = rnd() & 3, r;
		int rc;

		fill(d_tune[0], TUNEB); fill(d_tune[1], TUNEB);
		fill(d_buf[0], BUFB); fill(d_buf[1], BUFB);
		fill(d_st[0], STB); fill(d_st[1], STB);
		fill(d_base[0], BASEB); fill(d_base[1], BASEB);
		fill(d_in, INB);
		fill(d_hist, sizeof(d_hist));
		fill(d_w, sizeof(d_w));
		fill(d_comp, sizeof(d_comp));
		/* set_weight selectors: 0, 1, 2 or random, independently */
		r = rnd() & 3;
		memset(d_in, 0, 4);
		d_in[0] = r == 3 ? (u8)rnd() : (u8)r;
		d_in[3] = (u8)(rnd() & 1) << 7;	/* sometimes a wide value */
		r = rnd() & 3;
		memset(d_in + 4, 0, 4);
		d_in[4] = r == 3 ? (u8)rnd() : (u8)r;
		/* anti flicker: enable word 0 / 1, mode word 0 / 1 (u16 at 2920 / 2922) */
		memset(d_tune[0] + 2920, 0, 4);
		d_tune[0][2920] = (u8)(rnd() % 3 ? (rnd() % 3 ? 1 : 2) : 0);
		d_tune[0][2922] = (u8)(rnd() & 1);
		/* set_antiflick mode word: 0, 1, 2, random */
		if (which == C_SAF) {
			r = rnd() & 3;
			memset(d_in, 0, 4);
			d_in[0] = r == 3 ? (u8)rnd() : (u8)r;
		}
		/* set_comp: 0, 1..128, 129.., random 16 bit */
		if (which == C_COMP) {
			r = rnd() & 3;
			*(u32 *)d_in = r == 0 ? 0 : r == 1 ? 1 + rnd() % 128 : r == 2 ? 128 + rnd() % 3 + (rnd() & 1 ? 0 : rnd() % 1000) : rnd() & 0xffff;
			if (!(rnd() & 7))
				*(u32 *)d_in |= 0x30000u;	/* only the low 16 bits count */
		}
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_aeab_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aeab_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
