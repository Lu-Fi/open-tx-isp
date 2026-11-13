// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE per-frame paths tisp_ae_short_expt, tisp_ae_short_ev_alloc
 * and tisp_ae_long_ev_alloc (Matt Davis' recovered C, PR #15 @ 2e2e06bc) against
 * t32_ae_short_expt / t32_ae_short_ev_alloc / t32_ae_long_ev_alloc of
 * driver/t32/tx_isp_t32_stats.h.  Both sides run from the same random state
 * (tuning, buffer, channel state, ev ctx) and call the same reference callees
 * through hooks.  Compared: register writes in order, tuning/buffer/state
 * memory, events pushed, calls of the sensor setters.
 * Callees without a ref here (sensor setters, get_bv, log2, bilinear, reg_read,
 * event_push, runningMode, __ashldi3/__lshrdi3) get weak host stubs below; a
 * strong definition from env.c/refs wins.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define TUNE_SZ	4096u
#define BUF_SZ	0x3000u
#define ST_SZ	3072u
#define MAXW	64
#define WEAK	__attribute__((weak))

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
u64 __ashldi3(u32, u32, u32);
u64 __lshrdi3(u32, u32, u32);

extern u32 t32_data_e15a8[], t32_data_e1588[];
extern u8 t32_data_dd2d4[];
u64 tisp_ae_short_expt(u32, u32, u32, u32);
u64 tisp_ae_short_ev_alloc(u32, u32, u32, u32);
u64 tisp_ae_long_ev_alloc(u32, u32, u32, u32);
u64 tisp_ae_short_ev_alloc_calc(u32, u32, u32, u32);
u64 fix_point_mult2_32(u32, u32, u32, u32);
u64 fix_point_mult3_64(u32, u32, u32, u32, u32, u32, u32, u32);
u64 fix_point_div_64(u32, u32, u32, u32, u32, u32, u32, u32);
u64 tisp_ae_short_set_sensor_integration_time(u32, u32, u32, u32);
u64 tisp_set_ae_short_ag(u32, u32, u32, u32);
u64 tisp_ae_long_set_sensor_integration_time(u32, u32, u32, u32);
u64 tisp_set_ae_long_ag(u32, u32, u32, u32);
u64 tisp_ae_get_bv(u32, u32, u32, u32);
u64 lib_tisp_api_g_runningMode(u32, u32, u32, u32);
u64 tisp_reg_read(u32, u32, u32, u32);
u64 tisp_event_push(u32, u32, u32, u32);
u64 tisp_log2_fixed_to_fixed(u32, u32, u32, u32);
u64 tisp_ae_lib_bilinear_intp(u32, u32, u32, u32, u32, u32, u32, u32);

static u32 rs = 0x51c3a7u, rdseed, gmode;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 rdh(u32 reg) { return reg * 0x9e3779b1u ^ rdseed; }

/* logs of the weak stubs */
static u32 wl[64], nwl, evl[16][9], nev;
static void wlog(u32 id, u32 a)
{
	if (nwl < 62) { wl[nwl++] = id; wl[nwl++] = a; }
}
WEAK u64 tisp_ae_short_set_sensor_integration_time(u32 a0, u32 a1, u32 a2, u32 a3) { wlog(1, a0); return 0; }
WEAK u64 tisp_set_ae_short_ag(u32 a0, u32 a1, u32 a2, u32 a3) { wlog(2, a0); return 0; }
WEAK u64 tisp_ae_long_set_sensor_integration_time(u32 a0, u32 a1, u32 a2, u32 a3) { wlog(3, a0); return 0; }
WEAK u64 tisp_set_ae_long_ag(u32 a0, u32 a1, u32 a2, u32 a3) { wlog(4, a0); return 0; }
WEAK u64 tisp_ae_get_bv(u32 a0, u32 a1, u32 a2, u32 a3) { return 0x1234500u + a0 * 77; }
WEAK u64 lib_tisp_api_g_runningMode(u32 a0, u32 a1, u32 a2, u32 a3) { return gmode; }
WEAK u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { return rdh(a1); }
WEAK u64 tisp_log2_fixed_to_fixed(u32 a0, u32 a1, u32 a2, u32 a3) { return (a0 * 3u) ^ (a0 >> 5) ^ a1; }
WEAK u64 tisp_ae_lib_bilinear_intp(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7)
{
	return ((u64)(a0 ^ a2 ^ a4 ^ a6 ^ 0x55) << 32) | (a1 + a3 + a5 + a7 + a0 * 3);
}
WEAK u64 tisp_event_push(u32 a0, u32 a1, u32 a2, u32 a3)
{
	const u32 *p = T32O_HOST(a1);
	unsigned int i;

	if (nev < 16) {
		evl[nev][0] = a0;
		for (i = 0; i < 8; i++)
			evl[nev][1 + i] = p[i];
	}
	nev++;
	return 0;
}
WEAK u64 __ashldi3(u32 lo, u32 hi, u32 sh) { return ((u64)hi << 32 | lo) << (sh & 63); }
WEAK u64 __lshrdi3(u32 lo, u32 hi, u32 sh) { return ((u64)hi << 32 | lo) >> (sh & 63); }

/* hooks of the port: the same reference callees */
static u32 h_mult3_32(u32 sh, u32 a, u32 b, u32 c)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))fix_point_mult3_32, sh, a, b, c, 0);
}
static u32 h_mult2_32(u32 sh, u32 a, u32 b)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))fix_point_mult2_32, sh, a, b, 0, 0);
}
static t32_u64 h_mult3_64(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c)
{
	t32o_arg5 = (u32)(b >> 32);
	t32o_arg6 = (u32)c;
	return t32o_call((t32o_fn)(void (*)(void))fix_point_mult3_64, sh, 0, (u32)a, (u32)(a >> 32), (u32)b);
}
static t32_u64 h_div_64(u32 sh, t32_u64 a, t32_u64 b)
{
	t32o_arg5 = (u32)(b >> 32);
	t32o_arg6 = 0;
	return t32o_call((t32o_fn)(void (*)(void))fix_point_div_64, sh, 0, (u32)a, (u32)(a >> 32), (u32)b);
}
static t32_u64 h_bil(t32_u64 x, t32_u64 e0, t32_u64 e1, u32 y0, u32 y1)
{
	return tisp_ae_lib_bilinear_intp((u32)x, (u32)(x >> 32), (u32)e0, (u32)(e0 >> 32), (u32)e1,
					 (u32)(e1 >> 32), y0, y1);
}
static u32 h_log2(u32 v, u32 a, u32 b)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))tisp_log2_fixed_to_fixed, v, a, b, 0, 0);
}
static u32 h_rd(u32 ch, u32 reg)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))tisp_reg_read, ch, reg, 0, 0, 0);
}
static u32 h_mode(const u8 *st0)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))lib_tisp_api_g_runningMode, T32O_PTR(st0), 0, 0, 0, 0) & 0xff;
}
static u32 h_bv(u32 ch)
{
	return (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_get_bv, ch, 0, 0, 0, 0);
}
static void h_calc(u32 ch, t32_u64 v, u32 *a, u32 *b, u32 *c)
{
	u32 *cell = t32o_alloc(16);

	cell[0] = *a; cell[1] = *b; cell[2] = *c;
	t32o_arg5 = T32O_PTR(cell + 1);
	t32o_arg6 = T32O_PTR(cell + 2);
	t32o_call((t32o_fn)(void (*)(void))tisp_ae_short_ev_alloc_calc, ch, 0, (u32)v, (u32)(v >> 32),
		  T32O_PTR(cell));
	*a = cell[0]; *b = cell[1]; *c = cell[2];
}
static void h_it(u32 ch, int lg)
{
	t32o_call((t32o_fn)(void (*)(void))(lg ? tisp_ae_long_set_sensor_integration_time :
		  tisp_ae_short_set_sensor_integration_time), ch, 0, 0, 0, 0);
}
static void h_ag(u32 ch, int lg)
{
	t32o_call((t32o_fn)(void (*)(void))(lg ? tisp_set_ae_long_ag : tisp_set_ae_short_ag), ch, 0, 0, 0, 0);
}
static void h_event(u32 ch, const u32 *w)
{
	u32 *cell = t32o_alloc(32);

	memcpy(cell, w, 32);
	t32o_call((t32o_fn)(void (*)(void))tisp_event_push, ch, T32O_PTR(cell), 0, 0, 0);
}

