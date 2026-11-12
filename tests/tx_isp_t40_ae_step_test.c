/*
 * Host test of the T40 OEM AE exposure step + allocation (#410 / #412): driver/t40/tx_isp_t40_ae_target.h
 * t40_aet_tune2 (bit-exact with the OEM tisp_ae_long_tune2, tools/ko_emu/diff_ae_target.py case S) and
 * driver/t40/tx_isp_t40_ae_api.h t40_ae_oem_alloc + t40_ae_dg_ring_step (#412b OEM latency model), run every AE
 * frame like the driver on a plant model: statistics luma = min(clip, k x exposure); the sensor part of a step
 * reaches the statistics 3 frames later (OEM: written by the next frame-done worker, sensor apply delay 2), the ISP
 * dgain written at a step 1 frame later; sc830ai day bank words (lists 50..320000 / 72..40, speed
 * {154, 51, 4, 1024, 512}, tolerance {1150, 950, 1, 1}, mp48[2] 1, mp48[8] 2, mp48[10] 2 = ISP dgain ring depth,
 * antiflicker 50 Hz NORMAL: rung 450, max_it 2241, analog index 0..20, ISP dgain <= 4x).
 *   ring: t40_ae_dg_ring_step against a literal transcription of the OEM shift / refresh loops (0x38374).
 *   #410: a linear scene approached from a bright and from a dark history ends at the same exposure.
 *   #412: lamps on (clipped, 20x too bright) and lamps off: output Y (0.45 gamma) within 5 % about as fast as the
 *         OEM step alone on a continuous exposure, overshoot <= 3 %, no single wrong frame; the same plant with
 *         the ISP dgain at once, and with the pre-#412b driver (sensor write from the AE work landing 2 or 3
 *         frames later, one-frame dgain queue), must show single wrong frames (device .184: +12..14 %).
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../driver/t40/tx_isp_t40_ae_api.h"
#include "../driver/t40/tx_isp_t40_ae_target.h"

static u32 t_log2_gain(u32 i)
{
	static const u32 lut[] = {
		0U, 14995U, 31177U, 47704U, 65535U, 81158U, 97241U, 113239U, 131070U, 147006U, 162776U, 178774U,
		196605U, 212541U, 228311U, 244420U, 262140U, 278154U, 293912U, 309955U, 327675U, 343689U, 359480U,
		375518U, 393210U, 409243U,
	};

	return lut[i > 25U ? 25U : i];
}

static double t_exp2(double x)
{
	double r = 1.0;

	while (x >= 1.0) {
		r *= 2.0;
		x -= 1.0;
	}
	/* 2^x for 0 <= x < 1: 30 terms of the series are plenty */
	{
		double t = 1.0, s = 1.0, l = 0.69314718055994530942 * x;
		int n;

		for (n = 1; n < 30; n++) {
			t *= l / n;
			s += t;
		}
		r *= s;
	}
	return r;
}

/*
 * Plant: per-frame timeline of the exposure the statistics of frame f see.  A step at frame k (on the statistics of
 * frame k) reaches the sensor part from frame k + ls on (ls = 3 with the OEM timing: written by the next frame-done
 * worker, then the sensor's apply delay 2; the old open AE-work write landed at 2 or 3), the ISP dgain written at
 * step k from frame k + 1 on.
 */
enum { TL = 400 };
enum { DRV_OEM, DRV_OLD };
struct cam {
	u32 it, ag, dg;			/* decided */
	u32 eit[TL], eag[TL], edg[TL];	/* effective at frame f */
	u32 dgq, queued;		/* old driver: one-frame queue (t40_ae_dg_delay 1 before #412b) */
	struct t40_ae_dg_ring ring;
	int k, fill;
};

static int drv = DRV_OEM;
static int ring_depth = 2;		/* bank +0x288 u16[10] (sc830ai: 2) */
static unsigned int lcg = 1;

static int sensor_latency(void)
{
	if (drv == DRV_OEM)
		return 3;
	lcg = lcg * 1103515245U + 12345U;	/* old: I2C from the AE work before / after the frame latch */
	return 2 + ((lcg >> 16) & 1U);
}

static double cam_exposure(const struct cam *c, int f)
{
	return c->eit[f] * t_exp2(t_log2_gain(c->eag[f]) / 65536.0) * c->edg[f] / 1024.0;
}

