/* SPDX-License-Identifier: MIT */
/*
 * T41 per-zone AWB statistics report (IMP_ISP_Tuning_GetAwbStatistics,
 * control 0x8000011, 2700 bytes):
 *
 *	typedef struct { uint32_t statis[15][15]; } IMPISPStatisZone;
 *	typedef struct {
 *		IMPISPStatisZone awb_r;   zone means of the R sums
 *		IMPISPStatisZone awb_g;   G
 *		IMPISPStatisZone awb_b;   B
 *	} IMPISPAWBStatisInfo;        3 x 900 bytes
 *
 * The stock tx-isp-t41 (1.2.x) tisp_awb_get_zone copies the three planes
 * from its AWB state at +60096 (0xeac0), +60996 and +61896; the open native
 * AWB fills the same planes in t41_awb_long_frame as
 *
 *	zone[i] = count[i] ? sum[i] / count[i] : 0
 *
 * where sum is the luminance-class weighted channel sum (>>3) of the raw
 * AWB statistics of that frame and count its pixel count, i.e. the planes
 * are unrounded zone means of the sensor-domain (pre white balance) R, G,
 * B.  Stock T40 (1.3.1) does the same copy (tisp_awb_get_zone, 3 x 900).
 *
 * The open driver adds the freshness contract of tx_isp_awb_zone.h: the
 * planes are handed out only while the statistics stream is live and the
 * last frame that filled them is at most TX_ISP_AWB_ZONE_MAX_AGE_MS old;
 * otherwise the control fails with -EAGAIN (libimp: -1).  The stock copies
 * its state regardless (zeros before the first frame, the last frame after
 * a stop).
 */
#ifndef TX_ISP_T41_AWB_ZONE_REPORT_H
#define TX_ISP_T41_AWB_ZONE_REPORT_H

#include "../include/tx_isp/tx_isp_awb_zone.h"

#define T41_AWB_ZONE_REPORT_BYTES	2700U
#define T41_AWB_ZONE_REPORT_OFFSET	0xeac0U

/*
 * @live: the native AWB statistics path runs (stream started, not stopped)
 * @stamp: jiffies when the report planes were last filled (0 = never)
 * @now: jiffies
 * Returns 1 when GetAwbStatistics may hand out the planes, 0 for -EAGAIN.
 */
static inline int t41_awb_zone_report_fresh(unsigned int live, unsigned long stamp,
					    unsigned long now)
{
	if (!live)
		return 0;
	return tx_isp_awb_zone_fresh(stamp, now, TX_ISP_AWB_ZONE_MAX_AGE_MS);
}

/* a jiffies value that never reads as "never filled" */
static inline unsigned long t41_awb_zone_report_stamp(unsigned long now)
{
	return now ? now : 1UL;
}

#endif /* TX_ISP_T41_AWB_ZONE_REPORT_H */