static const struct t32_ae_ev_ops ops = {
	.mult3_32 = h_mult3_32, .mult2_32 = h_mult2_32, .mult3_64 = h_mult3_64, .div_64 = h_div_64,
	.bilinear = h_bil, .log2 = h_log2, .reg_read = h_rd, .running_mode = h_mode, .get_bv = h_bv,
	.short_calc = h_calc, .set_it = h_it, .set_ag = h_ag, .event = h_event,
};

struct snap {
	unsigned int nw, hooklog, nwl, nev;
	u32 w[MAXW][2], wl[64], evl[16][9], evn, evch, evw[8], evlog[T32O_EVLOG][9];
	u8 tune[TUNE_SZ], buf[BUF_SZ], s[2][ST_SZ];
};
static struct snap R, P;
static u8 timg[TUNE_SZ], bimg[BUF_SZ], simg[2][ST_SZ];
static u32 ch, kind, ddv, ctxflag;

struct mem { u8 *tune, *buf, *st[2]; u32 *ctx; };

static void setup(struct mem *m)
{
	m->tune = t32o_alloc(TUNE_SZ);
	m->buf = t32o_alloc(BUF_SZ);
	m->st[0] = t32o_alloc(ST_SZ);
	m->st[1] = t32o_alloc(ST_SZ);
	m->ctx = t32o_alloc(16);
	memcpy(m->tune, timg, TUNE_SZ);
	memcpy(m->buf, bimg, BUF_SZ);
	memcpy(m->st[0], simg[0], ST_SZ);
	memcpy(m->st[1], simg[1], ST_SZ);
	m->ctx[0] = T32O_PTR(m->tune);
	m->ctx[1] = T32O_PTR(m->buf);
	m->ctx[2] = ctxflag;
	m->ctx[3] = 0;
	t32_data_e15a8[ch] = T32O_PTR(m->ctx);
	t32_data_e1588[0] = T32O_PTR(m->st[0]);
	t32_data_e1588[1] = T32O_PTR(m->st[1]);
	t32_data_dd2d4[ch * 232 + 123] = (u8)ddv;
	t32o_rd_hook = rdh;
	nwl = 0;
	nev = 0;
	t32o_ev_n = 0;
	memset(t32o_evlog, 0, sizeof(t32o_evlog));
	t32o_ev_ch = 0;
	memset(t32o_ev_w, 0, sizeof(t32o_ev_w));
}