static void cam_set(struct cam *c, u32 it, u32 ag, u32 dg)
{
	int i;

	memset(c, 0, sizeof(*c));
	c->it = it;
	c->ag = ag;
	c->dg = dg;
	for (i = 0; i < TL; i++) {
		c->eit[i] = it;
		c->eag[i] = ag;
		c->edg[i] = dg;
	}
	c->fill = 1;
}

static void cam_sensor(struct cam *c, int from)
{
	int i;

	for (i = from; i < TL; i++) {
		c->eit[i] = c->it;
		c->eag[i] = c->ag;
	}
}

static void cam_dgain(struct cam *c, int from, u32 w)
{
	int i;

	for (i = from; i < TL; i++)
		c->edg[i] = w;
}

static struct t40_aet_bank bank;
static u32 evl[T40_AET_N], tgl[T40_AET_N];

static void bank_init(void)
{
	static const u32 ev[T40_AET_N] = { 50, 200, 1500, 6000, 8500, 15000, 35000, 80000, 140000, 320000 };
	static const u16 tg[T40_AET_N] = { 72, 68, 64, 60, 58, 55, 50, 45, 44, 40 };
	static const u16 spd[5] = { 154, 51, 4, 1024, 512 }, stab[4] = { 1150, 950, 1, 1 };
	unsigned int i;

	memset(&bank, 0, sizeof(bank));
	for (i = 0; i < T40_AET_N; i++) {
		bank.ev[i] = ev[i];
		bank.tgt[i] = tg[i];
	}
	memcpy(bank.spd, spd, sizeof(spd));
	memcpy(bank.stab, stab, sizeof(stab));
	bank.mp48[1] = 10;
	bank.mp48[2] = 1;
	bank.mp48[7] = 2;
	bank.mp48[8] = 2;
	bank.mp48[10] = 2;
	bank.af[0] = 1;
	t40_aet_lists(&bank, 0, evl, tgl);
}

static const struct t40_ae_alloc_lim lim = {
	.it_floor = 450, .it_cap = 2241, .af_step = 450, .lo = 0, .hi = 20, .dgain_max = 4096,
};

static u32 q10_gain(u32 idx)
{
	return (u32)(1024.0 * t_exp2(t_log2_gain(idx) / 65536.0));
}

/* one AE frame: statistics of the effective exposure, OEM step on the decided EV, allocation, writes */
static u32 frame(struct cam *c, struct t40_aet_tune *st, double k, double clip, u32 *tgt_out)
{
	int f = c->k;
	double x = k * cam_exposure(c, f);
	u32 luma = x >= clip ? (u32)clip : (u32)(x + 0.5), band[3], ev0, ev1, r;

	assert(f + 8 < TL);
	if (!luma)
		luma = 1;
	/* bright-band share (pixels >= 230, Q10): a clipped frame is all bright */
	band[0] = luma >= 230U ? 1024U : luma > 150U ? (luma - 150U) * 1024U / 80U : 0U;
	band[1] = luma < 30U ? 1024U : 0U;
	ev0 = t40_aet_ev_q10(c->it, q10_gain(c->ag), c->dg);
	ev1 = t40_aet_tune2(&bank, evl, tgl, st, ev0, luma, band);
	st->refresh = 0;
	r = ev1 != ev0 ? t40_ae_oem_alloc(ev1, &c->it, &c->ag, &c->dg, &lim, t_log2_gain) : 0U;
	if (r & 1U)
		cam_sensor(c, f + sensor_latency());
	if (drv == DRV_OEM) {
		/* recovered.c: ring step + GIB write every AE frame */
		cam_dgain(c, f + 1, t40_ae_dg_ring_step(&c->ring, c->dg, (u32)ring_depth, c->fill));
		c->fill = 0;
	} else {
		int defer = (r & 2U) && (r & 1U);	/* the old dg_defer */

		if (c->queued) {
			c->queued = 0;
			if (!(r & 2U) || defer)
				cam_dgain(c, f + 1, c->dgq);
		}
		if (r & 2U) {
			if (defer) {
				c->dgq = c->dg;
				c->queued = 1;
			} else {
				cam_dgain(c, f + 1, c->dg);
			}
		}
	}
	assert(c->dg >= 1024U && c->dg <= 4096U && c->ag <= 20U && c->it >= 450U && c->it <= 2241U);
	assert(c->it == 2241U || c->it % 450U == 0U);
	if (tgt_out)
		*tgt_out = st->tgt;
	c->k++;
	return luma;
}

