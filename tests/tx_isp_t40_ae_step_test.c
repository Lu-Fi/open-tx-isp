/*
 * Host test of the T40 OEM AE exposure step + allocation (#410 / #412): driver/t40/tx_isp_t40_ae_target.h
 * t40_aet_tune2 (bit-exact with the OEM tisp_ae_long_tune2, tools/ko_emu/diff_ae_target.py case S) and
 * driver/t40/tx_isp_t40_ae_api.h t40_ae_oem_alloc, run every AE frame like the driver on a plant model:
 * statistics luma = min(clip, k x exposure), sensor exposure (IT, analog gain) 2 frames late, ISP dgain 1 frame
 * late; sc830ai day bank words (lists 50..320000 / 72..40, speed {154, 51, 4, 1024, 512}, tolerance
 * {1150, 950, 1, 1}, mp48[2] 1, mp48[8] 2, antiflicker 50 Hz NORMAL: rung 450, max_it 2241, analog index 0..20,
 * ISP dgain <= 4x).
 *   #410: a linear scene approached from a bright and from a dark history ends at the same exposure (the old
 *         ladder: 14 % apart, the fall stopped one analog index above the end value at ISP dgain 1x).
 *   #412: lamps on (clipped, 20x too bright) and lamps off: output Y (0.45 gamma) within 5 % as fast as the OEM
 *         step alone on a continuous exposure (+3 frames; 20x: 43 frames, 2.2 s at 20 fps), overshoot <= 3 %;
 *         with the ISP dgain one frame ahead of the sensor <= +10 frames
 *         (the old ladder on .184: 12.5 %, 3.65 s; vendor 2.2 %, 1.75 s).
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

struct cam {
	u32 it, ag, dg;			/* commanded */
	u32 hit[3], hag[3], hdg[3];	/* applied history: [0] = this frame */
};

static double cam_exposure(const struct cam *c, int delay_s, int delay_d)
{
	return c->hit[delay_s] * t_exp2(t_log2_gain(c->hag[delay_s]) / 65536.0) * c->hdg[delay_d] / 1024.0;
}

static void cam_push(struct cam *c)
{
	int i;

	for (i = 2; i > 0; i--) {
		c->hit[i] = c->hit[i - 1];
		c->hag[i] = c->hag[i - 1];
		c->hdg[i] = c->hdg[i - 1];
	}
	c->hit[0] = c->it;
	c->hag[0] = c->ag;
	c->hdg[0] = c->dg;
}

static void cam_set(struct cam *c, u32 it, u32 ag, u32 dg)
{
	int i;

	c->it = it;
	c->ag = ag;
	c->dg = dg;
	for (i = 0; i < 3; i++) {
		c->hit[i] = it;
		c->hag[i] = ag;
		c->hdg[i] = dg;
	}
}

static struct t40_aet_bank bank;
static int dg_delay = 1;	/* ISP dgain latency (frames); the sensor part: 2 */
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

/* one AE frame: statistics of the applied exposure, OEM step on the commanded EV, allocation */
static u32 frame(struct cam *c, struct t40_aet_tune *st, double k, double clip, u32 *tgt_out)
{
	double x = k * cam_exposure(c, 2, dg_delay);
	u32 luma = x >= clip ? (u32)clip : (u32)(x + 0.5), band[3], ev0, ev1;

	if (!luma)
		luma = 1;
	/* bright-band share (pixels >= 230, Q10): a clipped frame is all bright */
	band[0] = luma >= 230U ? 1024U : luma > 150U ? (luma - 150U) * 1024U / 80U : 0U;
	band[1] = luma < 30U ? 1024U : 0U;
	ev0 = t40_aet_ev_q10(c->it, q10_gain(c->ag), c->dg);
	ev1 = t40_aet_tune2(&bank, evl, tgl, st, ev0, luma, band);
	st->refresh = 0;
	if (ev1 != ev0)
		(void)t40_ae_oem_alloc(ev1, &c->it, &c->ag, &c->dg, &lim, t_log2_gain);
	assert(c->dg >= 1024U && c->dg <= 4096U && c->ag <= 20U && c->it >= 450U && c->it <= 2241U);
	assert(c->it == 2241U || c->it % 450U == 0U);
	if (tgt_out)
		*tgt_out = st->tgt;
	cam_push(c);
	return luma;
}

static void tune_init(struct t40_aet_tune *st)
{
	memset(st, 0, sizeof(*st));
	st->refresh = 1;
	st->refresh_t = 1;
}

