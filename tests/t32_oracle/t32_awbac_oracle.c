// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AWB per-frame control paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_awb_* from driver/t32/tx_isp_t32_stats.h from the same initial state
 * and compares every register write (address, value, order), the whole
 * tune block, section block, tuning base, mode block and output buffer.
 *
 * Vendor layout: e16b4[ch] -> { tune, sec, mode }, dd0a4[ch] -> tuning base.
 * tisp_awb_set_start tail-calls tisp_awb_set_gain; our hook calls the vendor
 * tisp_awb_set_gain from our state, so the stores and the call arguments are
 * compared through its register writes.
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
#define TBB	0x8000u
#define OUTB	0x4000u
#define INB	4096u
#define MODEB	64u

extern u32 t32_data_e16b4[4];

extern u64 tisp_awb_get_global_statis(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_get_attr(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_set_weight(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_get_weight(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_get_zone(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_set_start(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_get_mode(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_set_mode(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_set_frz(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_get_frz(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_set_ct(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_api_set_ct(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_set_statis_localtion(u32 a0, u32 a1, u32 a2, u32 a3);

/* link-only data of the recovered tisp_awb_set_mode: printf strings (never
 * dereferenced, isp_printf is stubbed) and its jump table, whose entries must
 * equal the case label constants of the recovered switch. */
u8 t32_data_bf448[4] __asm__("t32_data_bf448");
u8 t32_data_c9430[4] __asm__("t32_data_c9430");
u32 t32_data_bf420[10] __asm__("t32_data_bf420") = {
	0x3f858, 0x3f860, 0x3f868, 0x3f880, 0x3f898,
	0x3f8b0, 0x3f8c8, 0x3f8e0, 0x3f8f8, 0x3f910
};

/* stand-in for the vendor tisp_awb_set_gain (tail call of set_start): records
 * its arguments; the real one needs state that tisp_awb_init builds. */
struct gainrec {
	u32 n, a0, a2, a3, in0, in1, in2;
};
static struct gainrec g;

u64 tisp_awb_set_gain(u32 a0, u32 a1, u32 a2, u32 a3)
{
	const u32 *in = (const u32 *)T32O_HOST(a1);

	g.n++;
	g.a0 = a0;
	g.a2 = a2;
	g.a3 = a3;
	g.in0 = in[0];
	g.in1 = in[1];
	g.in2 = in[2];
	return 0;
}

static u32 rs = 0x51ab77u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum {
	CASE_GSTAT, CASE_ATTR, CASE_SETW, CASE_GETW, CASE_GETZ, CASE_START, CASE_GETM,
	CASE_SETM, CASE_SETFRZ, CASE_GETFRZ, CASE_SETCT, CASE_APICT, CASE_LOC, CASE_N
};

static const char *const names[CASE_N] = {
	"get_global_statis", "get_attr", "set_weight", "get_weight", "get_zone", "set_start",
	"get_mode", "set_mode", "set_frz", "get_frz", "set_ct", "api_set_ct", "set_statis_localtion"
};

struct case_data {
	u8 tune[TUNEB];
	u8 sec[SECB];
	u8 tb[TBB];
	u8 mode[MODEB];
	u32 in[INB / 4];
};

struct env {
	u8 *tune, *sec, *tb, *mode, *in, *out;
	u32 ch;
};

static struct case_data cd;
static u8 r_tune[TUNEB], r_sec[SECB], r_tb[TBB], r_mode[MODEB], r_out[OUTB];
static struct { u32 reg, val; } r_log[T32O_MAX_WRITES];
static unsigned int r_n;

static int setup(struct env *e, u32 ch)
{
	u8 *ctx;

	e->tune = t32o_alloc(TUNEB);
	e->sec = t32o_alloc(SECB);
	e->tb = t32o_alloc(TBB);
	e->mode = t32o_alloc(MODEB);
	e->in = t32o_alloc(INB);
	e->out = t32o_alloc(OUTB);
	ctx = t32o_alloc(16);
	if (!e->tune || !e->sec || !e->tb || !e->mode || !e->in || !e->out || !ctx)
		return 2;
	memcpy(e->tune, cd.tune, TUNEB);
	memcpy(e->sec, cd.sec, SECB);
	memcpy(e->tb, cd.tb, TBB);
	memcpy(e->mode, cd.mode, MODEB);
	memcpy(e->in, cd.in, INB);
	memset(e->out, 0, OUTB);
	*(u32 *)(ctx + 0) = T32O_PTR(e->tune);
	*(u32 *)(ctx + 4) = T32O_PTR(e->sec);
	*(u32 *)(ctx + 8) = T32O_PTR(e->mode);
	*(u32 *)(ctx + 12) = 0;
	t32_data_e16b4[ch] = T32O_PTR(ctx);
	t32_data_dd0a4[ch] = T32O_PTR(e->tb);
	e->ch = ch;
	return 0;
}

static void gain_hook(void *priv, u32 ch, const u32 *in, u32 a2, u32 a3)
{
	(void)priv;
	t32o_call((t32o_fn)(void (*)(void))tisp_awb_set_gain, ch, T32O_PTR(in), a2, a3, 0);
}

#define VC(fn, a0, a1, a2) t32o_call((t32o_fn)(void (*)(void))fn, a0, a1, a2, 0, 0)

static int run_case(unsigned int which, u32 ch, u32 flag, u32 ct)
{
	struct env e;
	struct t32_awb_ctx c;
	unsigned int i;
	const char *what = NULL;
	u32 *in32;
	struct gainrec rg;

	memset(&g, 0, sizeof(g));
	t32o_reset();
	if (setup(&e, ch))
		return 2;
	in32 = (u32 *)e.in;
	(void)in32;
	switch (which) {
	case CASE_GSTAT: VC(tisp_awb_get_global_statis, ch, T32O_PTR(e.out), 0); break;
	case CASE_ATTR: VC(tisp_awb_get_attr, ch, T32O_PTR(e.out), 0); break;
	case CASE_SETW: VC(tisp_awb_set_weight, ch, T32O_PTR(e.in), 0); break;
	case CASE_GETW: VC(tisp_awb_get_weight, ch, T32O_PTR(e.out), 0); break;
	case CASE_GETZ: VC(tisp_awb_get_zone, ch, T32O_PTR(e.out), 0); break;
	case CASE_START: VC(tisp_awb_set_start, ch, T32O_PTR(e.in), 0); break;
	case CASE_GETM: VC(tisp_awb_get_mode, ch, T32O_PTR(e.out), 0); break;
	case CASE_SETM: VC(tisp_awb_set_mode, ch, T32O_PTR(e.in), 0); break;
	case CASE_SETFRZ: VC(tisp_awb_set_frz, ch, cd.in[0], 0); break;
	case CASE_GETFRZ: VC(tisp_awb_get_frz, ch, T32O_PTR(e.out), 0); break;
	case CASE_SETCT: VC(tisp_awb_set_ct, ch, T32O_PTR(e.in), 0); break;
	case CASE_APICT: VC(tisp_awb_api_set_ct, ch, flag, ct); break;
	case CASE_LOC: VC(tisp_awb_set_statis_localtion, ch, T32O_PTR(e.in), 0); break;
	}
	rg = g;
	memset(&g, 0, sizeof(g));
	memcpy(r_tune, e.tune, TUNEB);
	memcpy(r_sec, e.sec, SECB);
	memcpy(r_tb, e.tb, TBB);
	memcpy(r_mode, e.mode, MODEB);
	memcpy(r_out, e.out, OUTB);
	r_n = t32o_log.n;
	if (r_n > T32O_MAX_WRITES)
		r_n = T32O_MAX_WRITES;
	for (i = 0; i < r_n; i++) {
		r_log[i].reg = t32o_log.w[i].reg;
		r_log[i].val = t32o_log.w[i].val;
	}
	{
		unsigned int rn_full = t32o_log.n;

		t32o_reset();
		if (setup(&e, ch))
			return 2;
		c.tune = e.tune;
		c.sec = e.sec;
		c.mode = (u32 *)e.mode;
		c.base = e.tb;
		switch (which) {
		case CASE_GSTAT: t32_awb_get_global_statis(&c, (u32 *)e.out); break;
		case CASE_ATTR: t32_awb_get_attr(&c, (u32 *)e.out); break;
		case CASE_SETW: t32_awb_set_weight(&c, e.in); break;
		case CASE_GETW: t32_awb_get_weight(&c, e.out); break;
		case CASE_GETZ: t32_awb_get_zone(&c, e.out); break;
		case CASE_START: t32_awb_set_start(&c, ch, (const u32 *)e.in, gain_hook, NULL); break;
		case CASE_GETM: t32_awb_get_mode(&c, (u32 *)e.out); break;
		case CASE_SETM: t32_awb_set_mode(&c, (const u32 *)e.in); break;
		case CASE_SETFRZ: t32_awb_set_frz(&c, cd.in[0]); break;
		case CASE_GETFRZ: e.out[0] = t32_awb_get_frz(&c); break;
		case CASE_SETCT: t32_awb_set_ct(&c, ((const u32 *)e.in)[0]); break;
		case CASE_APICT: t32_awb_api_set_ct(&c, flag, ct); break;
		case CASE_LOC: t32_awb_set_statis_localtion(&c, (const u32 *)e.in); break;
		}
		if (rn_full != t32o_log.n)
			what = "register write count";
	}
	if (!what)
		for (i = 0; i < r_n; i++)
			if (r_log[i].reg != t32o_log.w[i].reg || r_log[i].val != t32o_log.w[i].val) {
				what = "register write";
				break;
			}
	if (!what && memcmp(&rg, &g, sizeof(g)))
		what = "set_gain call";
	if (!what && memcmp(r_tune, e.tune, TUNEB))
		what = "tune block";
	if (!what && memcmp(r_sec, e.sec, SECB))
		what = "section block";
	if (!what && memcmp(r_tb, e.tb, TBB))
		what = "tuning base";
	if (!what && memcmp(r_mode, e.mode, MODEB))
		what = "mode block";
	if (!what && memcmp(r_out, e.out, OUTB))
		what = "output buffer";
	if (what) {
		printf("MISMATCH %s ch %u: %s\n", names[which], ch, what);
		return 1;
	}
	return 0;
}

static void fill32(u8 *p, u32 bytes)
{
	u32 i;

	for (i = 0; i < bytes; i += 4) {
		u32 v = rnd();

		p[i] = (u8)v; p[i + 1] = (u8)(v >> 8); p[i + 2] = (u8)(v >> 16); p[i + 3] = (u8)(v >> 24);
	}
}

static void gen_case(unsigned int which, u32 *flag, u32 *ct)
{
	static const u32 flags[4] = { 1, 0x101, 0x100, 0 };
	u32 r;

	fill32(cd.tune, TUNEB);
	fill32(cd.sec, SECB);
	fill32(cd.tb, TBB);
	fill32(cd.mode, MODEB);
	fill32((u8 *)cd.in, INB);
	r = rnd() % 4;
	if (r == 0)
		cd.sec[T32_AWB_SEC_MODE] = 9;
	else if (r == 1)
		cd.sec[T32_AWB_SEC_MODE] = 0;
	else if (r == 2)
		cd.sec[T32_AWB_SEC_MODE] = (u8)(rnd() % 12);
	if (!(rnd() & 7))
		cd.sec[T32_AWB_SEC_A0] = cd.sec[T32_AWB_SEC_A0 + 1] = 0;
	if (!(rnd() & 7))
		cd.sec[T32_AWB_SEC_A1] = cd.sec[T32_AWB_SEC_A1 + 1] = 0;
	if (!(rnd() & 7))
		cd.sec[T32_AWB_SEC_B0] = cd.sec[T32_AWB_SEC_B0 + 1] = 0;
	if (!(rnd() & 7))
		cd.sec[T32_AWB_SEC_B1] = cd.sec[T32_AWB_SEC_B1 + 1] = 0;
	if (which == CASE_SETM) {
		r = rnd() % 4;
		cd.in[0] = r ? rnd() % 12 : rnd();
	}
	r = rnd() % 5;
	*flag = r < 4 ? flags[r] : rnd();
	*ct = rnd();
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3900;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % CASE_N;
		u32 flag, ct, ch = rnd() & 3;
		int rc;

		gen_case(which, &flag, &ct);
		rc = run_case(which, ch, flag, ct);
		if (rc) {
			printf("t32_awbac_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_awbac_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