static void capture(struct snap *s, const struct mem *m)
{
	memcpy(s->tune, m->tune, TUNE_SZ);
	memcpy(s->buf, m->buf, BUF_SZ);
	memcpy(s->s[0], m->st[0], ST_SZ);
	memcpy(s->s[1], m->st[1], ST_SZ);
	s->nwl = nwl;
	memcpy(s->wl, wl, sizeof(wl));
	s->nev = nev;
	memcpy(s->evl, evl, sizeof(evl));
	s->evn = t32o_ev_n;
	s->evch = t32o_ev_ch;
	memcpy(s->evw, t32o_ev_w, sizeof(s->evw));
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
}

static void run_ref(void)
{
	struct mem m;
	unsigned int i;
	u64 (*fn)(u32, u32, u32, u32) = kind == 0 ? tisp_ae_short_expt :
		kind == 1 ? tisp_ae_short_ev_alloc : tisp_ae_long_ev_alloc;

	t32o_reset();
	setup(&m);
	t32o_call((t32o_fn)(void (*)(void))fn, ch, 0, 0, 0, 0);
	capture(&R, &m);
	R.nw = t32o_log.n;
	if (R.nw > MAXW) {
		printf("too many ref writes\n");
		exit(1);
	}
	for (i = 0; i < R.nw; i++) {
		R.w[i][0] = t32o_log.w[i].reg;
		R.w[i][1] = t32o_log.w[i].val;
	}
}

static void run_port(void)
{
	struct mem m;
	struct t32_stats_write ow[T32_STATS_MAX_WRITES];
	unsigned int n = 0, i;

	t32o_reset();
	setup(&m);
	if (kind == 0)
		t32_ae_short_expt(ch, m.tune, m.st[ch], &ops);
	else if (kind == 1)
		n = t32_ae_short_ev_alloc(ch, m.tune, m.buf, m.st[ch], m.st[0], t32_data_dd2d4, &ops, ow);
	else
		n = t32_ae_long_ev_alloc(ch, m.tune, m.buf, m.st[ch], m.st[0], t32_data_dd2d4, ctxflag, &ops, ow);
	capture(&P, &m);
	P.hooklog = t32o_log.n;
	P.nw = n;
	for (i = 0; i < n && i < MAXW; i++) {
		P.w[i][0] = ow[i].reg;
		P.w[i][1] = ow[i].val;
	}
}

static void fail(unsigned int c, const char *what)
{
	printf("MISMATCH case %u kind %u ch %u: %s (writes ref %u ours %u, hook writes %u, ev ref %u ours %u)\n",
	       c, kind, ch, what, R.nw, P.nw, P.hooklog, R.evn, P.evn);
	exit(1);
}