static void tune_init(struct t40_aet_tune *st)
{
	memset(st, 0, sizeof(*st));
	st->refresh = 1;
	st->refresh_t = 1;
}

static double ev_of(u32 it, u32 ag, u32 dg)
{
	return it * t_exp2(t_log2_gain(ag) / 65536.0) * dg / 1024.0;
}

/* OEM allocation order: IT rung (<= EV / min gain), analog index, ISP dgain remainder; manual stages stay */
static void test_alloc(void)
{
	struct t40_ae_alloc_lim l = lim;
	u32 it = 2241, ag = 5, dg = 2000, r;

	/* below the ceiling: IT = rung floor of the EV, the rest in the ISP dgain (< one analog step) */
	r = t40_ae_oem_alloc(1000U * 1024U, &it, &ag, &dg, &l, t_log2_gain);
	assert(r == 3U && it == 900U && ag == 0U && dg > 1024U && dg < 1200U);
	assert(fabs(ev_of(it, ag, dg) / 1000.0 - 1.0) < 0.002);
	/* below one rung (NORMAL): IT stays on the first rung at gain 1x */
	r = t40_ae_oem_alloc(300U * 1024U, &it, &ag, &dg, &l, t_log2_gain);
	assert(it == 450U && ag == 0U && dg == 1024U);
	/* above the ceiling: IT 2241, analog index 2 (1.39x), dgain 1.5 / 1.39 */
	r = t40_ae_oem_alloc((u32)(2241.0 * 1.5 * 1024.0), &it, &ag, &dg, &l, t_log2_gain);
	assert(it == 2241U && ag == 2U && fabs(ev_of(it, ag, dg) / (2241.0 * 1.5) - 1.0) < 0.002);
	/* nothing changes on the same EV */
	assert(t40_ae_oem_alloc((u32)(ev_of(it, ag, dg) * 1024.0), &it, &ag, &dg, &l, t_log2_gain) == 0U ||
	       fabs(ev_of(it, ag, dg) / (2241.0 * 1.5) - 1.0) < 0.002);
	/* beyond everything: analog maximum, ISP dgain at its cap */
	(void)t40_ae_oem_alloc(0xffffffffU, &it, &ag, &dg, &l, t_log2_gain);
	assert(it == 2241U && ag == 20U && dg == 4096U);
	/* no antiflicker (AUTO / off): continuous IT down to the floor */
	l.af_step = 0;
	l.it_floor = 8;
	(void)t40_ae_oem_alloc(1000U * 1024U, &it, &ag, &dg, &l, t_log2_gain);
	assert(it == 1000U && ag == 0U && dg == 1024U);
	(void)t40_ae_oem_alloc(4U * 1024U, &it, &ag, &dg, &l, t_log2_gain);
	assert(it == 8U && dg == 1024U);
	/* manual IT: the gains take the EV */
	l = lim;
	l.it_fixed = true;
	it = 1350;
	(void)t40_ae_oem_alloc(2700U * 1024U, &it, &ag, &dg, &l, t_log2_gain);
	assert(it == 1350U && ag == 4U && fabs(ev_of(it, ag, dg) / 2700.0 - 1.0) < 0.002);
	/* manual analog gain: IT + ISP dgain */
	l = lim;
	l.ag_fixed = true;
	l.lo = l.hi = 8;
	ag = 8;
	(void)t40_ae_oem_alloc(2241U * 5U * 1024U, &it, &ag, &dg, &l, t_log2_gain);
	assert(ag == 8U && it == 2241U && dg >= 1270U && dg <= 1290U);	/* 5 / 3.97 = 1.26 */
	(void)t40_ae_oem_alloc(2000U * 4U * 1024U, &it, &ag, &dg, &l, t_log2_gain);
	assert(ag == 8U && it == 1800U && dg >= 1024U);
	puts("alloc ok");
}

