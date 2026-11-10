// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable RDNS port (driver/t32/tx_isp_t32_rdns.h)
 * against the unmodified vendor functions tisp_rdns_* (Matt Davis' recovered
 * C, PR #15 @ 2e2e06bc).  Both sides start from the same random state for
 * four channels (decoys); compared: every register write (address, value,
 * order), vendor return values where defined, every context field, the
 * parameter block, the tuning base buffer, the separate section buffer and
 * the get/set buffers, all inside canary frames.
 *
 * Needs from env.c: t32_data_e3600 (context array), t32_data_c0ac0 (64-byte
 * rows), private_kmalloc/vmalloc/vfree/kfree, isp_printf.  Weak fallbacks
 * below are used only if env.c does not define them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_rdns.h"

#define NCH	4
#define SECB	1540u
#define SEC_OFF	25280u
#define PARB	140u
#define BASEB	(SEC_OFF + SECB + 64u)
#define PAD	32u
#define MAXW	128

__attribute__((weak)) u32 t32_data_e3600[8];
__attribute__((weak)) u8 t32_data_c0ac0[0x4000];
/* only addresses passed to isp_printf by the init error paths */
__attribute__((weak)) u8 t32_data_ca66c[64];
__attribute__((weak)) u8 t32_data_ca654[64];
__attribute__((weak)) u8 t32_data_c0ab0[64];
__attribute__((weak)) u64 private_kmalloc(u32 size, u32 flags)
{
	(void)flags;
	return T32O_PTR(t32o_alloc(size));
}
__attribute__((weak)) u64 private_vmalloc(u32 size, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return T32O_PTR(t32o_alloc(size));
}
__attribute__((weak)) u64 private_vfree(u32 p, u32 a1, u32 a2, u32 a3)
{
	(void)p; (void)a1; (void)a2; (void)a3;
	return 0;
}
__attribute__((weak)) u64 private_kfree(u32 p, u32 a1, u32 a2, u32 a3)
{
	(void)p; (void)a1; (void)a2; (void)a3;
	return 0;
}
__attribute__((weak)) u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

extern u64 tisp_rdns_noref_reg_cfg(u32, u32, u32, u32);
extern u64 tisp_rdns_ref_reg_cfg(u32, u32, u32, u32);
extern u64 tisp_rdns_reg_trig(u32, u32, u32, u32);
extern u64 tisp_rdns_intp(u32, u32, u32, u32);
extern u64 tisp_rdns_all_reg_refresh(u32, u32, u32, u32);
extern u64 tisp_rdns_intp_reg_refresh(u32, u32, u32, u32);
extern u64 tisp_rdns_par_refresh(u32, u32, u32, u32);
extern u64 tisp_rdns_refresh(u32, u32, u32, u32);
extern u64 tisp_rdns_init(u32, u32, u32, u32);
extern u64 tisp_rdns_deinit(u32, u32, u32, u32);
extern u64 tisp_rdns_dn_params_refresh(u32, u32, u32, u32);
extern u64 tisp_rdns_param_array_get(u32, u32, u32, u32);
extern u64 tisp_rdns_param_array_set(u32, u32, u32, u32);
extern u64 tisp_rdns_wake_up(u32, u32, u32, u32);

enum { OP_NOREF, OP_REF, OP_TRIG, OP_INTP, OP_ALL, OP_INTPREG, OP_PAR, OP_REFRESH,
       OP_INIT, OP_DEINIT, OP_DN, OP_GET, OP_SET, OP_WAKE, NOPS };

static const char *const opname[NOPS] = {
	"noref_reg_cfg", "ref_reg_cfg", "reg_trig", "intp", "all_reg_refresh",
	"intp_reg_refresh", "par_refresh", "refresh", "init", "deinit",
	"dn_params_refresh", "param_array_get", "param_array_set", "wake_up"
};

static void (*const ref_fn[NOPS])(void) = {
	(void (*)(void))tisp_rdns_noref_reg_cfg, (void (*)(void))tisp_rdns_ref_reg_cfg,
	(void (*)(void))tisp_rdns_reg_trig, (void (*)(void))tisp_rdns_intp,
	(void (*)(void))tisp_rdns_all_reg_refresh, (void (*)(void))tisp_rdns_intp_reg_refresh,
	(void (*)(void))tisp_rdns_par_refresh, (void (*)(void))tisp_rdns_refresh,
	(void (*)(void))tisp_rdns_init, (void (*)(void))tisp_rdns_deinit,
	(void (*)(void))tisp_rdns_dn_params_refresh, (void (*)(void))tisp_rdns_param_array_get,
	(void (*)(void))tisp_rdns_param_array_set, (void (*)(void))tisp_rdns_wake_up
};

static u32 rs = 0x5d17a3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared;

/* ---- frames with canaries ---- */
struct fr { u8 *p; u32 len; };
static struct fr frs[64];
static unsigned int nfr;
static u8 pool[1 << 17];
static u32 pooln;
static int cur_ref;

static void *bump(u32 sz)
{
	u8 *p = pool + pooln;

	pooln += (sz + 15u) & ~15u;
	if (pooln > sizeof(pool)) {
		fprintf(stderr, "pool exhausted\n");
		exit(2);
	}
	return p;
}

static u8 *frame(u32 len)
{
	u8 *p = cur_ref ? (u8 *)t32o_alloc(len + 2 * PAD) : (u8 *)bump(len + 2 * PAD);

	memset(p, 0xC3, len + 2 * PAD);
	frs[nfr].p = p + PAD;
	frs[nfr++].len = len;
	return p + PAD;
}

static int pads_ok(void)
{
	unsigned int i, k;

	for (i = 0; i < nfr; i++)
		for (k = 0; k < PAD; k++)
			if (frs[i].p[-1 - (int)k] != 0xC3 || frs[i].p[frs[i].len + k] != 0xC3)
				return 0;
	return 1;
}

/* ---- initial state ---- */
static struct {
	u8 base[BASEB], sec[SECB], par[PARB];
	u32 gain, rsvd;
	int secmode, have, pnull;
} ci[NCH];
static u8 inbuf_init[SECB];

static void gen_sec(u8 *s, int style)
{
	unsigned int i, j, t;

	for (i = 0; i < SECB; i++) {
		switch (style) {
		case 0: s[i] = (u8)rnd(); break;
		case 1: s[i] = (u8)(rnd() & 3); break;
		case 2: s[i] = (i & 1) ? (u8)(rnd() & 1) : (u8)rnd(); break;
		default: s[i] = (u8)(rnd() & 0x1f); break;
		}
	}
	for (t = 0; t < 3; t++)		/* row selectors stay inside the table */
		for (j = 0; j < 11; j++)
			s[1460 + 11 * t + j] = (u8)(rnd() % 8);
}

static void gen_state(void)
{
	unsigned int c, i;
	u32 id = T32_RDNS_ID;

	for (c = 0; c < NCH; c++) {
		for (i = 0; i < BASEB; i++)
			ci[c].base[i] = (u8)rnd();
		gen_sec(ci[c].base + SEC_OFF, rnd() % 4);
		memcpy(ci[c].base + SEC_OFF, &id, 4);
		gen_sec(ci[c].sec, rnd() % 4);
		for (i = 0; i < PARB; i++)
			ci[c].par[i] = (u8)rnd();
		for (i = 0; i < 3; i++)
			ci[c].par[i] = (u8)(rnd() % 8);
		ci[c].gain = rnd();
		ci[c].rsvd = rnd();
		ci[c].secmode = (int)(rnd() & 1);
		ci[c].have = 1;
		ci[c].pnull = 0;
	}
	gen_sec(inbuf_init, rnd() % 4);
}

static u32 gen_gain(void)
{
	static const u32 fr[] = { 0, 1, 0x7fff, 0x8000, 0xffff };

	switch (rnd() % 6) {
	case 0: return rnd();
	case 1: return (rnd() % 13) << 16 | (rnd() & 0xffff);
	case 2: return (rnd() % 13) << 16 | fr[rnd() % 5];
	case 3: return (10 + rnd() % 2) << 16 | (rnd() & 0xffff);
	case 4: return 0xffffffffu - (rnd() % 3);
	default: return 0x10000;
	}
}

static void gen_hyst(int op, unsigned int ch, u32 *gain, u32 *thr)
{
	static const u32 ths[] = { 0, 1, 2, 100, 255, 256, 257, 1000 };
	u32 g0 = gen_gain(), t, k;

	*thr = (op == OP_REFRESH) ? 256 : ((rnd() % 3) ? ths[rnd() % 8] : (rnd() & 0xfff));
	t = (op == OP_REFRESH) ? 256 : *thr;
	switch (rnd() % 5) {
	case 0: k = t - 1; break;
	case 1: k = t; break;
	case 2: k = t + 1; break;
	case 3: k = rnd() % 0x40000; break;
	default: k = 0; break;
	}
	*gain = (rnd() & 1) ? g0 + k : g0 - k;
	ci[ch].gain = g0;
	switch (rnd() % 12) {
	case 0: case 1: ci[ch].gain = 0xffffffffu; break;
	case 2: ci[ch].gain = 0xfffffffeu; break;
	default: break;
	}
}

/* ---- the two sides ---- */
struct side {
	u8 *base[NCH], *sec[NCH], *par[NCH];
	struct t32_rdns_ctx *oc[NCH];
	u8 *in, *out;
	u32 *len;
};

static void build(struct side *s, int ref)
{
	unsigned int c;
	u32 *q;

	for (c = 0; c < NCH; c++) {
		s->base[c] = frame(BASEB);
		memcpy(s->base[c], ci[c].base, BASEB);
		s->sec[c] = frame(SECB);
		memcpy(s->sec[c], ci[c].sec, SECB);
		s->par[c] = frame(PARB);
		memcpy(s->par[c], ci[c].par, PARB);
		if (ref)
			t32_data_dd0a4[c] = T32O_PTR(s->base[c]);
		if (!ci[c].have) {
			if (ref)
				t32_data_e3600[c] = 0;
			continue;
		}
		if (ref) {
			q = (u32 *)frame(16);
			q[0] = ci[c].secmode ? T32O_PTR(s->base[c]) + SEC_OFF : T32O_PTR(s->sec[c]);
			q[1] = ci[c].pnull ? 0 : T32O_PTR(s->par[c]);
			q[2] = ci[c].gain;
			q[3] = ci[c].rsvd;
			t32_data_e3600[c] = T32O_PTR(q);
		} else {
			struct t32_rdns_ctx *o = (struct t32_rdns_ctx *)frame(sizeof(*o));

			o->sec = ci[c].secmode ? s->base[c] + SEC_OFF : s->sec[c];
			o->param = ci[c].pnull ? NULL : s->par[c];
			o->gain = ci[c].gain;
			o->rsvd = ci[c].rsvd;
			o->rows = t32_data_c0ac0;
			s->oc[c] = o;
		}
	}
	s->in = frame(SECB);
	memcpy(s->in, inbuf_init, SECB);
	s->out = frame(SECB);
	memset(s->out, 0x5a, SECB);
	s->len = (u32 *)frame(4);
	*s->len = 0x77777777u;
}

struct res {
	unsigned int n;
	u32 reg[MAXW], val[MAXW];
	u32 ret, dein;
	int hasctx[NCH], hasparam[NCH], padok;
	u32 secclass[NCH], gain[NCH], rsvd[NCH];
	u8 param[NCH][PARB], base[NCH][BASEB], sec[NCH][SECB];
	u8 in[SECB], out[SECB];
	u32 len;
};

static void extract(struct res *r, struct side *s, int ref)
{
	unsigned int c;

	for (c = 0; c < NCH; c++) {
		const u8 *secp = NULL, *parp = NULL;
		u32 gain = 0, rsvd = 0;

		memcpy(r->base[c], s->base[c], BASEB);
		memcpy(r->sec[c], s->sec[c], SECB);
		if (ref) {
			u32 cw = t32_data_e3600[c];
			const u32 *w;

			if (!cw)
				continue;
			w = (const u32 *)T32O_HOST(cw);
			secp = (const u8 *)T32O_HOST(w[0]);
			parp = (const u8 *)T32O_HOST(w[1]);
			gain = w[2];
			rsvd = w[3];
		} else {
			const struct t32_rdns_ctx *o = s->oc[c];

			if (!o)
				continue;
			secp = o->sec;
			parp = o->param;
			gain = o->gain;
			rsvd = o->rsvd;
		}
		r->hasctx[c] = 1;
		r->secclass[c] = secp == s->sec[c] ? 1 : (secp == s->base[c] + SEC_OFF ? 2 : 0);
		r->gain[c] = gain;
		r->rsvd[c] = rsvd;
		if (parp) {
			r->hasparam[c] = 1;
			memcpy(r->param[c], parp, PARB);
		}
	}
	memcpy(r->in, s->in, SECB);
	memcpy(r->out, s->out, SECB);
	r->len = *s->len;
}

/* deinit callbacks (our side) */
static unsigned int nv, nk;
static int argsok;
static void *expv, *expk;
static void o_vfree(void *p) { nv++; if (p != expv) argsok = 0; }
static void o_kfree(void *p) { nk++; if (p != expk) argsok = 0; }
static void *o_alloc(unsigned long sz)
{
	u8 *p = (u8 *)bump((u32)sz);

	memset(p, 0xEE, sz);
	return p;
}

static void die(const char *what, const char *field, unsigned int i)
{
	printf("MISMATCH %s: %s [%u]\n", what, field, i);
	printf("t32_rdns_oracle: %u compared, MISMATCH\n", compared);
	exit(1);
}

static void compare(const char *what, const struct res *a, const struct res *b)
{
	unsigned int i, c;

	compared++;
	if (!a->padok || !b->padok)
		die(what, "canary", a->padok);
	if (a->n != b->n)
		die(what, "write count", a->n * 1000 + b->n);
	for (i = 0; i < a->n; i++)
		if (a->reg[i] != b->reg[i] || a->val[i] != b->val[i]) {
			printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i, a->reg[i], a->val[i],
			       b->reg[i], b->val[i]);
			die(what, "write", i);
		}
	if (a->ret != b->ret)
		die(what, "return", a->ret);
	if (a->dein != b->dein)
		die(what, "deinit calls", a->dein);
	for (c = 0; c < NCH; c++) {
		if (a->hasctx[c] != b->hasctx[c])
			die(what, "ctx present", c);
		if (a->secclass[c] != b->secclass[c])
			die(what, "sec pointer", c);
		if (a->gain[c] != b->gain[c])
			die(what, "gain", c);
		if (a->rsvd[c] != b->rsvd[c])
			die(what, "rsvd", c);
		if (a->hasparam[c] != b->hasparam[c])
			die(what, "param present", c);
		if (memcmp(a->param[c], b->param[c], PARB))
			die(what, "param bytes", c);
		if (memcmp(a->base[c], b->base[c], BASEB))
			die(what, "tuning base", c);
		if (memcmp(a->sec[c], b->sec[c], SECB))
			die(what, "section buffer", c);
	}
	if (memcmp(a->in, b->in, SECB))
		die(what, "input buffer", 0);
	if (memcmp(a->out, b->out, SECB))
		die(what, "output buffer", 0);
	if (a->len != b->len)
		die(what, "length", a->len);
}

