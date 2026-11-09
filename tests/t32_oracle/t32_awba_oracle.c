// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AWB per-frame paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_awb_* from driver/t32/tx_isp_t32_stats.h from the same initial state and
 * compares every memory byte they change, outputs, the error prints and the
 * tail call of tisp_awb_set_start into tisp_awb_set_gain (stubbed here: its
 * arguments are compared, its body is not part of this oracle).
 *
 * Vendor memory: e16b4[ch] -> { par, st, scr }, dd0a4[ch] = tuning base.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define PAR_B	0x2000u
#define ST_B	0x1d800u
#define BASE_B	0x1000u
#define SCR_B	64u
#define OUT_B	16384u

/* weak: used when env.c does not provide them */
__attribute__((weak)) u32 t32_data_e16b4[4];
__attribute__((weak)) unsigned char t32_data_c9430[] = "tisp_awb_set_mode";
__attribute__((weak)) unsigned char t32_data_bf448[] = "invalid awb mode";
__attribute__((weak)) u32 t32_data_bf420[10] = {
	0x3f858, 0x3f860, 0x3f868, 0x3f880, 0x3f898,
	0x3f8b0, 0x3f8c8, 0x3f8e0, 0x3f8f8, 0x3f910
};

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

/* recording stand-in for the tail call of tisp_awb_set_start */
static u32 rg_n, rg[5], og_n, og[5];

u64 tisp_awb_set_gain(u32 a0, u32 a1, u32 a2, u32 a3)
{
	rg_n++;
	rg[0] = a0;
	rg[1] = *(u32 *)(uintptr_t)a1;
	rg[2] = *(u32 *)((uintptr_t)a1 + 4);
	rg[3] = a2;
	rg[4] = a3;
	return 0;
}

static void our_gain(u32 ch, const u8 *win, u32 w1, u32 w0)
{
	og_n++;
	og[0] = ch;
	og[1] = t32_ld32(win, 0);
	og[2] = t32_ld32(win, 4);
	og[3] = w1;
	og[4] = w0;
}

static u32 rng = 0x51ab37u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

enum {
	CASE_GLOBAL, CASE_ATTR, CASE_SETW, CASE_GETW, CASE_ZONE, CASE_START, CASE_GETMODE,
	CASE_SETMODE, CASE_SETFRZ, CASE_GETFRZ, CASE_SETCT, CASE_APICT, CASE_STATLOC, CASE_N
};

static const char *const names[CASE_N] = {
	"global", "attr", "setw", "getw", "zone", "start", "getmode",
	"setmode", "setfrz", "getfrz", "setct", "apict", "statloc"
};
static const u32 outlen[CASE_N] = { 16, 76, 0, 1024, 12288, 0, 28, 0, 0, 1, 0, 0, 0 };

static u8 gpar[PAR_B], gst[ST_B], gbase[BASE_B], gscr[SCR_B], garg[4096];
static u8 rpar[PAR_B], rst[ST_B], rbase[BASE_B], rscr[SCR_B], rout[OUT_B];
static u8 opar[PAR_B], ost[ST_B], obase[BASE_B], oscr[SCR_B];
static u32 oout32[OUT_B / 4];

static unsigned int compared;