static void compare(unsigned int c)
{
	unsigned int i;

	if (P.hooklog)
		fail(c, "callee wrote registers in the port run");
	if (R.nw != P.nw)
		fail(c, "register write count");
	for (i = 0; i < R.nw; i++)
		if (R.w[i][0] != P.w[i][0] || R.w[i][1] != P.w[i][1])
			fail(c, "register write");
	if (memcmp(R.tune, P.tune, TUNE_SZ))
		fail(c, "tuning memory");
	if (memcmp(R.buf, P.buf, BUF_SZ))
		fail(c, "buffer memory");
	if (memcmp(R.s[0], P.s[0], ST_SZ) || memcmp(R.s[1], P.s[1], ST_SZ))
		fail(c, "state memory");
	if (R.nwl != P.nwl || memcmp(R.wl, P.wl, sizeof(wl)))
		fail(c, "setter calls");
	if (R.nev != P.nev || memcmp(R.evl, P.evl, sizeof(evl)))
		fail(c, "events (stub)");
	if (R.evn != P.evn || R.evch != P.evch || memcmp(R.evw, P.evw, sizeof(R.evw)) ||
	    memcmp(R.evlog, P.evlog, sizeof(R.evlog)))
		fail(c, "events (env)");
}

static void gen(void)
{
	u8 *stc;
	u32 i, sh, mode;
	t32_u64 x, cc;

	for (i = 0; i < TUNE_SZ; i++) timg[i] = (u8)rnd();
	for (i = 0; i < BUF_SZ; i++) bimg[i] = (u8)rnd();
	for (i = 0; i < ST_SZ; i++) simg[0][i] = (u8)rnd();
	for (i = 0; i < ST_SZ; i++) simg[1][i] = (u8)rnd();
	ch = rnd() & 1;
	stc = simg[ch];
	sh = rnd() % 12;
	t32_wr32(timg, 1736, sh | (rnd() & 0xffff) << 16);
	t32_wr32(timg, 1656, (rnd() << 16) | rnd() % 24);
	ddv = rnd() % 3 ? rnd() % 24 : rnd() & 0xff;
	ctxflag = rnd() & 1;
	gmode = rnd() % 3;
	rdseed = rnd();
	t32_wr32(stc, 576, 1 + rnd() % 0x10000);
	t32_wr32(stc, 580, 1 + rnd() % 0x400);
	t32_wr32(stc, 588, 1 + rnd() % 0x400);
	if (rnd() & 1) t32_wr16(bimg, 8568, 0);
	if (rnd() & 1) stc[1285] = 0; else stc[1285] = 1 + rnd() % 200;
	if (rnd() & 1) t32_wr16(timg, 1228, 0); else t32_wr16(timg, 1228, 1 + rnd() % 60000);
	if (rnd() & 1) t32_wr16(timg, 2956, 1);
	if (rnd() & 1) t32_wr32(bimg, 11784, 1);
	if (rnd() & 1) stc[1695] = 0; else stc[1695] = 1 + rnd() % 9;
	simg[0][2183] = (u8)(rnd() % 3);
	t32o_reset();
	x = h_mult3_64(sh, (t32_u64)t32_ld32(stc, 576) << sh, t32_ld32(stc, 580), t32_ld32(stc, 588));
	cc = x >> sh;
	mode = rnd() % 4;
	if (mode == 0) {
		t32_u64 v = rnd() % 1000;

		for (i = 15; i-- > 0;) {
			t32_wr64(timg, 128 + 8 * i, v);
			v += rnd() % 100000;
		}
	} else if (mode == 2) {
		/* ascending, x in the last bracket (13): reached only after 13 steps of the search */
		long long d = (long long)(cc >> 4) + 1 + rnd() % 4, val;

		for (i = 0; i < 15; i++) {
			val = (long long)cc + ((long long)i - 13) * d;
			t32_wr64(timg, 128 + 8 * i, val < 0 ? 0 : (t32_u64)val);
		}
	} else {
		long long d = (long long)(cc >> 3) + 1 + rnd() % 4, val;

		for (i = 0; i < 15; i++) {
			val = (long long)cc + (7 - (long long)i) * d;
			t32_wr64(timg, 128 + 8 * i, val < 0 ? 0 : (t32_u64)val);
		}
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 4500, c, cnt[3] = { 0 };

	for (c = 0; c < n; c++) {
		gen();
		kind = c % 3;
		run_ref();
		run_port();
		compare(c);
		cnt[kind]++;
	}
	printf("t32_aee_oracle: %u cases compared (short_expt %u, short_ev_alloc %u, long_ev_alloc %u), 0 mismatches\n",
	       n, cnt[0], cnt[1], cnt[2]);
	return 0;
}
