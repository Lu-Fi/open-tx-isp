/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T20/T10 compact AE: the analog gain ceiling the AE can really reach.
 *
 * sensor_drv.c clamps the requested analog gain to the sensor maximum
 * (attr->max_again, log2 <<16) and, when set, to the SetMaxAgain ceiling
 * stab.global_max_sensor_analog_gain (8-bit, log2 <<5, i.e. the same value
 * <<11).  /proc/jz/isp/isp-m0 must publish the lower of the two in the
 * "MAX SENSOR analog gain" line (log2 <<5): streamers read it as the
 * exposure cap.  Before, the dump showed only the sensor maximum (158 on
 * jxf22/jxf23) while the AE stopped at the tuning ceiling (128), so timps
 * believed a cap of 7838 that the AE never reaches.
 */
#ifndef TX_ISP_T20_AE_MAX_H
#define TX_ISP_T20_AE_MAX_H

#include <linux/types.h>

/* sensor_max_log2_16: attr->max_again; user_max: stab ceiling, 0 = none.
 * Returns the reachable maximum in log2 <<5 units. */
static inline uint32_t t20_ae_max_again_log2_5(uint32_t sensor_max_log2_16,
					       uint32_t user_max)
{
	uint32_t m = sensor_max_log2_16 >> 11;

	return (user_max && user_max < m) ? user_max : m;
}

/* The gain the compact AE hands to the sensor allocator at its limit, in
 * log2 <<16 (same clamp order as sensor_drv.c: sensor max, then ceiling). */
static inline uint32_t t20_ae_request_log2_16(uint32_t sensor_max_log2_16,
					      uint32_t user_max)
{
	uint32_t u = user_max << 11;

	return (user_max && u < sensor_max_log2_16) ? u : sensor_max_log2_16;
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

#endif
