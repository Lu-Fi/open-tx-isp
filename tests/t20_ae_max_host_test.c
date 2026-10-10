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
	printf("t20_ae_max_host_test: %s (%d bad)\n", bad ? "FAIL" : "PASS", bad);
	return bad != 0;
}