/* #410: same scene, bright and dark history -> same end exposure */
static void test_path(void)
{
	static const double hist[2] = { 2.5, 0.4 };
	double k = 60.0 / 4700.0, ev_end[2];
	int h;

	for (h = 0; h < 2; h++) {
		struct cam c;
		struct t40_aet_tune st;
		u32 it = 2241, ag = 0, dg = 1024, l = 0, t = 0;
		double want = 4700.0 * hist[h];
		int f;

		/* start allocation for the history exposure */
		(void)t40_ae_oem_alloc((u32)(want * 1024.0), &it, &ag, &dg, &lim, t_log2_gain);
		cam_set(&c, it, ag, dg);
		tune_init(&st);
		for (f = 0; f < 300; f++)
			l = frame(&c, &st, k, 255.0, &t);
		ev_end[h] = cam_exposure(&c, 0, 0);
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

/*
 * #412: light step with a clipped start.  Reference = the OEM step alone on a continuous exposure with the same
 * 2-frame latency (what the vendor does, its allocation is continuous); the driver loop (allocation on the 1.17x
 * analog ladder + ISP dgain, sensor 2 / ISP 1 frame late) must settle as fast (+-3 frames) and overshoot <= 3 %.
 */
static void step_run(const char *name, double ev_start, double k, int max_frames)
{
	enum { N = 200 };
	struct cam c;
	struct t40_aet_tune st, rs;
	u32 it = 2241, ag = 0, dg = 1024, l[N], rl[N], t = 0, rev[N + 2];
	int f, settle, rsettle;
	double over, rover, x;

	(void)t40_ae_oem_alloc((u32)(ev_start * 1024.0), &it, &ag, &dg, &lim, t_log2_gain);
	cam_set(&c, it, ag, dg);
	tune_init(&st);
	tune_init(&rs);
	rev[0] = rev[1] = t40_aet_ev_q10(it, q10_gain(ag), dg);
	for (f = 0; f < N; f++) {
		u32 band[3];

		l[f] = frame(&c, &st, k, 255.0, &t);
		x = k * rev[f] / 1024.0;
		rl[f] = x >= 255.0 ? 255U : x < 1.0 ? 1U : (u32)(x + 0.5);
		band[0] = rl[f] >= 230U ? 1024U : rl[f] > 150U ? (rl[f] - 150U) * 1024U / 80U : 0U;
		band[1] = rl[f] < 30U ? 1024U : 0U;
		rev[f + 2] = t40_aet_tune2(&bank, evl, tgl, &rs, rev[f + 1], rl[f], band);
		rs.refresh = 0;
	}
	step_eval(l, N, k * ev_start, &settle, &over);
	step_eval(rl, N, k * ev_start, &rsettle, &rover);
	printf("%s: end luma %u (target %u, stable %u), within 5 %% after %d frames (OEM reference %d), "
	       "overshoot %.1f %% (reference %.1f %%)\n", name, l[N - 1], t, st.stable, settle, rsettle,
	       over * 100.0, rover * 100.0);
	if (getenv("AE_STEP_TRACE"))
		for (f = 0; f < 80; f++)
			printf("  %3d %3u %3u\n", f, l[f], rl[f]);
	assert(l[N - 1] + 2U >= t && l[N - 1] <= t + 2U);
	/*
	 * Same sensor / ISP latency: as fast as the OEM step itself.  ISP dgain one frame earlier than the sensor: a
	 * rung / index change that lowers the dgain shows one darker frame (the rising run loses a few frames).
	 */
	assert(settle <= rsettle + (dg_delay == 2 ? 3 : 10) && settle <= max_frames);
	assert(over <= 0.03);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	bank_init();
	test_path();
	/* lamps on: dark state 2241 lines x 32x x 1.2 -> scene needs ~1/20 of it (frames clipped at the start) */
	for (dg_delay = 2; dg_delay >= 1; dg_delay--) {
		printf("ISP dgain latency %d frame(s), sensor 2:\n", dg_delay);
		step_run("lamps on", 2241.0 * 32.0 * 1.2, 60.0 / (2241.0 * 32.0 * 1.2 / 20.0), 60);
		/* lamps off: from ~1/8 of the needed exposure */
		step_run("lamps off", 4000.0, 60.0 / 32000.0, 80);
	}
	puts("tx_isp_t40_ae_step tests passed");
	return 0;
}
