/* Host test: T20/T10 reachable analog gain cap (tx_isp_t20_ae_max.h).
 * The isp-m0 "MAX SENSOR analog gain" must be min(sensor max, SetMaxAgain
 * ceiling), the same limit sensor_drv.c clamps the AE to. */
#include <stdio.h>
#include <stdint.h>

#define _LINUX_TYPES_H
#include "../driver/t20/tx_isp_t20_ae_max.h"

int main(void)
{
	int bad = 0;
	const uint32_t jxf = 158u << 11;	/* jxf22/jxf23 attr->max_again */

	bad += t20_ae_max_again_log2_5(jxf, 128) != 128; /* Wyze/Pan: tuning ceiling */
	bad += t20_ae_max_again_log2_5(jxf, 0) != 158;   /* no ceiling: sensor max */
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
	bad += t20_ae_request_log2_16(324678, 0) != 324678;
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
	printf("t20_ae_max_host_test: %s (%d bad)\n", bad ? "FAIL" : "PASS", bad);
	return bad != 0;
}