/* #410: same scene, bright and dark history -> same end exposure */
static void test_path(double ev_eq)
{
	static const double hist[2] = { 2.5, 0.4 };
	double k = 60.0 / ev_eq, ev_end[2];
	int h;

	for (h = 0; h < 2; h++) {
		struct cam c;
		struct t40_aet_tune st;
		u32 it = 2241, ag = 0, dg = 1024, l = 0, t = 0;
		double want = ev_eq * hist[h];
		int f;

		/* start allocation for the history exposure */
		(void)t40_ae_oem_alloc((u32)(want * 1024.0), &it, &ag, &dg, &lim, t_log2_gain);
		cam_set(&c, it, ag, dg);
		tune_init(&st);
		for (f = 0; f < 300; f++)
			l = frame(&c, &st, k, 255.0, &t);
		ev_end[h] = cam_exposure(&c, c.k);
		printf("path %s: luma %u target %u stable %u EV %.0f (it %u ag %u dg %u)\n", h ? "dark" : "bright", l, t,
		       st.stable, ev_end[h], c.it, c.ag, c.dg);
		assert(st.stable == 1U && l + 1U >= t && l <= t + 1U);
	}
	/*
	 * Both inside the OEM tolerance (+-1 count) of their target; the OEM itself keeps that much path dependence
	 * (2 counts + 1 target step at ~60 = 5 %; vendor .184 3000 K: 4664 / 4806 = 3 %).  The old ladder: 14 %.
	 */
	assert(ev_end[0] / ev_end[1] < 1.06 && ev_end[1] / ev_end[0] < 1.06);
	puts("path ok");
}

/*
 * Settle frame (last frame outside +-5 % of the end value, + 1) and overshoot beyond the end value, measured like
 * the light box (aeresp) on the output Y: statistics luma through a 0.45 gamma.
 */
static double out_y(u32 l)
{
	return 255.0 * pow(l / 255.0, 0.45);
}

static void step_eval(const u32 *l, int n, double start_luma, int *settle, double *over)
{
	double yf = out_y(l[n - 1]);
	int f;

	for (f = n - 1; f >= 0; f--)
		if (out_y(l[f]) > yf * 1.05 || out_y(l[f]) < yf * 0.95)
			break;
	*settle = f + 1;
	*over = 0.0;
	for (f = 0; f < n; f++) {
		double d = start_luma > l[n - 1] ? yf - out_y(l[f]) : out_y(l[f]) - yf;

		if (d / yf > *over)
			*over = d / yf;
	}
}

/* single frames against both neighbours (> 3 % in output Y, same side): the #412 "bright frame at an analog step" */
static int spikes(const u32 *l, int n)
{
	int f, c = 0;

	for (f = 1; f + 1 < n; f++) {
		double y = out_y(l[f]), a = out_y(l[f - 1]), b = out_y(l[f + 1]);

		if ((y > a * 1.03 && y > b * 1.03) || (y < a / 1.03 && y < b / 1.03))
			c++;
	}
	return c;
}

/*
 * #412: light step with a clipped start.  Reference = the OEM step alone on a continuous exposure with the same
 * latency (3 frames, what the vendor does, its allocation is continuous); the driver loop (allocation on the 1.17x
 * analog ladder + ISP dgain) must settle as fast (+strict frames: 3 falling; 8 rising, where the last per cent is a
 * one-count creep of the quantised allocation), overshoot <= 3 % and show no single wrong frame.  strict 0 = report
 * only.  Returns the single-frame count.
 */
static int step_run(const char *name, double ev_start, double k, int max_frames, int strict)
{
	enum { N = 200 };
	static struct cam c;
	struct t40_aet_tune st, rs;
	u32 it = 2241, ag = 0, dg = 1024, l[N], rl[N], t = 0, rev[N + 3];
	int f, settle, rsettle, sp;
	double over, rover, x;

	(void)t40_ae_oem_alloc((u32)(ev_start * 1024.0), &it, &ag, &dg, &lim, t_log2_gain);
	cam_set(&c, it, ag, dg);
	tune_init(&st);
	tune_init(&rs);
	rev[0] = rev[1] = rev[2] = t40_aet_ev_q10(it, q10_gain(ag), dg);
	for (f = 0; f < N; f++) {
		u32 band[3];

		l[f] = frame(&c, &st, k, 255.0, &t);
		x = k * rev[f] / 1024.0;
		rl[f] = x >= 255.0 ? 255U : x < 1.0 ? 1U : (u32)(x + 0.5);
		band[0] = rl[f] >= 230U ? 1024U : rl[f] > 150U ? (rl[f] - 150U) * 1024U / 80U : 0U;
		band[1] = rl[f] < 30U ? 1024U : 0U;
		rev[f + 3] = t40_aet_tune2(&bank, evl, tgl, &rs, rev[f + 2], rl[f], band);
		rs.refresh = 0;
	}
	step_eval(l, N, k * ev_start, &settle, &over);
	step_eval(rl, N, k * ev_start, &rsettle, &rover);
	sp = spikes(l, N);
	printf("%s: end luma %u (target %u, stable %u), within 5 %% after %d frames (OEM reference %d), "
	       "overshoot %.1f %% (reference %.1f %%), single wrong frames %d (reference %d)\n", name, l[N - 1], t,
	       st.stable, settle, rsettle, over * 100.0, rover * 100.0, sp, spikes(rl, N));
	if (getenv("AE_STEP_TRACE"))
		for (f = 0; f < 80; f++)
			printf("  %3d %3u %3u\n", f, l[f], rl[f]);
	assert(l[N - 1] + 2U >= t && l[N - 1] <= t + 2U);
	if (strict) {
		assert(settle <= rsettle + strict && settle <= max_frames);
		assert(over <= 0.03);
		assert(sp == 0);
	}
	return sp;
}