static int run_case(int which, u32 ch, u32 aval)
{
	u8 *par, *st, *scr, *base, *arg, *out;
	u32 *e;
	u32 pf = 0, rn = 0;
	u8 *oout = (u8 *)oout32;
	struct t32_awb_ctx oc;
	struct t32_awb_ops ops = { our_gain };
	const char *bad = NULL;
	u32 mode = t32_ld32(garg, 0);

	t32o_reset();
	par = t32o_alloc(PAR_B);
	st = t32o_alloc(ST_B);
	scr = t32o_alloc(SCR_B);
	base = t32o_alloc(BASE_B);
	arg = t32o_alloc(4096);
	out = t32o_alloc(OUT_B);
	e = t32o_alloc(16);
	if (!par || !st || !scr || !base || !arg || !out || !e) {
		fprintf(stderr, "alloc failed\n");
		exit(2);
	}
	memcpy(par, gpar, PAR_B);
	memcpy(st, gst, ST_B);
	memcpy(scr, gscr, SCR_B);
	memcpy(base, gbase, BASE_B);
	memcpy(arg, garg, 4096);
	memset(out, 0xa5, OUT_B);
	e[0] = T32O_PTR(par);
	e[1] = T32O_PTR(st);
	e[2] = T32O_PTR(scr);
	t32_data_e16b4[ch] = T32O_PTR(e);
	t32_data_dd0a4[ch] = T32O_PTR(base);
	rg_n = 0;

#define CALL(f, a1, a2) t32o_call((t32o_fn)(void (*)(void))f, ch, a1, a2, 0, 0)
	switch (which) {
	case CASE_GLOBAL: CALL(tisp_awb_get_global_statis, T32O_PTR(out), 0); break;
	case CASE_ATTR: CALL(tisp_awb_get_attr, T32O_PTR(out), 0); break;
	case CASE_SETW: CALL(tisp_awb_set_weight, T32O_PTR(arg), 0); break;
	case CASE_GETW: CALL(tisp_awb_get_weight, T32O_PTR(out), 0); break;
	case CASE_ZONE: CALL(tisp_awb_get_zone, T32O_PTR(out), 0); break;
	case CASE_START: CALL(tisp_awb_set_start, T32O_PTR(arg), 0); break;
	case CASE_GETMODE: CALL(tisp_awb_get_mode, T32O_PTR(out), 0); break;
	case CASE_SETMODE: CALL(tisp_awb_set_mode, T32O_PTR(arg), 0); break;
	case CASE_SETFRZ: CALL(tisp_awb_set_frz, aval, 0); break;
	case CASE_GETFRZ: CALL(tisp_awb_get_frz, T32O_PTR(out), 0); break;
	case CASE_SETCT: CALL(tisp_awb_set_ct, T32O_PTR(arg), 0); break;
	case CASE_APICT: CALL(tisp_awb_api_set_ct, aval & 0xff, t32_ld32(garg, 0)); break;
	case CASE_STATLOC: CALL(tisp_awb_set_statis_localtion, T32O_PTR(arg), 0); break;
	}
#undef CALL
	rn = t32o_log.n;
	pf = t32o_log.printfs;
	memcpy(rpar, par, PAR_B);
	memcpy(rst, st, ST_B);
	memcpy(rscr, scr, SCR_B);
	memcpy(rbase, base, BASE_B);
	memcpy(rout, out, OUT_B);

	/* ours */
	memcpy(opar, gpar, PAR_B);
	memcpy(ost, gst, ST_B);
	memcpy(oscr, gscr, SCR_B);
	memcpy(obase, gbase, BASE_B);
	memset(oout, 0xa5, OUT_B);
	oc.par = opar; oc.st = ost; oc.scr = oscr; oc.base = obase;
	og_n = 0;

	switch (which) {
	case CASE_GLOBAL: t32_awb_get_global_statis(&oc, oout32); break;
	case CASE_ATTR: t32_awb_get_attr(&oc, oout32); break;
	case CASE_SETW: t32_awb_set_weight(&oc, garg); break;
	case CASE_GETW: t32_awb_get_weight(&oc, oout); break;
	case CASE_ZONE: t32_awb_get_zone(&oc, oout); break;
	case CASE_START: t32_awb_set_start(&oc, ch, garg, &ops); break;
	case CASE_GETMODE: t32_awb_get_mode(&oc, oout); break;
	case CASE_SETMODE: t32_awb_set_mode(&oc, garg); break;
	case CASE_SETFRZ: t32_awb_set_frz(&oc, (u8)aval); break;
	case CASE_GETFRZ: oout[0] = t32_awb_get_frz(&oc); break;
	case CASE_SETCT: t32_awb_set_ct(&oc, t32_ld32(garg, 0)); break;
	case CASE_APICT: t32_awb_api_set_ct(&oc, (u8)aval, t32_ld32(garg, 0)); break;
	case CASE_STATLOC: t32_awb_set_statis_localtion(&oc, garg); break;
	}

	compared++;
	if (rn)
		bad = "register writes";
	else if (pf != ((which == CASE_SETMODE && mode >= 10) ? 1u : 0u))
		bad = "error prints";
	else if (memcmp(rpar, opar, PAR_B))
		bad = "par";
	else if (memcmp(rst, ost, ST_B))
		bad = "state";
	else if (memcmp(rscr, oscr, SCR_B))
		bad = "scratch";
	else if (memcmp(rbase, obase, BASE_B))
		bad = "base";
	else if (memcmp(rout, oout, OUT_B))
		bad = "out";
	else if (rg_n != og_n || (rg_n && memcmp(rg, og, sizeof(rg))))
		bad = "set_gain call";
	else if (rg_n != (which == CASE_START ? 1u : 0u))
		bad = "set_gain count";
	(void)outlen;
	if (!bad)
		return 0;
	printf("MISMATCH %s ch %u: %s (compared %u)\n", names[which], ch, bad, compared);
	return 1;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 1000;
	unsigned int c, i, k;

	for (c = 0; c < n; c++) {
		u32 ch = rnd() & 3;

		for (i = 0; i < ST_B; i++)
			gst[i] = (u8)rnd();
		for (i = 0; i < PAR_B; i++)
			gpar[i] = (u8)rnd();
		for (i = 0; i < BASE_B; i++)
			gbase[i] = (u8)rnd();
		for (i = 0; i < SCR_B; i++)
			gscr[i] = (u8)rnd();
		for (i = 0; i < 4096; i++)
			garg[i] = (u8)rnd();
		if (c & 1)
			gst[T32_AWB_ST_MODE] = (u8)((c >> 1) % 12);
		if (c % 6 == 0) {
			static const u32 offs[4] = { T32_AWB_ST_MC, T32_AWB_ST_MD, T32_AWB_ST_ME, T32_AWB_ST_MF };

			t32_wr16(gst, offs[(c / 6) % 4], 0);
		}

		for (k = 0; k < CASE_N; k++) {
			u32 aval = rnd() & 0xff;

			if (k == CASE_SETMODE) {
				u32 m = c % 16;

				t32_wr32(garg, 0, m < 14 ? m : rnd());
			}
			if (k == CASE_APICT && c % 3 == 0)
				aval = 1;
			if (run_case((int)k, ch, aval))
				return 1;
		}
	}
	printf("t32_awba_oracle: random %u compared, 0 mismatches\n", compared);
	return 0;
}
