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

#endif