/*
 * The ring against a literal transcription of the OEM loops (tisp_ae_main_long_ev_alloc 0x38374: shift
 * `for (i = d; i + 1 > 0; i--) r[i + 1] = r[i]; r[0] = new;`, refresh fill `r[i + 1] = r[0]` for i = d..0, write
 * r[d]) on random sequences, depth 0..8.
 */
static void test_ring(void)
{
	unsigned int x = 7;
	int d, n, i;

	for (d = 0; d <= 8; d++) {
		struct t40_ae_dg_ring r;
		u32 o[T40_AE_DG_RING_N];

		memset(&r, 0, sizeof(r));
		memset(o, 0, sizeof(o));
		for (n = 0; n < 500; n++) {
			u32 v, w;
			int fill;

			x = x * 1103515245U + 12345U;
			v = 1024U + ((x >> 8) & 4095U);
			fill = ((x >> 4) & 31U) == 0U;
			for (i = d; i + 1 > 0; i--)
				o[i + 1] = o[i];
			o[0] = v;
			if (fill)
				for (i = d; i + 1 > 0; i--)
					o[i + 1] = o[0];
			w = t40_ae_dg_ring_step(&r, v, (u32)d, fill);
			assert(w == o[d]);
			assert(memcmp(r.v, o, sizeof(o)) == 0);
		}
	}
	/* depth beyond the 10-word ring is clamped to 8 */
	{
		struct t40_ae_dg_ring r;

		memset(&r, 0, sizeof(r));
		for (n = 0; n < 9; n++)
			(void)t40_ae_dg_ring_step(&r, 1024U + (u32)n, 99U, false);
		assert(t40_ae_dg_ring_step(&r, 2000U, 99U, false) == 1025U);
	}
	puts("ring ok");
}

int main(void)
{
	int sp;

	setvbuf(stdout, NULL, _IONBF, 0);
	bank_init();
	test_ring();
	test_alloc();
	test_path(4700.0);	/* IT at the ceiling, analog gain + ISP dgain (Eufy comp 128) */
	test_path(1500.0);	/* below the ceiling: IT rungs (Eufy comp 40: vendor IT 1500) */
	/* lamps on: dark state 2241 lines x 32x x 1.2 -> scene needs ~1/20 of it (frames clipped at the start) */
	puts("OEM timing (sensor at the next frame-done, +2), ISP dgain ring depth 2 (bank word):");
	drv = DRV_OEM;
	ring_depth = bank.mp48[10];
	step_run("lamps on", 2241.0 * 32.0 * 1.2, 60.0 / (2241.0 * 32.0 * 1.2 / 20.0), 60, 3);
	/* lamps off: from ~1/8 of the needed exposure */
	step_run("lamps off", 4000.0, 60.0 / 32000.0, 80, 8);
	/* the model must see the defects it removes */
	puts("OEM timing, ISP dgain at once (depth 0):");
	ring_depth = 0;
	sp = step_run("lamps on", 2241.0 * 32.0 * 1.2, 60.0 / (2241.0 * 32.0 * 1.2 / 20.0), 60, 0);
	assert(sp > 0);
	puts("before #412b: sensor from the AE work (2 or 3 frames), dgain one-frame queue:");
	drv = DRV_OLD;
	sp = step_run("lamps on", 2241.0 * 32.0 * 1.2, 60.0 / (2241.0 * 32.0 * 1.2 / 20.0), 60, 0);
	assert(sp > 0);
	puts("tx_isp_t40_ae_step tests passed");
	return 0;
}
