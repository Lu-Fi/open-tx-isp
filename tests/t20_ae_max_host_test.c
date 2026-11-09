/* Host test: T20/T10 reachable analog gain cap and ISP digital gain fill
 * (tx_isp_t20_ae_max.h).
 * The isp-m0 "MAX SENSOR analog gain" must be min(sensor max, SetMaxAgain
 * ceiling), the same limit sensor_drv.c clamps the AE to. */
#include <stdio.h>
#include <stdint.h>

#define _LINUX_TYPES_H
#include "../driver/t20/tx_isp_t20_ae_max.h"

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
	printf("t20_ae_max_host_test: %s (%d bad)\n", bad ? "FAIL" : "PASS", bad);
	return bad != 0;
}
