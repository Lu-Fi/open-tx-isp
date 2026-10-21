/* Host test: T31 AE minimum EV step (tx_isp_t31_ae_step.h).
 * ae_ev_step[2] must act as a minimum step like the OEM (0x50a44..0x50b2c),
 * not as a clamp.  The convergence check models ae0_tune2 phase F with the
 * stock sc2336-t31.bin values (ev_step = [154, 51, 4, 1024, 512], q = 10,
 * 15 fps, 2 frames sensor delay, 15-entry weighted luma FIFO). */
#include <stdio.h>
#include <stdint.h>

#include "../driver/t31/tx_isp_t31_ae_step.h"

#define Q	10
#define ONE_Q2	((uint64_t)1 << (2 * Q))
#define STEP_Q2	((uint64_t)4 << Q)

static double step_ratio(double r)
{
	uint64_t x = (uint64_t)(r * ONE_Q2 + 0.5);

	return (double)t31_ae_min_step_q2(x, ONE_Q2, STEP_Q2) / ONE_Q2;
}

/* Frames until the measured luma stays within 5 % of the target. */
static int settle_frames(double ev0, double k, double target)
{
	const double dc = 154 / 1024.0, e0 = 51 / 1024.0, v01 = 512 / 1024.0;
	double ev = ev0, hist[3] = { ev0, ev0, ev0 }, fifo[15];
	int f, i, last_bad = -1;

	for (i = 0; i < 15; i++)
		fifo[i] = target;
	for (f = 0; f < 1800; f++) {
		double luma = k * hist[0], s6 = 0, w = 0, err, r;

		if (luma > 255)
			luma = 255;
		for (i = 1; i < 15; i++)
			fifo[i - 1] = fifo[i];
		fifo[14] = luma;
		for (i = 0; i < 15; i++) {
			s6 += (i + 1) * fifo[i];
			w += i + 1;
		}
		s6 /= w;
		if (luma < target * 0.95 || luma > target * 1.05)
			last_bad = f;
		if (s6 < target - 1 || s6 > target + 1) {
			if (s6 >= target) {
				err = (s6 - target) / s6;
				r = err >= v01 ? 1 - dc * err :
						 1 - dc * err * err / (1 - err);
			} else {
				err = (target - s6) / s6;
				r = err <= 1 ? 1 + e0 * err * err : 1 + e0 * err;
			}
			ev *= step_ratio(r);
		}
		hist[0] = hist[1];
		hist[1] = hist[2];
		hist[2] = ev;
	}
	return last_bad + 1;
}

int main(void)
{
	int bad = 0;
	const uint64_t up = ONE_Q2 + STEP_Q2, lo = ONE_Q2 - STEP_Q2;

	/* small steps are pushed out to the minimum step */
	bad += t31_ae_min_step_q2(ONE_Q2 + 1, ONE_Q2, STEP_Q2) != up;
	bad += t31_ae_min_step_q2(ONE_Q2 - 1, ONE_Q2, STEP_Q2) != lo;
	bad += t31_ae_min_step_q2(up - 1, ONE_Q2, STEP_Q2) != up;
	bad += t31_ae_min_step_q2(lo + 1, ONE_Q2, STEP_Q2) != lo;
	/* unity and the bounds themselves pass unchanged */
	bad += t31_ae_min_step_q2(ONE_Q2, ONE_Q2, STEP_Q2) != ONE_Q2;
	bad += t31_ae_min_step_q2(up, ONE_Q2, STEP_Q2) != up;
	bad += t31_ae_min_step_q2(lo, ONE_Q2, STEP_Q2) != lo;
	/* large steps are not clamped (the old bug: 0.4 % per frame) */
	bad += t31_ae_min_step_q2(ONE_Q2 / 2, ONE_Q2, STEP_Q2) != ONE_Q2 / 2;
	bad += t31_ae_min_step_q2(ONE_Q2 * 3, ONE_Q2, STEP_Q2) != ONE_Q2 * 3;

	/* convergence at 15 fps, target luma 60 (sc2336 at_list) */
	{
		int down = settle_frames(4055, 2.77 * 60 / 4055, 60);	/* .25 log 10-21 */
		int up_ = settle_frames(1465, 60 / 3159.0, 60);
		int door = settle_frames(4055, 10 * 60 / 4055.0, 60);	/* luma saturates */

		printf("t31_ae_step_host_test: settle down %.1f s, up %.1f s, x10 %.1f s\n",
		       down / 15.0, up_ / 15.0, door / 15.0);
		bad += down > 6 * 15;
		bad += up_ > 6 * 15;
		bad += door > 8 * 15;
	}
	printf("t31_ae_step_host_test: %s (%d bad)\n", bad ? "FAIL" : "PASS", bad);
	return bad != 0;
}
