// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE API accessors (tisp_api_ae_get_tgain, tisp_ae_api_
 * set_ev_start / get_luma / get_stable / get_flicker_flag / get_gobal_roi_luma /
 * get_face_ae_tune_params / set_face_ae_tune_params / get_fps / set_fps /
 * get_faceae, tisp_ae_get_reg_state).  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our t32_aea_* from
 * driver/t32/tx_isp_t32_stats.h from the same random state and compares the
 * tune / buf / st blocks of the used and of a decoy channel, the shared dd2d4
 * table, the input and output buffers, the return value (where the vendor
 * defines one), the printf count, the register and event log and every
 * argument of the dd2d4 + 212 hook (recording stub on both sides).
 * Exit 0 = all equal, 1 = first mismatch, 2 = setup failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define TUNEB	0x1000u
#define BUFB	0x3000u
#define STB	0x800u
#define OUTB	64u
#define INB	64u
#define NR_BOUND	20u	/* #350: 20-word window at st + 1300 (fixed here, not taken from the header) */

extern u32 t32_data_e15a8[4], t32_data_e1588[4];
extern u8 t32_data_dd2d4[0x400];
char t32_data_c86bc[4] = "f", t32_data_c86e0[4] = "f", t32_data_bf3a4[4] = "f";

extern u64 api_ae_get_tgain(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_set_ev_start(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_luma(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_stable(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_flicker_flag(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_gobal_roi_luma(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_face_ae_tune_params(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_set_face_ae_tune_params(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_fps(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_set_fps(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_api_get_faceae(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ae_get_reg_state(u32 a0, u32 a1, u32 a2, u32 a3);

/* dd2d4 + ch * 232 + 212: recording stand-in (vendor call: fps, &buf, ch * 232, a3) */
struct fpsrec {
	u32 n, a0, a2, a3, chbuf;
};
static struct fpsrec fr;

u64 fps_stub(u32 a0, u32 a1, u32 a2, u32 a3)
{
	fr.n++;
	fr.a0 = a0;
	fr.a2 = a2;
	fr.a3 = a3;
	fr.chbuf = *(u32 *)T32O_HOST(a1 + 8);
	return 0;
}

static unsigned int our_warn;
static void warn_hook(void *priv)
{
	(void)priv;
	our_warn++;
}

static void fps_hook(void *priv, u32 fps, u32 ch)
{
	(void)priv;
	fr.n++;
	fr.a0 = fps;
	fr.a2 = ch * 232;
	fr.a3 = 0;
	fr.chbuf = ch;
}

static u32 rs = 0x7f4a7c15u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_TGAIN, C_EVSTART, C_LUMA, C_STABLE, C_FLICK, C_ROI, C_GFACE, C_SFACE, C_GFPS, C_SFPS,
       C_FACEAE, C_REGST, C_N };
static const char *const names[C_N] = {
	"get_tgain", "set_ev_start", "get_luma", "get_stable", "get_flicker_flag",
	"get_gobal_roi_luma", "get_face_ae_tune_params", "set_face_ae_tune_params", "get_fps",
	"set_fps", "get_faceae", "get_reg_state"
};
/* the vendor returns a defined 0 / status for these, a leftover register for the others */
static const int has_ret[C_N] = { 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0 };

struct env {
	u8 *tune[2], *buf[2], *st[2], *in, *out;
};
static u8 d_tune[2][TUNEB], d_buf[2][BUFB], d_st[2][STB], d_in[INB], d_dd[0x400];
struct snap {
	u8 tune[2][TUNEB], buf[2][BUFB], st[2][STB], in[INB], out[OUTB], dd[0x400];
	u32 evlog[T32O_EVLOG][9], evn, logn, printfs;
	u64 ret;
	struct fpsrec fr;
};
static struct snap sr;
static unsigned int n_refused, n_inrange;

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
	memcpy(t32_data_dd2d4, d_dd, sizeof(d_dd));
	/* the per-channel hook pointer of the used and the decoy channel */
	*(u32 *)(t32_data_dd2d4 + ch * 232 + 212) = T32O_PTR(fps_stub);
	*(u32 *)(t32_data_dd2d4 + ((ch + 1) & 3) * 232 + 212) = T32O_PTR(fps_stub);
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
	memcpy(s->dd, t32_data_dd2d4, sizeof(s->dd));
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->evn = t32o_ev_n;
	s->logn = t32o_log.n;
	s->printfs = t32o_log.printfs;
	s->fr = fr;
}

#define VC(fn, a0, a1, a2, a3) t32o_call((t32o_fn)(void (*)(void))fn, a0, a1, a2, a3, 0)

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
	struct snap so;
	const char *what = NULL;
	u32 k;
	u64 ret = 0;
	u32 *in = NULL, *out = NULL;

	memset(&fr, 0, sizeof(fr));
	t32o_reset();
	if (setup(&e, ch))
		return 2;
	switch (which) {
	case C_TGAIN: ret = VC(api_ae_get_tgain, ch, T32O_PTR(e.out), 0, 0); break;
	case C_EVSTART: ret = VC(tisp_ae_api_set_ev_start, ch, 0, ((u32 *)e.in)[0], ((u32 *)e.in)[1]); break;
	case C_LUMA: ret = VC(tisp_ae_api_get_luma, ch, T32O_PTR(e.out), 0, 0); break;
	case C_STABLE: ret = VC(tisp_ae_api_get_stable, ch, T32O_PTR(e.out), 0, 0); break;
	case C_FLICK: ret = VC(tisp_ae_api_get_flicker_flag, ch, T32O_PTR(e.out), 0, 0); break;
	case C_ROI: ret = VC(tisp_ae_api_get_gobal_roi_luma, ch, T32O_PTR(e.out), T32O_PTR(e.out + 8), 0); break;
	case C_GFACE: ret = VC(tisp_ae_api_get_face_ae_tune_params, ch, T32O_PTR(e.out), T32O_PTR(e.out + 8), T32O_PTR(e.out + 16)); break;
	case C_SFACE: ret = VC(tisp_ae_api_set_face_ae_tune_params, ch, T32O_PTR(e.in), T32O_PTR(e.in + 8), T32O_PTR(e.in + 16)); break;
	case C_GFPS: ret = VC(tisp_ae_api_get_fps, ch, T32O_PTR(e.out), 0, 0); break;
	case C_SFPS: ret = VC(tisp_ae_api_set_fps, ch, T32O_PTR(e.in), 0, 0); break;
	case C_FACEAE: ret = VC(tisp_ae_api_get_faceae, ch, T32O_PTR(e.out), 0, 0); break;
	case C_REGST: ret = VC(tisp_ae_get_reg_state, ch, ((u32 *)e.in)[0], 0, 0); break;
	}
	take(&sr, &e);
	sr.ret = (u32)ret;

	memset(&fr, 0, sizeof(fr));
	our_warn = 0;
	t32o_reset();
	if (setup(&e, ch))
		return 2;
	c.tune = e.tune[0];
	c.buf = e.buf[0];
	c.st = e.st[0];
	c.fps = t32_data_dd2d4 + ch * 232;
	in = (u32 *)e.in;
	out = (u32 *)e.out;
	so.ret = 0;
	switch (which) {
	case C_TGAIN: out[0] = t32_aea_get_tgain(&c); break;
	case C_EVSTART: t32_aea_set_ev_start(&c, in[0], in[1]); break;
	case C_LUMA: e.out[0] = t32_aea_get_luma(&c); break;
	case C_STABLE: t32_aea_get_stable(&c, out); break;
	case C_FLICK: out[0] = t32_aea_get_flicker_flag(&c); break;
	case C_ROI: t32_aea_get_gobal_roi_luma(&c, &out[0], &out[2]); break;
	case C_GFACE: t32_aea_get_face_ae_tune_params(&c, &out[0], &out[2], &out[4]); break;
	case C_SFACE: so.ret = (u32)t32_aea_set_face_ae_tune_params(&c, &in[0], &in[2], &in[4], warn_hook, NULL); break;
	case C_GFPS: out[0] = t32_aea_get_fps(&c); break;
	case C_SFPS: so.ret = (u32)t32_aea_set_fps(&c, ch, in[0], fps_hook, warn_hook, NULL); break;
	case C_FACEAE: t32_aea_get_faceae(&c, out); break;
	case C_REGST: t32_aea_set_reg_state(&c, in[0]); break;
	}
	if (has_ret[which])
		so.ret = which == C_SFPS || which == C_SFACE ? so.ret : 0;
	take(&so, &e);
	so.printfs = our_warn;
	/*
	 * #350 (beyond vendor, user-approved): a window length > 20 is refused
	 * with -EINVAL and nothing is written; the vendor stores it (named
	 * difference, counted, our side checked against the unchanged inputs).
	 */
	if (which == C_SFACE && in[4] > NR_BOUND) {
		n_refused++;
		if ((u32)so.ret != (u32)-EINVAL)
			what = "refused: return value";
		else if (so.logn || so.evn || so.printfs || so.fr.n)
			what = "refused: side effect";
		for (k = 0; !what && k < 2; k++)
			if (memcmp(so.tune[k], d_tune[k], TUNEB) || memcmp(so.buf[k], d_buf[k], BUFB) ||
			    memcmp(so.st[k], d_st[k], STB))
				what = "refused: state written";
		if (!what && (memcmp(so.in, d_in, INB) || memcmp(so.dd, sr.dd, sizeof(so.dd))))
			what = "refused: input / dd2d4 changed";
		if (what) {
			printf("MISMATCH %s ch %u: %s\n", names[which], ch, what);
			return 1;
		}
		return 0;
	}
	if (which == C_SFACE)
		n_inrange++;
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	if (!what && so.printfs != sr.printfs)
		what = "printf count";
	if (!what && memcmp(&so.fr, &sr.fr, sizeof(so.fr)))
		what = "fps hook call";
	if (!what && has_ret[which] && (u32)so.ret != (u32)sr.ret)
		what = "return value";
	for (k = 0; !what && k < 2; k++) {
		if (memcmp(so.tune[k], sr.tune[k], TUNEB))
			what = k ? "decoy tune block" : "tune block";
		else if (memcmp(so.buf[k], sr.buf[k], BUFB))
			what = k ? "decoy buf block" : "buf block";
		else if (memcmp(so.st[k], sr.st[k], STB))
			what = k ? "decoy st block" : "st block";
	}
	if (!what && memcmp(so.dd, sr.dd, sizeof(so.dd)))
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
		u32 ch = rnd() & 3, r;
		int rc;

		fill(d_tune[0], TUNEB); fill(d_tune[1], TUNEB);
		fill(d_buf[0], BUFB); fill(d_buf[1], BUFB);
		fill(d_st[0], STB); fill(d_st[1], STB);
		fill(d_in, INB);
		fill(d_dd, sizeof(d_dd));
		r = rnd() & 7;
		if (r == 0)
			d_tune[0][2770] = d_tune[0][2771] = 0;	/* zero divisor of get_luma */
		else if (r == 1)
			d_buf[0][11796] = d_buf[0][11797] = 0;
		else if (r == 2)
			d_buf[0][11826] = d_buf[0][11827] = 0;
		/* set_fps: tune + 1740 == 1 (error path) sometimes, fps equal to the stored one sometimes */
		if (!(rnd() & 3))
			d_tune[0][1740] = 1, d_tune[0][1741] = d_tune[0][1742] = d_tune[0][1743] = 0;
		if (!(rnd() & 3))
			memcpy(d_in, d_dd + ch * 232 + 56, 4);
		if (!(rnd() & 3))
			memset(d_in + 16, 0, 4);	/* p3 == 0 of set_face_ae_tune_params */
		else if (rnd() & 1) {
			u32 w = rnd() % 26;	/* #350: window length around the bound 20 */

			memcpy(d_in + 16, &w, 4);
		}
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_aeaa_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aeaa_oracle: random %u compared, 0 mismatches (set_face_ae_tune_params: %u in range bit-exact, %u refused > %u, #350 named difference)\n",
	       n, n_inrange, n_refused, NR_BOUND);
	return 0;
}
