// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AWB parameter array (tisp_awb_param_array_get /
 * tisp_awb_param_array_set).  Runs the unmodified vendor functions (Matt
 * Davis' recovered C, PR #15 @ 2e2e06bc) and our t32_awb_param_array_* from
 * driver/t32/tx_isp_t32_stats.h from the same random state and compares the
 * tune / sec / hw blocks of the used and of a decoy channel, the input and
 * output buffers, the length word, the call sequence of tisp_awb_params_refresh /
 * tisp_awb_set_hardware_param / tisp_awb_show_para_update (recording stubs on
 * both sides) and the register and event log.
 * Exit 0 = all equal, 1 = first mismatch, 2 = setup failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define SECB	0x1e000u
#define TUNEB	0x2000u
#define HWB	0x1200u
#define OUTB	0x3000u
#define INB	0x3000u

extern u32 t32_data_e16b4[4];

extern u64 tisp_awb_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);

struct callrec {
	u32 n, id[8], a0[8], a1[8];
};
static struct callrec cr;

static void rec(u32 id, u32 a0, u32 a1)
{
	if (cr.n < 8) {
		cr.id[cr.n] = id;
		cr.a0[cr.n] = a0;
		cr.a1[cr.n] = a1;
	}
	cr.n++;
}

u64 tisp_awb_params_refresh(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	rec(1, a0, a1);
	return 0;
}

u64 tisp_awb_set_hardware_param(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rec(2, a0, 0);
	return 0;
}

u64 tisp_awb_show_para_update(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rec(3, a0, 0);
	return 0;
}

static void refresh_hook(void *priv, u32 ch, u8 *tune)
{
	(void)priv;
	rec(1, ch, T32O_PTR(tune));
}

static void hw_hook(void *priv, u32 ch)
{
	(void)priv;
	rec(2, ch, 0);
}

static void show_hook(void *priv, u32 ch)
{
	(void)priv;
	rec(3, ch, 0);
}

static u32 rs = 0x68e31da4u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_GET, C_SET, C_N };
static const char *const names[C_N] = { "param_array_get", "param_array_set" };

struct env {
	u8 *tune[2], *sec[2], *hw[2], *in, *out;
	u32 *len;
};
static u8 d_tune[2][TUNEB], d_sec[2][SECB], d_hw[2][HWB], d_in[INB];
struct snap {
	u8 tune[2][TUNEB], sec[2][SECB], hw[2][HWB], in[INB], out[OUTB];
	u32 len, evlog[T32O_EVLOG][9], evn, logn;
	struct callrec cr;
};
static struct snap sr, so;

static int setup(struct env *e, u32 ch)
{
	u32 k, slot[2] = { ch, (ch + 1) & 3 };

	for (k = 0; k < 2; k++) {
		u8 *ctx = t32o_alloc(16);

		e->tune[k] = t32o_alloc(TUNEB);
		e->sec[k] = t32o_alloc(SECB);
		e->hw[k] = t32o_alloc(HWB);
		if (!ctx || !e->tune[k] || !e->sec[k] || !e->hw[k])
			return 2;
		memcpy(e->tune[k], d_tune[k], TUNEB);
		memcpy(e->sec[k], d_sec[k], SECB);
		memcpy(e->hw[k], d_hw[k], HWB);
		*(u32 *)(ctx + 0) = T32O_PTR(e->tune[k]);
		*(u32 *)(ctx + 4) = T32O_PTR(e->sec[k]);
		*(u32 *)(ctx + 8) = 0;
		*(u32 *)(ctx + 12) = T32O_PTR(e->hw[k]);
		t32_data_e16b4[slot[k]] = T32O_PTR(ctx);
	}
	e->in = t32o_alloc(INB);
	e->out = t32o_alloc(OUTB);
	e->len = (u32 *)t32o_alloc(16);
	if (!e->in || !e->out || !e->len)
		return 2;
	memcpy(e->in, d_in, INB);
	memset(e->out, 0xa5, OUTB);
	*e->len = 0x5a5a5a5au;
	memset(&cr, 0, sizeof(cr));
	return 0;
}

static void take(struct snap *s, const struct env *e)
{
	u32 k;

	for (k = 0; k < 2; k++) {
		memcpy(s->tune[k], e->tune[k], TUNEB);
		memcpy(s->sec[k], e->sec[k], SECB);
		memcpy(s->hw[k], e->hw[k], HWB);
	}
	memcpy(s->in, e->in, INB);
	memcpy(s->out, e->out, OUTB);
	s->len = *e->len;
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->evn = t32o_ev_n;
	s->logn = t32o_log.n;
	s->cr = cr;
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
	struct t32_awb_ctx c;
	const char *what = NULL;
	u32 k;

	t32o_reset();
	if (setup(&e, ch))
		return 2;
	switch (which) {
	case C_GET: VC(tisp_awb_param_array_get, ch, T32O_PTR(e.out), T32O_PTR(e.len)); break;
	case C_SET: VC(tisp_awb_param_array_set, ch, T32O_PTR(e.in), 0); break;
	}
	take(&sr, &e);

	t32o_reset();
	if (setup(&e, ch))
		return 2;
	memset(&c, 0, sizeof(c));
	c.tune = e.tune[0];
	c.sec = e.sec[0];
	switch (which) {
	case C_GET: t32_awb_param_array_get(&c, e.hw[0], e.out, e.len); break;
	case C_SET: t32_awb_param_array_set(&c, e.hw[0], ch, e.in, refresh_hook, hw_hook, show_hook, NULL); break;
	}
	take(&so, &e);
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	if (!what && memcmp(&so.cr, &sr.cr, sizeof(sr.cr)))
		what = "call sequence";
	if (!what && so.len != sr.len)
		what = "length word";
	for (k = 0; !what && k < 2; k++) {
		if (memcmp(so.tune[k], sr.tune[k], TUNEB))
			what = k ? "decoy tune block" : "tune block";
		else if (memcmp(so.sec[k], sr.sec[k], SECB))
			what = k ? "decoy sec block" : "sec block";
		else if (memcmp(so.hw[k], sr.hw[k], HWB))
			what = k ? "decoy hw block" : "hw block";
	}
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
		u32 ch = rnd() & 3, k, r;
		int rc;

		for (k = 0; k < 2; k++) {
			fill(d_tune[k], TUNEB);
			fill(d_sec[k], SECB);
			fill(d_hw[k], HWB);
			r = rnd() % 3;
			if (r == 0)
				d_sec[k][T32_AWB_SEC_FRZ] = 0;	/* freeze off: the hardware is programmed */
			else if (r == 1)
				d_sec[k][T32_AWB_SEC_FRZ] = 1;
		}
		fill(d_in, INB);
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_awbab_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_awbab_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