static void run_case(int op, unsigned int ch, u32 gain, u32 thr)
{
	static struct res rr, ro;
	static struct t32_front_write ow[MAXW];
	struct side sr, so;
	unsigned int n, i;
	u32 a1 = 0, a2 = 0;
	u64 rv;
	int ret = 0;

	memset(&rr, 0, sizeof(rr));
	memset(&ro, 0, sizeof(ro));
	memset(&sr, 0, sizeof(sr));
	memset(&so, 0, sizeof(so));

	/* vendor */
	t32o_reset();
	cur_ref = 1;
	nfr = 0;
	build(&sr, 1);
	switch (op) {
	case OP_INTP: case OP_ALL: case OP_INTPREG: case OP_REFRESH: a1 = gain; break;
	case OP_PAR: a1 = gain; a2 = thr; break;
	case OP_GET: a1 = T32O_PTR(sr.out); a2 = T32O_PTR(sr.len); break;
	case OP_SET: a1 = T32O_PTR(sr.in); break;
	default: break;
	}
	rv = t32o_call((t32o_fn)ref_fn[op], ch, a1, a2, 0, 0);
	n = t32o_log.n;
	if (n > MAXW) {
		fprintf(stderr, "too many writes %u\n", n);
		exit(2);
	}
	for (i = 0; i < n; i++) {
		rr.reg[i] = t32o_log.w[i].reg;
		rr.val[i] = t32o_log.w[i].val;
	}
	rr.n = n;
	extract(&rr, &sr, 1);
	rr.padok = pads_ok();
	switch (op) {
	case OP_REFRESH: case OP_INIT: case OP_DEINIT: case OP_DN: case OP_GET: case OP_SET:
		rr.ret = (u32)rv;
		break;
	default:
		break;
	}
	if (op == OP_DEINIT)
		rr.dein = ci[ch].have ? (1u | (ci[ch].pnull ? 0u : 1u << 4) | 1u << 8) : 1u << 8;

	/* ours */
	cur_ref = 0;
	pooln = 0;
	nfr = 0;
	build(&so, 0);
	nv = nk = 0;
	argsok = 1;
	n = 0;
	switch (op) {
	case OP_NOREF: t32_rdns_noref_reg_cfg(so.oc[ch], ow, &n); break;
	case OP_REF: t32_rdns_ref_reg_cfg(so.oc[ch], ow, &n); break;
	case OP_TRIG: t32_rdns_reg_trig(ow, &n); break;
	case OP_INTP: t32_rdns_intp(so.oc[ch], gain); break;
	case OP_ALL: ret = t32_rdns_all_reg_refresh(so.oc[ch], gain, ow); n = (unsigned)ret; break;
	case OP_INTPREG: ret = t32_rdns_intp_reg_refresh(so.oc[ch], gain, ow); n = (unsigned)ret; break;
	case OP_PAR: ret = t32_rdns_par_refresh(so.oc[ch], gain, thr, ow); n = (unsigned)ret; break;
	case OP_REFRESH: ret = t32_rdns_refresh(so.oc[ch], gain, ow); n = (unsigned)ret; break;
	case OP_INIT:
		ret = t32_rdns_init(&so.oc[ch], o_alloc, o_alloc, so.base[ch], t32_data_c0ac0, ow);
		n = ret < 0 ? 0 : (unsigned)ret;
		break;
	case OP_DEINIT:
		expk = so.oc[ch];
		expv = so.oc[ch] ? (void *)so.oc[ch]->param : NULL;
		ret = t32_rdns_deinit(o_vfree, o_kfree, &so.oc[ch]);
		break;
	case OP_DN: ret = t32_rdns_dn_params_refresh(so.oc[ch], so.base[ch], ow); n = (unsigned)ret; break;
	case OP_GET: ret = t32_rdns_param_array_get(so.oc[ch], so.out, so.len); break;
	case OP_SET: ret = t32_rdns_param_array_set(so.oc[ch], so.in, ow); n = (unsigned)ret; break;
	case OP_WAKE: ret = t32_rdns_wake_up(so.oc[ch], ow); n = (unsigned)ret; break;
	}
	if (n > MAXW)
		die(opname[op], "ours too many writes", n);
	for (i = 0; i < n; i++) {
		ro.reg[i] = ow[i].reg;
		ro.val[i] = ow[i].val;
	}
	ro.n = n;
	extract(&ro, &so, 0);
	ro.padok = pads_ok();
	switch (op) {
	case OP_GET: case OP_DEINIT: ro.ret = ret != 0; break;
	case OP_REFRESH: case OP_INIT: case OP_DN: case OP_SET: ro.ret = ret < 0; break;
	default: break;
	}
	if (op == OP_DEINIT)
		ro.dein = nk | nv << 4 | (u32)argsok << 8;

	compare(opname[op], &rr, &ro);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 1500;
	unsigned int c, i, op, ch, idx, f, zero = 1;
	u32 gain, thr;
	static const u32 fr[] = { 0, 1, 0x7fff, 0x8000, 0xffff };

	for (i = 0; i < 512; i++)
		if (t32_data_c0ac0[i])
			zero = 0;
	if (zero)			/* env.c has no row table: oracle-chosen rows, same for both sides */
		for (i = 0; i < sizeof(t32_data_c0ac0); i++)
			t32_data_c0ac0[i] = (u8)rnd();

	for (op = 0; op < NOPS; op++) {
		for (c = 0; c < n; c++) {
			gen_state();
			ch = rnd() & 3;
			gain = gen_gain();
			thr = rnd() % 3 ? (rnd() & 0x3ff) : rnd();
			if (op == OP_PAR || op == OP_REFRESH)
				gen_hyst((int)op, ch, &gain, &thr);
			if (op == OP_INIT)
				ci[ch].have = 0;
			if (op == OP_DEINIT) {
				ci[ch].have = (rnd() % 4) != 0;
				ci[ch].pnull = (int)(rnd() & 1);
			}
			run_case((int)op, ch, gain, thr);
		}
	}
	/* interpolation sweep: every table index and fraction edge */
	for (idx = 0; idx < 14; idx++)
		for (f = 0; f < 5; f++)
			for (c = 0; c < 6; c++) {
				gen_state();
				ch = rnd() & 3;
				gain = idx << 16 | fr[f];
				run_case(OP_INTP, ch, gain, 0);
				run_case(OP_ALL, ch, gain, 0);
				run_case(OP_INTPREG, ch, gain, 0);
			}
	printf("t32_rdns_oracle: %u compared, 0 mismatches (14 functions, 4 channels, canary frames)\n",
	       compared);
	return 0;
}
