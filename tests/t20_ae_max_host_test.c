/* Host test: T20/T10 reachable analog gain cap and ISP digital gain fill
 * (tx_isp_t20_ae_max.h).
 * The isp-m0 "MAX SENSOR analog gain" must be min(sensor max, SetMaxAgain
 * ceiling), the same limit sensor_drv.c clamps the AE to. */
#include <stdio.h>
#include <stdint.h>

#include <stdlib.h>
#include <math.h>

#define _LINUX_TYPES_H
#include "../driver/t20/tx_isp_t20_ae_max.h"
#include "../driver/include/tx_isp/tx_isp_math.h"

static int32_t lg(uint32_t v)
{
	return tx_isp_log2_fixed_u32(v ? v : 1, 0, 16);
}

/*
 * #252 closed loop: a linear scene (mean = k * 2^exposure, clipped at 255),
 * the exposure applied with a 2-frame sensor latency, an update every
 * T20_AE_UPDATE_FRAMES frames (cooldown 2) as AE_fsm_process_interrupt().
 * linear = 1: the old step |target - mean| * 4096 / K.
 * Returns the frames until the mean stays within max(2 %, 1 LSB) of the
 * target for 30 frames, or -1;
 * *over = the worst overshoot in % of the target.
 */
static int settle_frames(double target, double start_ratio, int linear,
			 double *over)
{
	int32_t exp_log2 = 0, applied[3] = { 0, 0, 0 };
	double k = target * start_ratio, worst = 0;
	int f, cooldown = 0, inside = 0, first = -1;
	int above_start = start_ratio > 1.0;

	for (f = 0; f < 2000; f++) {
		double m = k * pow(2.0, applied[0] / 65536.0);
		/* the OEM/compact mean is an integer (div64, floored) */
		uint32_t mean = m > 255 ? 255 : (uint32_t)m;
		uint32_t t = (uint32_t)(target + 0.5);

		if (cooldown == 0) {
			if (linear) {
				int32_t e = (int32_t)t - (int32_t)mean;

				if (e > 1 || e < -1) {
					int32_t c = (abs(e) * 4096 + 9) / 10;

					exp_log2 += e > 0 ? c : -c;
				}
			} else {
				exp_log2 += t20_ae_log2_correction(
					lg(t) - lg(mean), 10);
			}
			cooldown = T20_AE_UPDATE_FRAMES - 1;
		} else {
			cooldown--;
		}
		applied[0] = applied[1];
		applied[1] = applied[2];
		applied[2] = exp_log2;
		if (above_start ? m < target : m > target) {
			double o = fabs(m - target) * 100.0 / target;

			if (o > worst)
				worst = o;
		}
		/* 2 % or the 1-LSB quantisation of the 8-bit mean */
		if (fabs(m - target) <= (target * 0.02 > 1.0 ?
					  target * 0.02 : 1.0)) {
			if (!inside++)
				first = f;
			if (inside >= 30)
				break;
		} else {
			inside = 0;
			first = -1;
		}
	}
	*over = worst;
	return inside >= 30 ? first : -1;
}

/* OEM 3.12.0 cmos_alloc_sensor_analog_gain() (tx_isp_t20_firmware.c):
 * max = stab[29] << 11; if (max >= request) max = request; -> unconditional
 * min(stab[29] << 11, request), stab[29] == 0 gives 1x (#238). */
static int32_t oem_alloc_request(uint32_t stab29, int32_t request)
{
	int32_t max_gain = (int32_t)stab29 << 11;

	if (max_gain >= request)
		max_gain = request;
	return max_gain;
}

/* The compact AE (sensor_drv.c allocate_gain): sensor clamp, then the
 * SetMaxAgain ceiling, unconditionally since #238. */
static int32_t compact_request(int32_t want, uint32_t sensor_max,
			       uint32_t stab29)
{
	int32_t r = want < 0 ? 0 : want;

	if (r > (int32_t)sensor_max)
		r = (int32_t)sensor_max;
	if (r > (int32_t)stab29 << 11)
		r = (int32_t)stab29 << 11;
	return r;
}

