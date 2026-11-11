/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T20/T10 compact AE: the analog gain ceiling the AE can really reach.
 *
 * sensor_drv.c clamps the requested analog gain to the sensor maximum
 * (attr->max_again, log2 <<16) and to the SetMaxAgain ceiling
 * stab.global_max_sensor_analog_gain (8-bit, log2 <<5, i.e. the same value
 * <<11).  Like the OEM 3.12.0 cmos_alloc_sensor_analog_gain() the ceiling
 * always applies: 0 means 1x, not "no cap" (#238).  /proc/jz/isp/isp-m0 must publish the lower of the two in the
 * "MAX SENSOR analog gain" line (log2 <<5): streamers read it as the
 * exposure cap.  Before, the dump showed only the sensor maximum (158 on
 * jxf22/jxf23) while the AE stopped at the tuning ceiling (128), so timps
 * believed a cap of 7838 that the AE never reaches.
 */
#ifndef TX_ISP_T20_AE_MAX_H
#define TX_ISP_T20_AE_MAX_H

#include <linux/types.h>

/* sensor_max_log2_16: attr->max_again; user_max: stab[29] ceiling (log2
 * <<5, 0 = 1x as in the OEM cmos_alloc_sensor_analog_gain, #238).
 * Returns the reachable maximum in log2 <<5 units. */
static inline uint32_t t20_ae_max_again_log2_5(uint32_t sensor_max_log2_16,
					       uint32_t user_max)
{
	uint32_t m = sensor_max_log2_16 >> 11;

	return user_max < m ? user_max : m;
}

/* The gain the compact AE hands to the sensor allocator at its limit, in
 * log2 <<16 (same clamp order as sensor_drv.c: sensor max, then ceiling). */
static inline uint32_t t20_ae_request_log2_16(uint32_t sensor_max_log2_16,
					      uint32_t user_max)
{
	uint32_t u = user_max << 11;

	return u < sensor_max_log2_16 ? u : sensor_max_log2_16;
}

/* The sensor gain tables are stepped: alloc_again() rounds the request
 * DOWN to the last table entry, so the AE plateaus on that step, not on
 * the ceiling (jxh42: ceiling 144 -> 142.7 -> plateau 142).
 * applied_log2_16: what alloc_again(request) returned, 0 = unknown.
 * Returns the cap in log2 <<5 units the AE really sits at. */
static inline uint32_t t20_ae_plateau_log2_5(uint32_t cap_log2_5,
					     uint32_t applied_log2_16)
{
	uint32_t a = applied_log2_16 >> 11;

	return (applied_log2_16 && a < cap_log2_5) ? a : cap_log2_5;
}

/*
 * ISP digital gain fill (#246), as the OEM 3.12.0 cmos_digital_gain_update()
 * (libt20-firmware cmos_func.c.o 0x139c-0x1430) with
 * cmos_alloc_isp_digital_gain() (0x2ec-0x31c): the part of the requested
 * total exposure that integration time and the (stepped, floored) analog
 * gain do not cover goes to the ISP digital gain, capped by
 * min(stab.global_max_isp_digital_gain << 11, hw_max_log2_16).  hw_max is
 * cmos+0x1e4, which cmos_fsm_clear() (cmos_fsm.c.o 0x68-0x88) seeds with
 * log2(4095/4096) + 4 = the 4.8 register maximum 0xfff.  A manual ISP gain
 * (stab.global_manual_isp_digital_gain, cmos_get_manual_isp_dgain_log2()
 * 0x378) replaces the remainder with stab.global_isp_digital_gain << 11.
 * Negative requests give 0 (OEM bltz at 0x2ec).  All values log2 <<16.
 */
static inline int32_t t20_ae_isp_dgain_cap(uint8_t max_isp_dgain_q5,
					   int32_t hw_max_log2_16)
{
	int32_t limit = (int32_t)max_isp_dgain_q5 << 11;

	return hw_max_log2_16 < limit ? hw_max_log2_16 : limit;
}

static inline int32_t t20_ae_isp_dgain_fill(int32_t total_log2,
					    int32_t integration_log2,
					    int32_t applied_again_log2,
					    uint8_t manual, uint8_t isp_dgain_q5,
					    uint8_t max_isp_dgain_q5,
					    int32_t hw_max_log2_16)
{
	int32_t limit = t20_ae_isp_dgain_cap(max_isp_dgain_q5, hw_max_log2_16);
	int32_t request = manual ? (int32_t)isp_dgain_q5 << 11 :
		total_log2 - integration_log2 - applied_again_log2;

	if (request < 0)
		return 0;
	return request < limit ? request : limit;
}

/*
 * #267: analog gain allocation like the OEM 3.12.0 cmos_analog_gain_update()
 * (tx_isp_t20_firmware.c, OEM hysteresis 0x1f144-0x1f178): 1/16 stop of the
 * request (0x1000) is left to the ISP digital gain, and a request within
 * +-0xfff of the gain applied last time re-allocates that gain.  Without it
 * the compact AE re-floored the gain to the sensor table on every update, so
 * a 0.5 % AE step next to a table entry toggled the sensor gain (jxh42 3.5x
 * <-> 3.625x) and the picture by ~1 Y.  Used only while the ISP digital gain
 * can carry the remainder (up to 0x1fff): cap >= 0x2000 and no manual ISP
 * digital gain.  All values log2 <<16; prev_applied < 0 = no previous gain.
 */
#define T20_AE_AGAIN_RESERVE	0x1000
#define T20_AE_AGAIN_HYST	0xfff

static inline int t20_ae_again_reserve_ok(uint8_t manual_isp_dgain,
					  int32_t isp_dgain_cap_log2)
{
	return !manual_isp_dgain &&
	       isp_dgain_cap_log2 >= T20_AE_AGAIN_RESERVE + T20_AE_AGAIN_HYST + 1;
}

static inline int32_t t20_ae_again_request(int32_t request_log2)
{
	request_log2 -= T20_AE_AGAIN_RESERVE;
	return request_log2 < 0 ? 0 : request_log2;
}

static inline int t20_ae_again_keep(int32_t request_log2, int32_t applied_log2,
				    int32_t prev_applied)
{
	return prev_applied >= 0 && applied_log2 != prev_applied &&
	       request_log2 >= prev_applied - T20_AE_AGAIN_HYST &&
	       request_log2 <= prev_applied + T20_AE_AGAIN_HYST;
}

/*
 * Manual integration time (SetIntegrationTime MODE_MANUAL / SetExpr manual:
 * stab.global_manual_integration_time = stab[3], lines in
 * stab.global_integration_time = stab+0x18).  The OEM 3.12.0
 * cmos_alloc_integration_time() takes the time from the table instead of the
 * AE, clamped to the sensor window [min, min(sensor max, AE ceiling)]; the
 * AE keeps steering the gain.  Returns the lines to use, 0 = AE owns it.
 */
static inline uint32_t t20_ae_manual_lines(uint8_t manual, uint32_t lines,
					   uint32_t min_lines,
					   uint32_t max_lines)
{
	if (!manual)
		return 0;
	if (lines < min_lines)
		lines = min_lines;
	if (lines > max_lines)
		lines = max_lines;
	return lines;
}

/* With a manual time only the gain is free: keep the AE total exposure
 * (log2 <<16) inside [time, time + reachable gain] so the PI loop cannot
 * wind up while the gain is pinned (and AUTO resumes from a real value). */
static inline int32_t t20_ae_manual_total(int32_t total, int32_t time_log2,
					  int32_t gain_max_log2)
{
	if (total < time_log2)
		return time_log2;
	if (total > time_log2 + gain_max_log2)
		return time_log2 + gain_max_log2;
	return total;
}

/*
 * #252: compact AE step on the log2 error, with the bandwidth of the OEM
 * 3.12.0 AE.  The OEM ae_calculate_target() (ae_balanced_func.c.o
 * 0x37c-0x820) forms the error in log2 Q16 (log2(target) - log2(mean)) and
 * ae_calculate_exposure() (0x82c-0xa70) integrates it with
 * K = AE_BALANCED[0] (AE_PI_COEFF, 10 on jxf23):
 * exposure = (err + acc) / K per AE-stats frame (integrator 0x928-0x9e8,
 * div64 0x9b4-0x9e4), an integral gain of 1/K per frame independent of
 * the metered level.  The compact AE updates every T20_AE_UPDATE_FRAMES
 * frames (sensor latency), so it takes err * T20_AE_UPDATE_FRAMES / K per
 * update: the same 0.1/frame.  Before, the step was |target - mean|
 * (linear 0..255) * gain_accuracy / K, a loop gain proportional to the
 * target: 0.04 at comp 40 (crawl), 1.4 at comp 200 (ringing).
 *
 * Not OEM, kept on purpose: one update is clamped to 1 stop (the measured
 * 0.1 % lamp-step overshoot stays) and |err| <= 1/128 stop is a deadband.
 * The OEM has no deadband; its per-frame dither changes the exposure every
 * frame, which makes iridix_fsm_process_interrupt (OEM 0x21740) skip the
 * 0x3c4 strength commit (0x3dc != 0x100).  With the deadband the compact
 * exposure freezes in steady state and Iridix commits as before.
 * All values log2 Q16.
 */
#define T20_AE_UPDATE_FRAMES	3
#define T20_AE_LOG2_DEADBAND	512		/* 1/128 stop = 0.54 % */
#define T20_AE_LOG2_STEP_MAX	(1 << 16)	/* 1 stop per update */

static inline int t20_ae_log2_stable(int32_t err_log2)
{
	return err_log2 >= -T20_AE_LOG2_DEADBAND &&
	       err_log2 <= T20_AE_LOG2_DEADBAND;
}

static inline int32_t t20_ae_log2_correction(int32_t err_log2,
					     uint32_t pi_coefficient)
{
	int32_t c;

	if (!pi_coefficient || t20_ae_log2_stable(err_log2))
		return 0;
	/* |err| <= 8 stops (8-bit mean), no overflow in err * 3 */
	if (err_log2 > 16 * T20_AE_LOG2_STEP_MAX)
		err_log2 = 16 * T20_AE_LOG2_STEP_MAX;
	if (err_log2 < -16 * T20_AE_LOG2_STEP_MAX)
		err_log2 = -16 * T20_AE_LOG2_STEP_MAX;
	c = err_log2 * T20_AE_UPDATE_FRAMES / (int32_t)pi_coefficient;
	if (c > T20_AE_LOG2_STEP_MAX)
		c = T20_AE_LOG2_STEP_MAX;
	if (c < -T20_AE_LOG2_STEP_MAX)
		c = -T20_AE_LOG2_STEP_MAX;
	return c;
}

/*
 * #267, optional (module parameter t20_ae_flip_guard, default off = vendor
 * behaviour): the AE correction curve is a u8 in 1/32-stop steps.  Where its
 * interpolated value changes by one, the compact AE (block step err*3/K)
 * can cycle across that exposure for ever.  With the guard a one-step
 * change is taken over only once the exposure has moved 1/16 stop away from
 * where the current value was adopted; larger changes pass at once.  The
 * OEM has no such guard (its per-frame PI moves 0.03-0.06 % per frame).
 */
#define T20_AE_CURVE_HOLD_LOG2	4096

struct t20_ae_curve_hold {
	int valid;
	uint8_t curve;
	int32_t exposure_log2;
};

static inline uint8_t t20_ae_curve_hysteresis(struct t20_ae_curve_hold *h,
					      uint8_t curve,
					      int32_t exposure_log2)
{
	int32_t d = exposure_log2 - h->exposure_log2;
	int step = (int)curve - (int)h->curve;

	if (h->valid && (step == 1 || step == -1) &&
	    d > -T20_AE_CURVE_HOLD_LOG2 && d < T20_AE_CURVE_HOLD_LOG2)
		return h->curve;
	if (!h->valid || step) {
		h->valid = 1;
		h->curve = curve;
		h->exposure_log2 = exposure_log2;
	}
	return curve;
}

#endif