int main(void)
{
	int bad = 0;
	const uint32_t jxf = 158u << 11;	/* jxf22/jxf23 attr->max_again */

	bad += t20_ae_max_again_log2_5(jxf, 128) != 128; /* Wyze/Pan: tuning ceiling */
	bad += t20_ae_max_again_log2_5(jxf, 0) != 0;     /* #238: 0 = 1x, not "none" */
	bad += t20_ae_max_again_log2_5(jxf, 160) != 158; /* SetMaxAgain above sensor */
	bad += t20_ae_max_again_log2_5(jxf, 158) != 158;
	bad += t20_ae_max_again_log2_5(jxf, 1) != 1;
	bad += t20_ae_max_again_log2_5((158u << 11) | 0x7ff, 200) != 158; /* fraction cut */
	bad += t20_ae_max_again_log2_5(0, 128) != 0;
	/* stepped sensor tables: the AE plateaus on the step below the cap */
	bad += t20_ae_plateau_log2_5(144, 292253) != 142; /* jxh42 0x46 */
	bad += t20_ae_plateau_log2_5(128, 262144) != 128; /* exact step */
	bad += t20_ae_plateau_log2_5(128, 0) != 128;      /* unknown */
	bad += t20_ae_plateau_log2_5(128, 324678) != 128; /* never above cap */
	bad += t20_ae_request_log2_16(324678, 144) != (144u << 11);
	bad += t20_ae_request_log2_16(324678, 200) != 324678; /* sensor max exact */
	bad += t20_ae_request_log2_16(324678, 0) != 0;      /* #238: 0 = 1x */
	/* #238 device sweep values on jxf23 (sensor max 158): cap, AE request
	 * at the limit and the OEM allocator agree for every ceiling */
	{
		static const uint32_t sweep[] = { 0, 32, 64, 96, 128, 160, 255 };
		static const int32_t wants[] = { -5, 0, 1 << 16, 100 << 11,
						 158 << 11, 400 << 11 };
		unsigned i, j;

		for (i = 0; i < sizeof(sweep) / sizeof(sweep[0]); i++) {
			uint32_t u = sweep[i], exp = u < 158 ? u : 158;

			bad += t20_ae_max_again_log2_5(jxf, u) != exp;
			bad += t20_ae_request_log2_16(jxf, u) != (exp << 11);
			/* the PI ceiling (IT + this) can be reached exactly */
			bad += compact_request((int32_t)t20_ae_request_log2_16(
				jxf, u), jxf, u) !=
				(int32_t)t20_ae_request_log2_16(jxf, u);
			for (j = 0; j < sizeof(wants) / sizeof(wants[0]); j++) {
				int32_t w = wants[j];
				int32_t s = w < 0 ? 0 : (w > (int32_t)jxf ?
							 (int32_t)jxf : w);

				/* same result as the vendor allocator fed with
				 * the sensor-clamped request */
				bad += compact_request(w, jxf, u) !=
					oem_alloc_request(u, s);
				/* never above the published cap */
				bad += compact_request(w, jxf, u) >
					(int32_t)(t20_ae_max_again_log2_5(jxf,
						u) << 11);
			}
		}
		bad += compact_request(158 << 11, jxf, 0) != 0; /* max_again 0 */
		bad += compact_request(158 << 11, jxf, 32) != (32 << 11);
		bad += compact_request(158 << 11, jxf, 160) != (int32_t)jxf;
		bad += t20_ae_plateau_log2_5(0, 0) != 0; /* alloc(0) = 1x */
	}
	bad += t20_ae_plateau_log2_5(158, 324678) != 158; /* top step, not 157 */
	/* #128 manual integration time: table value, clamped to the window */
	bad += t20_ae_manual_lines(0, 30, 2, 2246) != 0;     /* AUTO: AE owns it */
	bad += t20_ae_manual_lines(1, 30, 2, 2246) != 30;
	bad += t20_ae_manual_lines(1, 240, 2, 2246) != 240;
	bad += t20_ae_manual_lines(1, 0, 2, 2246) != 2;      /* below sensor min */
	bad += t20_ae_manual_lines(1, 5000, 2, 2246) != 2246; /* above ceiling */
	/* total exposure stays in [time, time + gain cap] (no PI wind-up) */
	bad += t20_ae_manual_total(100, 200, 50) != 200;
	bad += t20_ae_manual_total(300, 200, 50) != 250;
	bad += t20_ae_manual_total(220, 200, 50) != 220;
	bad += t20_ae_manual_total(-1, 200, 0) != 200;
	/* #246 ISP digital gain fill, OEM cmos_digital_gain_update() +
	 * cmos_alloc_isp_digital_gain(): remainder = total - IT - analog,
	 * negative -> 0, capped by min(stab[33] << 11, cmos+0x1e4). */
	{
		const int32_t hw = (4 << 16) - 23; /* log2(4095/4096) + 4 */
		const int32_t it = 8 << 16, ag = 37 << 11;

		/* floored LUT step: the fractional remainder is filled */
		bad += t20_ae_isp_dgain_fill(it + ag + 1000, it, ag, 0, 0, 64,
					     hw) != 1000;
		bad += t20_ae_isp_dgain_fill(it + ag, it, ag, 0, 0, 64, hw) != 0;
		bad += t20_ae_isp_dgain_fill(it + ag - 5, it, ag, 0, 0, 64,
					     hw) != 0; /* OEM bltz -> 0 */
		/* SetMaxDgain cap (stab[33], log2 <<5) */
		bad += t20_ae_isp_dgain_fill(it + ag + (40 << 11), it, ag, 0, 0,
					     13, hw) != (13 << 11);
		bad += t20_ae_isp_dgain_fill(it + ag + (40 << 11), it, ag, 0, 0,
					     0, hw) != 0; /* SetMaxDgain 0: none */
		/* 4.8 register maximum */
		bad += t20_ae_isp_dgain_fill(it + ag + (200 << 11), it, ag, 0, 0,
					     255, hw) != hw;
		/* manual ISP gain: stab[32] << 11, same cap */
		bad += t20_ae_isp_dgain_fill(it, it, ag, 1, 20, 64, hw) !=
			(20 << 11);
		bad += t20_ae_isp_dgain_fill(it, it, ag, 1, 20, 8, hw) !=
			(8 << 11);
		bad += t20_ae_isp_dgain_cap(64, hw) != (64 << 11);
		bad += t20_ae_isp_dgain_cap(255, hw) != hw;
		bad += t20_ae_isp_dgain_cap(0, hw) != 0;
		/* applied total == request whenever the cap allows it */
		{
			int32_t d;

			for (d = 0; d < (64 << 11); d += 977)
				bad += it + ag + t20_ae_isp_dgain_fill(it + ag + d,
					it, ag, 0, 0, 64, hw) != it + ag + d;
		}
	}
	/* #252 log2 AE step: K = AE_PI_COEFF 10 (jxf23.bin), x3 per update */
	bad += t20_ae_log2_correction(0, 10) != 0;
	bad += t20_ae_log2_correction(T20_AE_LOG2_DEADBAND, 10) != 0;
	bad += t20_ae_log2_correction(-T20_AE_LOG2_DEADBAND, 10) != 0;
	bad += t20_ae_log2_correction(T20_AE_LOG2_DEADBAND + 1, 10) !=
		(T20_AE_LOG2_DEADBAND + 1) * 3 / 10;
	bad += t20_ae_log2_correction(1 << 16, 10) != (3 << 16) / 10;
	bad += t20_ae_log2_correction(-(1 << 16), 10) != -((3 << 16) / 10);
	bad += t20_ae_log2_correction(8 << 16, 10) != T20_AE_LOG2_STEP_MAX;
	bad += t20_ae_log2_correction(-(8 << 16), 10) != -T20_AE_LOG2_STEP_MAX;
	bad += t20_ae_log2_correction(0x7fffffff, 10) != T20_AE_LOG2_STEP_MAX;
	bad += t20_ae_log2_correction(1 << 16, 0) != 0; /* malformed IQ */
	bad += !t20_ae_log2_stable(0) || t20_ae_log2_stable(513);
	/* the step depends on the ratio only, not on the level (OEM) */
	bad += t20_ae_log2_correction(lg(20) - lg(10), 10) !=
		t20_ae_log2_correction(lg(200) - lg(100), 10);
	/* deadband 1/128 stop: 80 vs 79 (-1.25 %) is corrected now */
	bad += t20_ae_log2_correction(lg(80) - lg(79), 10) <= 0;
	bad += t20_ae_log2_stable(lg(80) - lg(79));
	/* closed loop: metered targets of comp 40..200 (RCA table), 1 stop
	 * away in both directions; the log2 loop settles in about the same
	 * frames at every level and overshoots by <= 2 % */
	{
		static const double tg[] = { 10, 17, 34, 68, 136, 200 };
		unsigned i;
		int fmin = 1 << 30, fmax = 0;

		for (i = 0; i < sizeof(tg) / sizeof(tg[0]); i++) {
			double o_new, o_new2, o_old;
			int n = settle_frames(tg[i], 0.5, 0, &o_new);
			int n2 = settle_frames(tg[i], 2.0, 0, &o_new2);
			int old = settle_frames(tg[i], 0.5, 1, &o_old);

			printf("  target %5.1f: log2 %3d/%3d frames (over %.1f/%.1f %%), "
			       "linear %4d frames (over %.1f %%)\n", tg[i], n, n2,
			       o_new, o_new2, old, o_old);
			bad += n < 0 || n2 < 0 || n > 60 || n2 > 60;
			bad += o_new > 2.0 || o_new2 > 2.0;
			if (n < fmin)
				fmin = n;
			if (n > fmax)
				fmax = n;
			/* the old law was slower at the dark targets */
			if (tg[i] <= 17)
				bad += old >= 0 && old <= n;
		}
		bad += fmax > 2 * fmin + 6; /* level-independent bandwidth */
		/* a black frame (mean 0) is metered as 1: full stop, no hang */
		bad += t20_ae_log2_correction(lg(68) - lg(0), 10) !=
			T20_AE_LOG2_STEP_MAX;
	}
	printf("t20_ae_max_host_test: %s (%d bad)\n", bad ? "FAIL" : "PASS", bad);
	return bad != 0;
}
